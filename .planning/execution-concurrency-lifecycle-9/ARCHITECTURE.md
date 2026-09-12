# ARCHITECTURE — Execution / Concurrency / Lifecycle Platform 9.0

基点：`8f6293bceb`。本方向不新建第二套 scheduler/agent runtime/renderer/IO authority/data store。
唯一执行调度链保持：`WorkflowRunCoordinator → TaskCenter → JobEngine → Executor/Operator`。

## 1. 现状结构（8.0 之后的事实）

```
WorkflowRunCoordinator (src/workflow)          [QObject, std::mutex m_mutex, 非递归]
  ├─ 持久 run aggregate（WorkflowRun，内部自带 mutex + 状态迁移表）
  ├─ checkpoint（WorkflowCheckpointManager，per-transition 原子落盘）
  ├─ run 锁（WorkflowRunLock，跨进程所有权）
  └─ notifyRunStateLocked：⚠ 持 m_mutex emit（#860）；ghost swap 静默 erase（#876）
TaskCenter (src/processing/framework)          [QObject, QMutex + QWaitCondition]
  ├─ 准入：ActiveCounters + ready heap + 单一状态缝 setTaskStatusLocked
  ├─ 信号：锁内排队 flushPendingSignals 锁外 drain（正确）
  ├─ 完成回调：exactly-once、锁外、event-loop 无关（正确）
  ├─ ⚠ waitForTask/waitForPipeline：worker 线程可阻塞 30min（#862）
  └─ ⚠ processNextQueuedTasks globalMax 闸门不感知 worker 阻塞父任务（#862）
JobEngine (src/jobs)                           [Qt-free, std::mutex + cv]
  ├─ 优先级桶 + 独占 FIFO + #798 transient allowance（worker 源 submit +1，≤8）
  ├─ waitForJob：worker 线程立即拒绝（#798）
  └─ 单 listener 槽（TaskCenter 拥有）
```

## 2. 核心不变量（9.0 全部用测试守住）

| # | 不变量 | 守护测试 |
|---|---|---|
| I1 | 一个 execution chain；不引入第二 scheduler | 代码审计 + 架构评审 |
| I2 | Task status 迁移单调，terminal 唯一（terminal 后不回非 terminal） | test_execution_plane_9 status-monotonicity |
| I3 | 锁内不 emit Qt signal、不执行外部回调、不做文件/网络 I/O | #860 回归 + 审计矩阵 |
| I4 | worker 同步等待的 child 必然可获得发射（admission 保证），等待有界 | #862 确定性 reproducer |
| I5 | cancellation 最终可观察（每个 cancel 请求在有界时间内产生终态） | cancel-storm 用例 |
| I6 | destroyed run/task/旧 generation 的迟到回调自动丢弃 | M4 late-callback 用例 |
| I7 | persistence 与 in-memory 状态不产生永久 ghost | #876 回归 + restart 对账 |
| I8 | 所有队列、trace、retry、cache 有界 | M8 边界断言 |
| I9 | parent terminal ⇒ 其 owned child 到达定义状态（terminal 或被 cancel） | M1 join 用例 |

## 3. 里程碑设计

### M0 — P0/P1 burn-down（修根因，不收 band-aid）

**#860 修复：通知与状态分离（durable pattern，也是 M4 的地基）**
- coordinator 增加 `std::vector<RunNotification> m_pendingRunNotifications`（m_mutex 保护）。
- `notifyRunStateLocked` → `queueRunStateNotificationLocked`（只拷贝小结构、不 emit）。
- 新增 `drainRunNotifications()`：锁内 swap，锁外逐条 emit。所有会入队通知的公共入口
  （startTrackedPipeline / onTaskUpdated / resumeRun / cancelRun / recoverAtStartup）在释放
  m_mutex 后调用 drain。drain 重入安全：emit 的观察者同步回调 coordinator 时，drain 以
  局部 vector 迭代、不再持锁，重入的 drain 只会 swap 到空表。
- 明确弃用"receivers must not call back"文档约束：同线程同步回调现在安全（#860 验收标准）。

