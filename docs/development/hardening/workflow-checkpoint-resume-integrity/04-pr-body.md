# fix(workflow): checkpoint/resume/path integrity — writer cap, ownership lock, fail-closed executor

## 目标（Track: workflow-checkpoint-resume-integrity）

仅针对 `src/workflow/**` 及其直接测试的硬化：把本 track recon 在当前 master 上实证的 6 个 correctness/integrity 缺陷按“先立 oracle、再修、再杀回归”逐个关闭。无新产品方向、无第二事实源、无新抽象（唯一新依赖复用既有 `WorkflowRunLock` authority）。

## Recon 基线

- master：`a9dc33fa73`（含 #1225 durability batch、#1200 deep-review remediation、#1224 atomic_fs 集中）。
- Open PR 去重：仅 #1237（teaching cockpit，不触 `src/workflow/**`；本 PR 不碰 teaching/app shell，根/tests CMakeLists 零改动——新测试全部追加进既有测试 TU）。
- Open issues：0。
- 历史分支去重：`agent/flash-workflow-integrity` 的 6 个线索 commit（#1037/#1038/#1056/#1069 时代）经逐 diff 对照，其防护已被 master（`f4758f2069`/#1107、`01d904297c`、#1225、#1200、#1224）完全吸收或被更强机制（内容指纹、delete-stale-before-execute、两份本地 containment 实现）取代——**零代码移植**，仅吸收其测试思路。详见 `.planning/hardening-workflow-checkpoint-resume-integrity/02-history-dedup.md`。

## 修复清单（每项：缺陷 → 修复 → 回归 oracle）

