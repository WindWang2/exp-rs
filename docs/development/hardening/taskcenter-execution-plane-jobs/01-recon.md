# Recon — TaskCenter / ExecutionPlane / Jobs / Output Commit 生命周期强化

- worktree: `exp-rs-hardening-taskcenter-execution-plane-jobs`, branch `hardening/taskcenter-execution-plane-jobs`
- base: origin/master `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`（启动时与 seed 一致）
- open PR: 仅 #1237（teaching cockpit，own `src/teaching/**`、`src/app/teaching/**`；对根 CMake/`src/app/CMakeLists.txt`/`tests/CMakeLists.txt` 为 append-only）。本 Track 不与其争抢。
- open issues: 0。

## 一、现状矩阵（核心组件）

| 组件 | 权威数据源 | 主要调用者 | 错误模型 | 资源上界 | 线程模型 |
|---|---|---|---|---|---|
| TaskCenter (`task_center.h/.cpp`, sicnu_task_center) | `m_tasks`, `m_taskByJobId`, `m_active`(增量计数), `m_readyHeap`+`m_readySerial`, `m_ownedChildren`, `m_fusedChains`, fingerprint maps | ExecutionPlane, ToolCallDispatcher, GUI dialogs, agent surfaces, workflow | 状态机+回调；-1 拒绝哨兵；终态吸收（I2） | globalMax(=JobEngine::maxWorkers), per-profile limits, RAM budget, RSS watermark, budget2 (tempDisk/VRAM/cpuThreads/io weights), kMaxTransientChildren=8, maxPending=4096 | 单 `m_mutex`；信号在锁外 flush；watchdog 线程独立锁域（m_watchdogMutex） |
| JobEngine (`src/jobs/job_engine.cpp`) | `m_jobs`, `m_jobBodies`, `m_cancelFlags`, `m_transientBacked`/`m_transientAllowance` | TaskCenter (listener ADR 0051), 直接提交方 | JobRecord.state + error；cancel 返回 bool | maxWorkers(2..) + kMaxTransientWorkers=8 | worker threads（t_isWorkerThread/t_currentJobId thread_local）+ `m_mutex` |
| ExecutionPlane (`execution_plane.cpp`) | ExecutionHandle::Shared; `m_commitCache`(commit-once) | ToolCallDispatcher, MCP/agent | Json error payloads | kMaxCommitCacheEntries | 锁内 flag+cv；commit 互斥 m_commitMutex |
| OutputCommitter (`output_committer.cpp`, sicnu_processing) | publish-then-swap（.new/.old），sidecar-first/primary-last | ToolCallDispatcher handler（经 buildTaskResultPayload） | CommitResult diagnostics | — | 调用方线程（DataManager affinity） |
| LocalWorkerPool (`local_worker_pool.cpp`) | `m_idle`, `m_alive`, lease verdicts | worker_execution_route shared pool, local_worker_host | Outcome 枚举→runtime_error（前缀=transient 契约） | maxWorkers clamp 1..16 | QProcess per-thread affinity; 进程等待永不在 m_mutex 内 |
| worker_execution_route | g_pool（故意泄漏的单例）, g_configuredMode | TaskCenter staging（shouldRunIsolated→makeIsolatedWorkerExecutor） | fail-closed（拒绝 in-process 回退） | isolatedJobLimit (default 2) | g_poolMutex 仅保护 start/shutdown |
| execution_resource_bridge | NVML inventory → setVramBudgetMb | host wiring | VramWireResult | — | once_flag |

## 二、全链路不变量（已确认 master 已有）

- submit→admit: enqueueTask 锁外 warm（#1097/#930），锁内再查 shutdown/pending bound；submitJob 与 enqueueTask 同一条 gated admission（#683/#686）。
- 就绪堆: priority-major + epoch FIFO rotation + serial 惰性失效；`setTaskStatusLocked` 是唯一状态缝，维护 m_active/m_liveTaskCount/watchdog index/owned-children staging。
- dispatch: 预注册 jobId（#799）→ submitWithId 冲突拒绝回滚 → cancel-in-flight 双侧处理（review L P3）。
- cancel/retry: `shouldAutoRetryLocked` 拒绝 Cancelling/已 stamp 的任务（#1182）；resurrect 清 runStartStamp/cancelRequestStamp/fingerprints（#1233）；stranded-job finalize 立即清 jobId 映射；watchdog armed-index O(armed)（#1159）。
- commit: ExecutionPlane commit-once cache + #1042 publication gate（rollback 在 cache 前）；null-handler 构建不缓存（#1233）。
- OutputCommitter: sidecar-first/primary-last（#1174）、.old 回滚保原输出（#617）、注册失败回滚、publish 失败回滚。

## 三、缺陷短list（本轮实施对象）