**#876 修复：ghost 必有终态**
- resumeRun swap 处：erase ghost 前，先对 ghost run `forceSetState(Canceled)`（附原因日志）
  并 `queueRunStateNotificationLocked(ghost, ...terminal)`。恢复对账（recoverAtStartup）作为
  第二道防线：非终态且无 pipeline 映射的 run → Interrupted（已有行为，保持）。

**#862 修复（TaskCenter 侧，与 M1 一体）**
- `AlgorithmTaskInfo` 增加 `bool workerOriginated`（submit 时由 `JobEngine::isWorkerThread()` 判定，
  录入 m_tasks 前定死，不可变）。
- `processNextQueuedTasks`：worker-originated 的 ready 候选**绕过 globalMax 与 per-profile 上限**
  （父任务占用的槽在子任务完成前不可能释放；这正是 JobEngine #798 在 JobEngine 层的同一根因），
  受 `kMaxTransientChildren = 8` 总量上限约束；RSS/RAM 预算闸门对 transient child 同样放行
  （never-starve 方向），上限计数防止风暴。trace 事件 `admission/transient_child`。
- `waitForTask/waitForPipeline`：worker 线程立即返回当前快照 + trace `wait_refused`
  （与 JobEngine::waitForJob #798 语义一致；生产代码无 worker 线程调用方，破坏面为零）。
- 回归测试（确定性）：globalMax=1，父任务占唯一槽 → worker 内提交子任务并等待 →
  子任务必须被发射并完成，父任务在界内拿到子结果。全进程无 sleep 依赖。

**#851/#852 复验**：基点已修（8f6293bceb），运行其回归测试并记录证据。

### M1 — Structured Task Hierarchy

- owner 链：JobEngine worker loop 以 thread_local 暴露"当前 job id"（`currentJobId()`）；
  TaskCenter 在 submit 时经 `m_taskByJobId` 解析 owner taskId，写入 `task.ownerTaskId`
  （不可变，-1 = 根任务）。ownerTaskId 与既有 parentTaskIds（DAG 数据依赖）正交。
- join 语义：parent 到 terminal 时，其 owned non-terminal children 走既有级联取消
  （cascadeCancelTargetsLocked 复用）→ I9。
- 取消传播：cancelTask(owner) 级联 owned children（含 transitive）。
- child failure 不自动 fail parent（executor 决策权）；child cancel ⇒ parent 的等待以
  快照观测到终态结束。策略文档化并测试。
- 准入公平性：transient child 与普通候选同堆排序，但 gate 绕过规则如上；transient 上限
  全局有界，完成即释放（复用 setTaskStatusLocked leave 钩子扣减计数）。

### M2 — Thread-Affinity & Snapshot Contract

- 三类对象清单（ARCHITECTURE 附表）：GUI/main-thread-only（DataManager、QGIS 容器）、
  worker-safe（JobEngine、ExecutionFingerprint、remote_identity_resolver）、
  immutable snapshot（AssetSnapshot、ExecutionFingerprint、JobRequest）。
- DataManager 在 8f6293bceb 已有快照提取；9.0 补：`catalogGeneration()` 原子代际 +
  快照携带 generation；debug 构建断言宏 `SICNU_ASSERT_MAIN_THREAD`（qWarning + 落 trace，
  不 abort 生产路径）。
- worker dispatch 前快照/指纹完成的既有缝（computeAndRecordSubmissionFingerprintLocked）
  契约化文档 + 测试（worker 线程绝不触 catalog 的静态断言式用例）。

### M3 — Unified Cancellation / Pause / Resume

- `enum class CancelReason { User, Shutdown, Timeout, Preempted }`：JobEngine record
  statusMessage 已有文本；TaskCenter 侧任务级 `cancelReason` 字段（additive），映射规则
  集中一处。timeout vs user cancel vs shutdown 不混淆（测试断言文本与字段）。
- terminal 单调：setTaskStatusLocked 增加迁移守卫（terminal → 非 terminal 拒绝 + trace），
  排查既有 violate 点（retry 是 NEW task，不受影响）。