### A. IR2 executor 参数转换：crash + fail-open（P0）
- 缺陷：`qJsonObjectToJsonCpp` 转换失败返回空对象继续执行（operator 以默认参数跑、发布用户未请求的产物）；且 jsoncpp 超过默认 stackLimit 时 `CharReader::parse` 抛 `Json::Exception` 而此处无捕获 → 异常穿出 executor 进入 D17 QThreadPool worker → `std::terminate`。实测窗口：Qt6 解析接受嵌套 ≤1023 层，jsoncpp 默认（本构建 256、上游 1000）拒绝 >256 → **256–1023 层的外部 workflow 参数即崩溃面**。
- 修复：转换返回 `std::optional`、捕获 `Json::Exception`，失败 → 类型化拒绝（`ir2.operator_failed: parameters ... cannot be converted`），operator 永不执行。附带 `isSafeNodeId` 拒绝 `:`（NTFS ADS：默认产物名分叉到备用数据流，字节永不落在声明输出上；POSIX 上 `:` 为合法文件名所以旧代码静默通过）。
- Oracle：`test_ir2_port_param_mapping.cpp` 新增 2 个 TEST_CASE（1020 层参数的拒绝 + 浅层正向对照；`host:stream` nodeId 拒绝）。旧实现 RED：深层参数直接异常/abort；`:` id 在 Linux 成功写出文件。
- subflow 兼容性：composer 的展开前缀是 `instanceId + "__"`，lab 语料与测试无 `:` node id（已扫描 data/labs/*.json 与全部测试）。

### B. Engine-2 checkpoint 写侧无 cap（P1）
- 缺陷：`saveCheckpoint` 不检查大小；读侧 `loadCheckpoint` 拒收 >16MiB → 大定义写出**自身 loader 必拒**的 checkpoint，每次恢复 warn+skip → run 静默不可恢复。D17 与 provenance 写侧已有 writer cap，Engine-2 是漏网点。
- 修复：序列化后、写盘前检查 `kMaxCheckpointDocumentBytes` → qWarning + 拒绝（明确的保存失败优于静默不可恢复）。
- Oracle：`test_workflow_recovery.cpp` `write-cap`：16MiB+1024 blob → save 返回空、无目标文件、无 tmp 残留。旧实现 RED：文件被写出。

### C. 空 stepId 定义自产毒 checkpoint（P1）
- 缺陷：`workflowDefinitionFromJson` 容忍空/缺 `id`（且**静默跳过非对象 step 条目**——silent truncation）；`createFromDefinition` 原样复制 → 两条 `stepId:""` 的 plan → checkpoint 保存成功、加载必以 "duplicate stepPlans id ''" 拒收 → run 永不可恢复。IR2 侧早有对应闸（"node: missing 'nodeId'"），Engine-2 定义侧是漏网点。
- 修复：解析期空 id / 非对象条目 → 类型化错误；`createFromDefinition` 二道防线（编程式定义绕过 JSON 门）返回 nullptr。
- Oracle：`test_workflow_recovery.cpp` `step-id`：双空 id 定义被拒 + 非对象条目被拒 + 编程式 nullptr + 合法定义 save/load round-trip 正向对照。

### D. Engine-2 tmp sweep 无视 live-owner（P2）
- 缺陷：`recoverInterruptedRuns` 无条件删除全部 `checkpoint_*.json.tmp.*`，先于任何 per-run lock 检查、无视 tmp 名内嵌 pid → 并发场景下删掉活进程 in-flight save 的 tmp，其最终 rename 失败、checkpoint 永不出现。违反 #727 “liveness 由锁原语判定”。
- 修复：tmp 清扫逐文件提取 runId → `WorkflowRunLock::probeOwner`，LiveOwner 跳过（复用既有 authority，`--list-runs` 同款 lock-free 探测）。
- Oracle：`test_workflow_recovery.cpp` `tmp-lock`：checkpoint+tmp+持锁 → pass 1 不动 tmp 不恢复 run；释放锁后 pass 2 清扫并 reconcile。旧实现 RED：持锁时 tmp 已被删。

### E. D17 无跨进程 resume 所有权锁（P1）
- 缺陷：`PipelineRunCoordinator` 全文无 WorkflowRunLock——两个进程（或同进程两个 coordinator）同时 resume 同一 checkpoint 均成功 → 双执行剩余节点 + checkpoint 互相覆盖；fresh startRun 中途也可被并发 resume。Engine-2 对 start（#727）与 resume 均持 flock，D17 是 workflow 模块唯一无跨进程所有权的执行面。
- 修复（复用既有 authority，零新机制）：`resumeOnAffinity` 在昂贵的 artifact 验证循环**前** tryAcquire（被拒 peer 快速失败），任何 fail 路径由局部 unique_ptr 自动释放，commit 时移交 `m_state->runLock`；`startRunOnAffinity` 为 fresh UUID 持锁（与 Engine-2 `startTrackedPipeline` fresh-run 持锁 parity，m_state 变更前获取）；`finalizeIfDone` 在最终 persist **后**释放（peer 永不见半发布终态，终态 checkpoint 可被全 CacheHit 复验）；锁路径用 `QDir::absolutePath` 归一（相对 runDirectory 在不同 CWD 下会派生不同 lock 文件）。析构兜底释放。header 契约注释同步。
- Oracle：`test_d17_workflow_pipeline_e2e.cpp` 新增 2 个 TEST_CASE：mid-run peer resume 被拒（含 checkpoint 字节不变断言）+ 终态后第三者全 CacheHit 复验成功；double-resume（A 持锁执行中 B 被拒）。同进程两 coordinator 的 flock 冲突即跨进程语义（flock 按 open file description 冲突），无需真实双进程。旧实现 RED：peer resume 成功（双执行）。

### F. D17 外线程析构 UAF 窗口（P2，契约修复）
- 缺陷：`~PipelineRunCoordinator` 的 drain functor（BlockingQueuedConnection）会在 `onNodeFinished` 的 whole-file hash **泵内被重入执行并返回**，外线程随即继续析构 `m_state`，而 affinity 线程泵返回后继续访问已释放状态（snapshot 引用/persist/finalize）。header 明确承诺 "a foreign-thread destruction is safe"——契约被实现破坏。
- 修复：`RunState::affinityBusy` 深度计数（onNodeFinished RAII guard）；析构在外线程路径 drain 后等待 busy==0（cancel flag 已保证 hash 一个 chunk 内中止，等待有界）；hash 后补 `shuttingDown` 再检查，销毁中放弃 completion。affinity 线程自身析构保持文档化的内联快速路径。
- Oracle：`test_d17_workflow_pipeline_e2e.cpp` `uaf`：32MiB 产物（Auto→full hash，30+ 泵窗口），0ms timer 在泵内触发外线程 delete，断言存活且析构完成。

## 测试证据

- 新增 7 个 TEST_CASE（6 文件内追加，**零新测试可执行文件、零 CMake 改动**）。
- 杀伤力证明（sabotage/RED）：每项修复在临时 revert 后由对应新测试变红（记录见 `.planning/.../03-implementation-ledger.md` 验证记录节 + PR 评论数据）。
- targeted 套件（两遍）：`test_ir2_port_param_mapping`、`test_workflow_recovery`、`test_workflow_checkpoint_cache`、`test_d17_workflow_pipeline_e2e`、`test_workflow_ir_v2`、`test_workflow_run_coordinator`、`test_workflow_durability_13`。
- 平台注意：POSIX 路径验证于 Linux；Windows/macOS 特有行为（ADS、QLockFile、case-insensitive containment）由平台无关节点单测与既有跨平台测试覆盖，未在真实 Windows 上运行（online CI not awaited）。

## 已知限制 / 记档不实现（防模糊 TODO）

1. Engine-2 resume swap 后 racing fold persist 可复活**惰性** terminal-Canceled ghost 文件（诊断噪声，非损坏、无双执行）。公开面无法确定性驱动该窗口（`onTaskUpdated` 为 private slot），按 track 规则“每个实现改动绑定 oracle”记档并附修复草案（swap 在 IO 锁内提升 `m_latestPersistSeq` watermark 后 remove），留待有测试 seam 的后续。
2. IR2 执行窗口内非声明陈旧文件校验残余缺口：构造型场景（operator 忽略 output 契约 + expectedOutput 恰为 input 引用路径），#1032 authorship 已覆盖主路径。
3. 路径 containment 三处实现的语义统一（executor 前缀式 vs D17 canonical+平台大小写 vs Engine-2 digest 锚定）：合并涉及语义决策，记录为后续收敛候选，本次不做有风险的重构。
4. 大 workflow O(V²E) 热点（plan_optimizer、每节点全量 checkpoint 重写）：取消响应性与内存上界设计达标，无实测退化证据——按 track 规则不硬造 micro-optimization。

## 回滚

单 PR 纯增量修复，`git revert` 即可整体回滚；各 slice 相互独立，可按 commit 拆分回滚。
