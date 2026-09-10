# BASELINE — 执行面现状审计（master 2041f6fa, 2026-09-09）

来源：主 agent 精读 + 只读审计 subagent #1（全引用 file:line 可复核）。
前置去重：open PR = 0；open issues = 0（#773-#817 已由 PR #822 关闭）；
#818-#821（data foundation/workbench/cartography/help）已合并；
遗留 dossier 中执行面条目（#749-#754、AUD-P5-25/27）均已由 PR #764+ 修复。

## 组件地图

| 组件 | 位置 | 角色 |
|---|---|---|
| WorkflowRunCoordinator | workflow/workflow_run_coordinator.cpp:137 (startTrackedPipeline:222, resumeRun:509, recoverAtStartup:479) | QObject 单例，桥接 TaskCenter pipeline ↔ 持久 WorkflowRun+checkpoint+ArtifactGC；run-lock 持有者；不调度 |
| TaskCenter | processing/framework/task_center.cpp:94 (admission:1131, flushPendingLaunches:1341, onJobRecord:961, terminal:1517/1729/1778, cancel:1827, retryTask:1941) | 中枢：队列、DAG gating、资源 admission、placeholder、执行缓存；唯一 JobEngine listener（ADR 0051）；QMutex+QWaitCondition |
| JobEngine | jobs/job_engine.cpp:42 (submitWithId:223, cancel:285, workerLoop:572, runOperatorJob:725) | 进程内 std::thread 池（hw-1, floor 2, job_engine.cpp:48-62）；sticky shutdown；#798 子任务 +8 瞬时容量 |
| Executor 解析 | job_engine.cpp:762-790 | per-job executor → prefix executor → RSOperatorRegistry → fallback(AtomicAlgorithmRegistry)（ADR 0062） |
| ExecutionPlane | processing/framework/execution_plane.cpp:42 | TaskCenter 之上的异步 facade；condvar await；commit-once payload 缓存 |
| LocalWorkerHost | local_worker_host.cpp:74 (runInLocalWorker) | 一次性 sicnu_worker QProcess；stdin/stdout v1 协议；**无生产调用方** |
| LocalWorkerPool | local_worker_pool.cpp:94 (start), :368 (run), :489 (health) | 有界热池（max 16）、ready 握手、lifetime recycle；**无生产调用方，仅 tests/test_worker_host.cpp** |
| sicnu_worker | src/cli/sicnu_worker_main.cpp:79 (构建于 src/cli/CMakeLists.txt:108) | 隔离 worker 进程；ready 握手；run/cancel/shutdown；`__hang__` 故障钩子；cancel flag + progress bridge |
| worker_protocol | runtime/worker/worker_protocol.h:30 | 行分隔紧凑 JSON；`v==1` 硬拒绝 |
| OutputCommitter | output_committer.h:66；接线 tool_call_dispatcher.cpp:103-177；seam output_committer_task_center.h:8 | 事务性 temp→stable 发布 + asset 注册 + DerivationRecord |
| ResourceMonitor | resource_monitor.cpp:45-88 | 进程 RSS 水位（默认 75% sysRAM）；Linux /proc、macOS mach、**其它平台返回 0（禁用）** |
| TaskResourceBudget | task_resource_budget.h:71 (canLaunch:98) | RAM 估算 admission 门 + never-starve；估算解析器来自 operator registry（task_center.cpp:341） |
| TaskResourceBudget2 | task_resource_budget2.h:123 | 多维（cpu/vram/disk/net、interactive reserve、aging）。**仅头文件+tests/test_scheduler3.cpp，未接线** |
| GpuPlane | runtime/gpu/gpu_plane.h:96 (ModelSessionPool) | 模型会话池、每设备 VRAM 预算、OOM ladder；**TaskCenter admission 不可见** |
| 执行缓存 | data/execution_fingerprint.h:63/:149；task_center.cpp:2400/2576/2962/2881 | SHA-256 执行身份 + 内存结果缓存 + 内容寻址持久池；**默认关闭**（SICNU_EXECUTION_CACHE=1） |
| Checkpoint/recovery | workflow_checkpoint.h:12；workflow_run_lock.h:35 | 原子 tmp+fsync+replace；~/.rs_studio/checkpoints；flock/QLockFile；历史有界 50 |
| Chunk runtime | runtime/chunk/chunk_pipeline.h:66-99 | 有界 MPMC tile pipeline，tile 间协作取消 |
| 遥测 | runtime/observability/execution_telemetry | 计数器（WorkersSpawned/Crashed、CacheHits/Misses…）+ 事件 |

## 调度机制现状

TaskCenter 不用 QThreadPool/QtConcurrent：admission（processNextQueuedTasks:1268）在 m_mutex 下
staged，flushPendingLaunches:1341 在锁外 submitWithId 到 JobEngine（caller-chosen id
`task-<id>-<uuid8>`:1401，#799 先注册后提交）。JobEngine 自有 std::thread 池执行。
LocalWorkerPool 无任何生产 caller —— selection seam 不存在。
ProviderResourceProfile（algorithm_provider_adapter.h:11）只影响 admission 槽位数
（defaultLimitForProfile, task_center.cpp:252-274），不改变执行位置。