- pause/resume：保持 #702 typed refusal（QgsTask-only pause）；补：WaitingResource/Queued
  pause = 推迟发射（新增可行），Dispatching/Running engine 任务 pause = typed refusal
  （现状已对，补测试与文档）。
- resume 无 ghost（=M0 #876 修复的契约化）+ retry attempt identity（已有 autoRetryAttempts，
  补 owner 链下 child retry 的 identity 断言）。

### M4 — Lifecycle-Safe Eventing

- M0 通知队列泛化：coordinator 全部 emit 点审计表（文件:行 → 是否锁内 → 处置）。
- run generation/序号：WorkflowRunCoordinator 通知携带单调 seq（per-process 计数即可，
  additive：不改变既有 signal 签名，新 signal `runStateChangedV2(seq, ...)` 仅供新观察者；
  旧 signal 保持兼容）。ghost swap 后旧 runId 的迟到 onTaskUpdated 因 pipeline 映射缺失
  天然丢弃（已有行为，补测试钉住）。
- shutdown drain/abort：TaskCenter::shutdown 语义（cancel-all → engine join → force terminal）
  已有；补 coordinator 在 shutdown 期间的通知 flush 完整性测试（无事件滞留队列）。

### M5 — Resource Admission 9.0

- actual usage observation：任务 leave 时记录 `observedRamMbDelta`（ResourceMonitor 采样）
  进 trace（有界），estimates 与 observation 的偏差可解释。
- 公平性：既有 epoch FIFO 轮转保持；新增 transient child 不破坏 priority 契约（测试）。
- overcommit/refusal 语义：admissionSnapshot 已暴露 hold reason；补 typed refusal 常量 +
  文档表（每一维度：全局槽/profile/isolated/RSS/RAM/tempdisk/VRAM/ioHeavy/exclusive）。
- 动态限额变更即时重准入（8.0 已有）：补 transient 上限动态性测试。

### M6 — Durable Checkpoint / Crash Recovery 4.0

- 写入原子性核查：checkpoint 落盘路径必须 temp+rename（核查 workflow_checkpoint.cpp，
  缺则补）+ fsync 策略记录。
- corrupt checkpoint：`loadCheckpoint` 失败 → resumeRun typed 拒绝（error 分类：
  corrupt/unsupported-version），不部分加载。
- crash-between-phases 恢复矩阵：persist-before-dispatch / swap 期 crash / finalize 期 crash
  三相位 × 重启行为（restart 对账已覆盖前两相位，补 finalize 期用例）。
- 旧 checkpoint 迁移：未知字段忽略、缺字段保守重执行（8.0 fail-closed 保持，回归钉住）。

### M7 — Execution Observability & Diagnostics

- `TaskCenter::explainDump()` / `WorkflowRunCoordinator::explainRun(pipelineId)`：
  队列深度、active 计数、每维度 hold reason 聚合、transient 使用量、近期 trace 摘要。
  纯读、有界、锁内只拷贝锁外格式化。
- 一个 stuck/deadlock 场景能仅凭 explain 输出定位（M8 用例断言关键字段存在）。

### M8 — Scale / Soak / Fault Matrix

新增 `tests/test_execution_plane_9.cpp`（+ 扩充 test_concurrency_stress.cpp）：
- 10k 短任务（比例断言，既有）+ 100k 逻辑规模断言（admission 数据结构复杂度、内存上界）；
- deep DAG（链 1000）、wide DAG（扇出 1000）、nested child（worker 内两层提交）；
- cancel storm（200 任务中 100 次随机 cancel，终态收敛 + 无悬挂）；retry storm；
- worker crash e2e（复用 7.0 kill 框架）；teardown while running（shutdown drain 完整）；
- bounded memory（RSS sampler 断言）。

## 4. 测试真实性纪律

- bug 修复一律"旧代码必失败"回归测试（M0 三个修复各一条，在新代码上验证通过前先在
  基点二进制上验证失败性——无法运行旧二进制时以代码路径推演 + 新测试针对性断言）。
- 并发用例确定性：rendezvous / QWaitCondition / fault-point，禁止 sleep 碰运气。
- 性能数字记录环境（Release、-j、host），Debug/Release 不互比。
