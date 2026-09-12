# OWNERSHIP — execution-concurrency-lifecycle-9

## 本方向拥有（可自由修改）

- `src/workflow/**`（workflow_run_coordinator / workflow_run / workflow_checkpoint /
  workflow_run_lock / workflow_runtime / workflow_session / artifact_gc / gate 等）
- `src/processing/framework/task_center.{h,cpp}` 及 execution 框架内
  execution_plane / output_committer_task_center / tool_call_dispatcher_task_center /
  local_worker_host / local_worker_pool / worker_execution_route / worker_process_guard /
  resource_monitor / task_resource_budget*
- `src/jobs/**`（job_engine / job_types）— JobEngine 执行内部
- `tests/test_task_center.cpp`、`tests/test_job_engine.cpp`、`tests/test_worker_host.cpp`、
  `tests/test_workflow_run_coordinator.cpp`、`tests/test_execution_plane*.cpp`、
  `tests/test_concurrency_stress.cpp`、新增 9.0 测试文件
- workflow checkpoint / resume / cancel / pause / recovery 语义
- execution trace / runtime lifecycle 窄缝

## 允许的窄共享 seam（只加不改语义，最小增量）

- `src/data/data_manager.{h,cpp}`：仅 M2 的 detached-snapshot / generation 契约在 8f6293bceb
  已有改动之上的**最小补充**（快照读接口命名、affinity 断言钩子）。不重排其内部 schema。
- `src/data/execution_fingerprint.*`：checkpoint identity pin 所需的最小扩展。
- `src/processing/algorithms/temporal/temporal_workspace.*`：仅快照接缝调用方适配。
- `CMakeLists.txt` / `tests/CMakeLists.txt`：milestone 末尾一次性注册新测试文件。
- `CHANGELOG.md`/docs：PR 前最小增量。

## 禁止修改（其他并行方向核心）

- `src/app/**`（Workbench/Track 7）：#859/#858/#861 等 UI 生命周期修复归其所有。
- scientific kernels（`src/processing/algorithms/**` 中非 execution 框架文件）
- model runtime 内部、cartography（`src/agent/cartography/**`、`src/operators/**`）
- plugin SDK、`src/dataset/**`、experiment store schema
- `src/geospatial/**`（I/O authority；canonical_metadata 的 -fpermissive 依赖不改代码，
  以构建配置对齐主构建）

## 与并行 track 的集成契约（本方向提供，不跨域重写）

1. **Detached snapshot**：主线程权威对象（DataManager/temporal catalog）向 worker dispatch
   提供代际化快照；worker 侧永不直接读 mutable 容器。
2. **Cancellation handle**：operator/外部 provider 可获得的统一取消观察句柄（M3）。
3. **Resource estimate**：descriptor 声明的估算进准入（M5，延续 8.0）。
4. **Run 生命周期事件**：coordinator 通知一律经"锁内排队、锁外 drain"通道（M0/M4），
   UI/服务观察者可安全重入。
