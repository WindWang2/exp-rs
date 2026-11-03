# RISK_REGISTER — R27-T2

| 风险 | 等级 | 缓解 |
|---|---|---|
| D-1 glob 外文件改动被判越界 | 中 | 最小聚焦 diff；FILE_OWNERSHIP 单列；PR 说明；必要时回退方案已定义 |
| detach 卡住 worker 后仍引用单例状态（UB 窗口） | 高 | immortal 单例（进程寿命）；worker 退出路径只访问引擎成员，对象永不析构；R1 重点评审项 |
| flock/LockFileEx 跨平台差异（NFS/SMB 无 flock 语义） | 中 | 锁文件位于 scratchRoot/statePath（本地盘场景）；open 失败 → typed 运行时错误（fail-closed），不静默降级；Windows 分支 LockFileEx FAIL_IMMEDIATELY |
| RIO-10 语义变化破坏既有消费方（TaskCenter 依赖 Cancelled） | 中 | 先跑全部 job_engine/task_center 既有测试；晚到取消仅当 executor 已成功完成时改报 Succeeded，纯取消路径不变 |
| RIO-12 OSRSetFromUserInputEx 需较新 PROJ | 中 | 编译期探测（PROJ_VERSION_MAJOR/MINOR），旧版回退为输入白名单拒绝危险拼写（open(、http、/etc 等）并记录 |
| 测试与兄弟轨在 tests/CMakeLists.txt 冲突 | 中 | append-only 唯一标识块；PR 前三方比对；冲突只重放自己段 |
| 并发测试不稳定（竞态flake） | 中 | 用确定性同步（barrier/条件变量），禁止 sleep 依赖；CI 无需等待 |
| 构建资源（-j 上限、内存） | 中 | CMAKE_BUILD_PARALLEL_LEVEL=2；宿主互斥锁 flock；RAM<30% 降 -j1 |

# FILE_OWNERSHIP — R27-t02

**白名单内（独占写）**：
- `src/runtime/chunk/chunk_graph.{h,cpp}`、`src/runtime/chunk/resumable_tile_run.{h,cpp}`、`src/runtime/chunk/exclusive_path_lock.{h,cpp}`（新）
- `src/runtime/worker/worker_lease.{h,cpp}`
- `src/geospatial/remote/safe_remote_open.{h,cpp}`（新，或并入既有 remote API 文件）
- `src/geospatial/vector/vector_reader.{h,cpp}`、`src/geospatial/crs/crs_policy.{h,cpp}`、`src/geospatial/fabric/mirror.{h,cpp}`、`src/geospatial/fabric/query_planner.{h,cpp}`
- `src/data/providers/remote_source_cache.{h,cpp}`、`src/data/governance/governance_store.{h,cpp}`、`src/data/import_session.{h,cpp}`（新）、`src/data/CMakeLists.txt`（注册新文件）
- `tests/test_r27_t02_*.cpp`（新）+ `tests/CMakeLists.txt`（append-only 块）
- `.gitignore`（末尾追加段）

**D-1 授权最小修改（glob 外、里程碑点名）**：
- `src/processing/framework/local_worker_pool.cpp`（仅租约释放行）
- `src/jobs/job_engine.{h,cpp}`（仅 shutdown 有界化/超时/RIO-9/10 相关）

**绝不触碰**：`src/app/**`（#1765 GUI 归 T4）、`src/operators/rs/**`、`src/agent/**`、`src/python/scripts/**`、其他兄弟轨文件。