### D1 (P2) — 就绪堆候选在饱和 fast-path `break` 上被丢弃 → 任务可被无限搁浅
`task_center.cpp:2499-2514`：`transientCapacityAtPassStart==false` 时 `break` 直接丢掉已弹出的 heap 头部条目；`m_readySerial` 仍保留 serial，但堆里没有条目，后续 pass 只扫堆。aging sweep 只 re-push *被晋升* 的候选（`effective < task.effectivePriority`），对已处最高优先级（High）或 aging 关闭的候选永不恢复 → 全球槽位释放后该任务永远 Queued。
触发面：≥8 个 worker-originated bypass 任务 active（transient 预算耗尽）且 global slots 饱和时，任何普通候选的入队 pass 即丢弃。MCP/workflow agent-loop（worker 线程大量提交子任务）正是 #862 设计的常规场景。
修复：break 前把弹出条目 requeue 回堆（保留 O(1) 饱和语义——只回推头部即退出）。

### D2 (P2) — auto-retry 不取消死 job → zombie 双生产 + clientTag 复活
`task_center.cpp:3455-3510`（auto-retry 块）vs 3518-3520（普通失败路径）：普通路径按 #702 把 rootJobId 交给 `jobCancelTargets`（"externally-driven failure must kill the still-running engine job"），auto-retry 路径只清映射不取消。`markTaskFailed` 是公共 API（生产调用方 `src/app/dialogs/async_algorithm_runner.cpp:79`）。外部以 transient 前缀消息调用且 job 仍在跑时：resurrect + 新 dispatch 与旧 job 并行；旧 job 完成后其终态记录经 clientTag 恢复（`task_center.cpp:1847-1864`，`jobId.empty()` 分支）重新绑定任务 → 旧 attempt 的 payload 完成任务（双生产、溯源错误）。
修复：auto-retry 块同样把 deadJobId emplace 进 `jobCancelTargets`，并让 `autoRetried` 早退路径也走 `dispatchPendingCancels`（引擎对已终态 job 的 cancel 是无害 no-op；对 Running job 置 cancel flag，`finishSuccess` 会把它标为 Cancelled）。
同块顺带（P3 防御）：清 `m_forwardedLogCounts`/`m_lastForwardedProgress`，消除复活尝试的日志/进度被 stale 去重键吞掉的窗口（新 attempt 的 delta 记录 engineIndex 从 0 起，`seen` 却残留旧值）。

### D3 (P3) — OutputCommitter publish sidecar 词表缺 `.aux.xml`（三处词表漂移）
`output_committer.cpp:160-164` publish 列表（.shx/.dbf/.prj/.cpg/.sbn/.sbx/.qix/.shp.xml/.tfw/.aux）缺 `.aux.xml`；同 TU `discardTemporary`（337+）单独处理 `.aux.xml`；`ArtifactGC::kSidecarSuffixes` 与 `atomic_fs::sidecarsFor` 都含 `.aux.xml`。后果：GDAL PAM 文件不随数据集发布（stats/掩膜丢失）且残留在 temp 目录（文档契约"temps removed only after full publish"未覆盖它）。
修复：TU 内收敛为一个文件级常量（publish 与 discard 共用），补 `.aux.xml`。不切换到 `atomic_fs::sidecarsFor`（清理侧词表含 `.sicnu-manifest.json`/`.jpw` 等，发布语义不同，over-publish 风险），在 PR 已知限制中记录三词表收敛为后续项。

## 四、排除项（证据记录，不移植）

- `agent/flash-processing-atomic-errors`（sink 写入检查、postProcess-once）：已被 #1200/#1216/#1224 之后的 master 覆盖（atomic_fs 契约 + #1225 durability batch）。
- `agent/flash-workflow-integrity`（IR2 authorship、marshal run-state）：workflow 域（#1225 已合），非本 Track ownership。
- `agent/flash-data-transaction-integrity`（WorkspaceCatalog/ArtifactStore 事务）：data 域，#1217 已合 store integrity。
- ExecutionPlane commit cache null-handler 不缓存：#1233 已修（读代码确认在位）。
- runStartStamp retry 重置、stranded finalize 清映射：#1233 已修（确认在位）。
- LocalWorkerPool m_alive 记账：逐路径核对（prewarm/reserve-spawn/force-retire/retire）无双计；#1090/#932 修复在位。
- watchdog 锁域、stopWatchdog join 语义、shutdown 后 ensure 拒绝：在位且正确。

## 五、测试面

- `tests/test_task_center.cpp`（基础）、`test_task_center_12.cpp`（fairness/backpressure/watchdog/weights/telemetry/fault-points/#1159 bounded scan）
- `test_execution_plane*.cpp`（base/7/8/9）、`test_output_committer.cpp`（#617 rollback、discard sidecars、cross-tree）、`test_job_engine.cpp`、`test_worker_host.cpp`、`test_fused_chain.cpp`
- 新 oracle 落点：D1/D2 → `tests/test_task_center_12.cpp`（复用 harness：registerExecutor prefix、blocking executors、transient children）；D3 → `tests/test_output_committer.cpp`。