## 缺口矩阵（A-H）

### A. LocalWorkerPool 生产接线
- 已有：热池/握手健康/lifetime recycle(maxJobs 64, idle 10min, cpp:152-185)/崩溃惰性替换(:461-468)/graceful shutdown(:133-144)/health(:489)/销毁 drain(:74-92)/owner-thread 纪律(:376-378)/一次性 host/worker 二进制/8 测试。
- 部分：progress 帧被忽略(:338-339)；取消靠错误串匹配(:342)；250ms 轮询粒度。
- 缺失：生产 caller、配置/env 开关、TaskCenter 路由、capability 交换、generation id、Windows Job Object、Windows kill parity 测试（test_fault_injection POSIX-only, tests/CMakeLists.txt:1683-1686）。

### B. Resource-aware admission
- 已有：全局并发 cap + per-profile caps(#686)、RSS 水位全局 hold(:1229-1244)、RAM 估算门+never-starve(:1246-1259)、估算缓存(:1115-1129)、preflight 覆盖(execution_plane.cpp:323-335)、admissionSnapshot API、GPU ModelSessionPool VRAM 预算（仅推理路径）。
- 部分：TaskResourceBudget2 未接线、LatencyClass 无生产者、GPU 对 admission 不可见、manifest hints 仅 RAM+memoryPolicy。
- 缺失：temp-disk 预算、external-process count 一等维度、I/O-heavy hint、跨进程协调。

### C. Hierarchical cancellation
- 已有：workflow cancelRun→cancelPipeline→cancelTask:1827（传递闭包:590）→ job cancel arming(JobEngine:285, pick/run 窗口闭合:331-342) → worker 协议 cancel+terminate→kill → operator RSOperatorContext cancel flag（rs_kmeans_operator.cpp:174 等）→ tile(chunk_pipeline.h:80)；stranded-Cancelling 收尾(:1703-1726)；shutdown 全取消(:100-160)。
- 部分：无 worker cancel ack（host 用错误串 `"cancelled"` 匹配, pool:342）；取消后 progress 仍转发；QgsTask pause 拒绝 engine 任务(:1878-1912)。
- 缺失：单 operator 内 batch/token 细粒度 scope、取消超时遥测。

### D. Crash/Resume
- 已有：原子 checkpoint（tmp+fsync+MoveFileEx/rename）、启动恢复→Interrupted、跨进程 run lock（flock/QLockFile）、resume 完成步验证（存在+非空+size+mtime+可选 digest≤256MB, coordinator:619-655，fail-closed）、ghost checkpoint 隔离(:751-850)、worker crash fail-conservative 不自动重试(pool.h:17-20)。
- 部分：独立 TaskCenter 任务（GUI dialog/MCP 单调用）无 checkpoint；身份是 per-step 输出级。
- 缺失：moved-output resume（绝对路径身份门）、changed-operator resume（无 diff/replan）、有界 retry classes（全系统无自动重试；仅手动 retryTask:1941 与 worker spawn 3 次, pool:425-431）。

### E. Output Committer / Provenance
- 已有：事务 commit（validate→atomic publish→register→DerivationRecord; 失败全不动, output_committer.h:49-63）；agent/MCP commit-once（tool_call_dispatcher.cpp:103-177）；workflow session commit（workflow_runtime.cpp:526-588）；CLI 直接 DataManager 注册（ADR 0023 偏离, rs_pipeline_runner.cpp:690-730）；output verification enrichment（output_verifier.h）。
- 缺口（不走 seam 的路径）：**agent plan 步骤（agent_workflow_executor.cpp:105,130 TODO P1-E1）**、**image_fusion.cpp:750（#617）**、**execution-cache serve 路径直接写目标文件（task_center.cpp serveFromExecutionCache）**；verification status 未持久化为 governed 字段。

### F. Execution Cache
- 已有：256-bit SHA-256 指纹、contract+platform 版本入身份（execution_fingerprint.h:42-48）、输出键排除的规范 JSON（task_center.cpp:2461-2493）、chained producer 指纹(:2518-2537)、确定性门(:2431-2443)、提交时计算+分发时分歧丢弃(:2400-2425,:2576-2628)、事务 serve+TOCTOU 重验证+Windows rename fallback(:2785-2878)、store stat 绑定(:2881-2959,#749)、路径单一所有权（execution_fingerprint.h:270-272）、自愈 lookup、ArtifactGC protected-provider（task_center.cpp:229-236）。
- 部分：默认关；hit 验证是 stat/digest 级、无 post-serve open 验证；LRU 按条目数(4096)；持久池无 size 配额。
- 缺失：provider 算法（gdal:/otb:）/callables 不缓存(:2423-2429)、多进程共享内存 map、**remote identity adapter seam 无接口**。

### G. Worker protocol
- 已有：major 版本前置拒绝；run/cancel/shutdown/ready/progress/result/error；one-run-per-jobId；EOF=shutdown；崩溃≠host 崩溃。
- 部分：progress 被 pool 忽略；cancel ack 靠字符串；stderr 从不收集。
- 缺失：capabilities、health/heartbeat、telemetry 帧、结构化错误码、输出身份/工件清单帧、min/max 版本协商。

### H. 并发/死锁
- 已有缓解：#798 子任务 +8 瞬时 worker + 饥饿守卫；worker 线程 sync 子等待拒绝（waitForJob:457-461）；hooks/notify 锁外（:356-368,:700-716）；TaskCenter 信号/回调全在 m_mutex 外（task_center.h:303-330）；#799 先注册+catch-up 快照；cancel/close pick→run 窗口（job_engine.cpp:331-342）；pool owner-thread+destroying-drain；coordinator flock-before-mutex（coordinator:482-487）、#720 run 对象 swap（:364-368）、:307-348 漏窗折叠；QgsTask cancel QPointer+QueuedConnection。
- 残留风险：LocalWorkerPool.run() 阻塞 JobEngine 线程最长 30min（接线后每 worker job 占一个 engine 线程）；waitForTask/waitForPipeline 持 m_mutex 等条件（task_center.cpp:2323-2357，唤醒间锁竞争）；ExecutionPlane 全局 commit mutex、调用线程执行 commit。

## Windows/Linux parity 现状

1. RSS 采样 Windows 返回 0 → 内存门禁静默禁用（resource_monitor.cpp:66-70）。
2. run lock：flock（内核死亡释放）vs QLockFile 启发式（workflow_run_lock.h:37-48）。
3. QFile::rename 覆盖已存在目标在 Windows 失败 → 非原子 remove-then-rename 窗口（task_center.cpp:2864-2875）。
4. test_fault_injection 不在 WIN32 构建（tests/CMakeLists.txt:1683-1686）。
5. QProcess::terminate 在 Windows 发 WM_CLOSE，sicnu_worker 不处理 → cancel 升级实际总是 kill()。
6. worker 无 Windows Job Object：host 崩溃依赖 stdin EOF 自退；wedged worker 忽略 EOF 即孤儿。

## 十大承重事实（改动前必读）

1. LocalWorkerPool/sicnu_worker 生产死代码；worker runtime 工作从创建 selection seam 开始。
2. 执行 = JobEngine std::thread 池，永远进程内；池大小 hw-1（min2/max64），TaskCenter admission 刻意等于它（#686）——改一不改二会再造"假 Running"。
3. 唯一 JobEngine listener 属 TaskCenter（ADR 0051），每次 submit 重装；测试不得在 TaskCenter 任务飞行时自装 listener。
4. 提交顺序承重：#799 先注册后提交 + catch-up 快照；Running 只来自 engine Running 记录（#686）。
5. TaskCenter 一切在 m_mutex 下 stage、锁外 flush；无锁不得调 processNextQueuedTasks，锁内不得 emit/commit。
6. 执行缓存 opt-in 且 fail-conservative：需注册 RSOperator + 确定性 opt-in + 提交线程 catalog；任何不可识别输入/分发分歧 = 无指纹。kExecutionFingerprintContractVersion 升位即全失效。
7. Resume 身份 fail-closed：size+mtime(+digest≤256MB) 不符即重跑；协调器锁内不算 hash（:353-362 先算后 fold）。
8. 取消端到端协作式：每 job 一个 atomic flag 入 RSOperatorContext；engine cancel 只是 arming，终态由 operator 退出产生。
9. workflow 恢复锁序：先 flock 后 m_mutex；runStateChanged 持锁发出、不得同步重入。
10. 平台门禁在 Windows 静默降级：RSS=0、QLockFile、非原子 rename、fault-injection 套件不构建。Linux 上测的资源行为≠Windows 发行行为。

## 故障矩阵覆盖现状

覆盖良好：worker crash/hang/cancel 升级/池生命周期（test_worker_host）；engine 并发/竞速取消/优先级/#798/快速提交取消（test_job_engine）；级联取消/排队取消#683/retry 不双跑#616#685/admission#686/RSS hold（test_task_center 31 例）；#559 死锁/exactly-once/超时取消晚完成/committer 拒绝/shutdown 唤醒（test_execution_plane）；resume 矩阵（test_workflow_run_coordinator/test_workflow_recovery/test_fault_injection）；缓存故障自愈#749（test_workflow_cache_e2e/test_workflow_incremental_cache/test_execution_fingerprint）；GPU 预算（test_gpu_plane）；调度维度（test_scheduler3）；committer（test_output_committer）；规模（test_workspace_stress 100k; test_execution_benchmarks 14 ebench）。

薄弱/空白：app-shutdown 时 worker 进程在飞（仅一个遥测事件）；disk-full（FAULT_MATRIX_4 row 24 接受债务）；WaitingResource 风暴下排队取消；跨进程算子 schema 升级后的 cache stale；changed-operator resume（无机制无测试）；moved-output resume（无）；进程池子任务死锁（#798 仅线程池）；Windows kill parity（不构建）；worker stderr 诊断（缺失）；progress 透传（缺失）；有界自动重试（无）。
