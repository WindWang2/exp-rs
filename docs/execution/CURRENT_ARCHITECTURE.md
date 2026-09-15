# CURRENT_ARCHITECTURE — 执行底座权威与接缝图（Execution Runtime 11.0）

基线 `origin/master@a5b11b7f` + 本 track 增量。本文是执行域的 authority 真值图；修改任一权威所有者必须同步本文与 `tests/test_execution_authority_11.cpp` 的 allowlist。

## 1. 权威所有者（单一真值）

| 关注点 | 唯一权威 | 位置 | 说明 |
|---|---|---|---|
| 任务状态 + DAG + admission + auto-retry | **TaskCenter**（singleton QObject） | `src/processing/framework/task_center.{h,cpp}` | TaskStatus 超集（Paused/WaitingResource/Dispatching/Cancelling）；ready-heap 准入（优先级/全局槽位/per-profile 上限/RSS/RAM/临时盘/VRAM）；transient 自动重试 0..3。 |
| Job 调度（线程池） | **JobEngine**（singleton, Qt-free） | `src/jobs/job_engine.{h,cpp}` | 唯一 job 线程池（cores−1，floor 2）；优先级桶 + exclusive FIFO；`submitWithId` 由 TaskCenter 预注册 task↔job 映射。**任何新并发形态必须落这里或经其批准**（见 §4 架构网）。 |
| 持久 run 生命周期 | **WorkflowRunCoordinator** | `src/workflow/workflow_run_coordinator.{h,cpp}` | 唯一持久状态机：原子 checkpoint、startup 恢复、resume 跳过已验证输出、跨进程 run 锁。 |
| 出版本（事务） | **OutputCommitter** | `src/processing/framework/output_committer.cpp` | publish-then-swap + 回滚；任务级 commit-once 由 ExecutionPlane 缓存守卫。operator 级 partial-output 删除由 `PartialOutputGuard`（rs_partial_output_guard.h）承担。 |
| Worker 进程生命周期 | **LocalWorkerPool / LocalWorkerHost** | `src/processing/framework/local_worker_{pool,host}.cpp` | 自我声明"NOT a second job scheduler"：warm 隔离 sicnu_worker 进程，路由由 worker_execution_route 决定；lease/poison 判定委托 `sicnu::runtime::worker::WorkerLeaseTracker`（本 track 新增，见 §3）。 |
| VRAM / 模型会话 | **ModelSessionPool (gpu_plane)** | `src/runtime/gpu/gpu_plane.{h,cpp}` | 身份键控会话池 + OOM 阶梯（Acquired/Reduced/CpuFallback/Busy）。 |
| 遥测/跟踪 | **ExecutionTelemetry + Trace** | `src/runtime/observability/*` | telemetry：8192 ring + 原子计数器（有界）；trace：NDJSON span 链（Pi/Harness→Workflow→TaskCenter→JobEngine→Worker→Operator→OutputCommitter）。 |

## 2. 取消链（唯一路径）

```
surface(GUI/CLI/agent/workflow)
  → TaskCenter::cancelTask/cancelPipeline（cascade: descendants + owned children + QgsTask + scratch）
    → JobEngine::cancel（queued 同步取消；running: per-job flag + CancelHook，锁外触发）
      → RSOperatorContext::isCancelled()/throwIfCancelled()（flag 优先，callback 次之）
        → operator body 轮询（throwIfCancelled）
        → chunk 执行：ChunkPipeline/ChunkGraph.setCancelFlag(context.cancelFlag()) + body 内 isCancelled() 轮询
          （fused_chain 自 11.0 起桥接；此前 cancelFlag 死代码）
      → 终态 Cancelled → TaskCenter markTaskCanceled（typed TaskCancelReason）→ coordinator rollup
```

## 3. 本 track 新增的底座接缝（Qt-free，位于 src/runtime）

```
RSOperator::run(params, RSOperatorContext)
  └─ adoption kit: operators/framework/chunked_run.h —— runChunkedOperator(ctx, TileRunSpec, kernel)
       ├─ runtime/chunk/tile_run_contract.h    统一 TileRun 合约（identity/halo/determinism/envelope）
       ├─ runtime/chunk/resumable_tile_run.{h,cpp} crash-safe resume driver（journal+checkpoint+exactly-once publish）
       ├─ runtime/exec/execution_governor.{h,cpp} 统一预算面（RAM planner/scratch/write-gate/leak 检测）
       ├─ runtime/chunk/{chunk_pipeline,chunk_graph} 有界流水线（consumer-abort fail-closed；per-chunk telemetry 采样）
       └─ runtime/worker/worker_lease.{h,cpp} lease/poison 判定（host 侧，注入时钟）
错误信封：chunk 异常族 → operators/framework/chunk_error_bridge.h → RSOperatorError
（Cancelled / CorruptArtifactData / ResourceBudgetExceeded / ComputationError）
```

## 4. "无第二 scheduler" 架构网

`tests/test_execution_authority_11.cpp` 以源扫描冻结 scheduler 形态（std::thread/jthread 池、QThreadPool、QtConcurrent::、std::async）的文件 allowlist（36 个，BASELINE.md）。新增命中即测试失败。现存合法形态分三类：
1. **job 级**：JobEngine（唯一跨 job 调度器）；ChunkedProcessor 的 job 内 QThreadPool fan-out（在 job 预算内，注释已知）。
2. **job 内流式 runner**：ChunkPipeline/ChunkGraph per-node 线程（生命周期=run() 调用）。
3. **IO/辅助线程**：trace 后台写、worker/插件进程 stdin 读、GUI 预览池、governance 导入池、pipeline_run_coordinator 进度聚合——均不做 job 调度决策。

## 5. 已知重复/残留（记录，不扩权）

- ExecutionPlane 的 ExecutionState 是 TaskStatus 镜像 + TimedOut（文档化镜像，第三枚举——保留：plane 需在无 TaskCenter 语境下运行）。
- transient 允许量两层各 8（TaskCenter kMaxTransientChildren / JobEngine kMaxTransientWorkers）——有意分层（DAG 子任务 vs 引擎槽位）。
- ChunkedProcessor 注释声称 JobEngine clamp 2..4 与实际 cores−1 不符（P3，域属 src/processing）。
