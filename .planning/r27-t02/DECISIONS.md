# DECISIONS — R27-T2（无人值守模式，按 AGENTS.md 把选项与默认决策记于此）

- **D-1 白名单 glob 外的两个里程碑点名文件**（`src/processing/framework/local_worker_pool.*`、`src/jobs/job_engine.*`）
  - 冲突：白名单 glob 列表（runtime/geospatial/dataset/data/operators-runtime/operators-io/preflight）不含这两个路径；但里程碑 B 明确点名 "#1764 local_worker_pool/worker_lease" 与 "#1767 shutdown 无超时 join"，且 #1764/#1767/#1803(RIO-9/10) 都归属 runtime-data-io 分轨（本轨），显式排除清单（src/app/**、src/operators/rs/**、src/agent/**）也不含它们。
  - 默认决策：按里程碑 B 授权做**最小聚焦修改**（只动租约释放与 shutdown 有界化相关行），FILE_OWNERSHIP.md 单列，PR 描述明示。若 R1/R2 评审认为越界，回退为仅白名单内部分（worker_lease 侧 API + job_engine 不可达时由 T5 接手）并留待办。
- **D-2 #1766 锁机制：OS 级建议锁（flock/LockFileEx），非 QLockFile、非 PID 文件**
  - `sicnu_runtime` CMake 契约是 Qt-free（no Qt, no GDAL），QLockFile（QtCore）违反库边界。
  - PID 文件在崩溃后变 stale，需要启发式清理；OS 锁在进程死亡时由内核自动释放 → 直接满足"崩溃后确定性恢复"Oracle。追加尝试获取 → typed 失败（fail-closed），符合"另一个 fail-closed 或排队"（选 fail-closed：本轨不引入排队调度器，YAGNI）。
- **D-3 #1767 有界 shutdown + immortal 单例**
  - std::thread 无 timed join；正确做法是 deadline 等待 worker 退出（完成计数 + CV），超时后 detach 卡住的 worker。detach 安全的前提是对象寿命覆盖 worker —— `JobEngine` 全部使用点（生产 + 测试）都是 `instance()` 单例，将其改为 never-deleted 堆对象（1 行）即可让 detach 永远安全（进程退出回收）。不改全库为 shared_ptr（过度重构，违反 Surgical Changes）。
  - per-job wall-clock 超时按 Issue 建议做成**可选**（JobRequest 增字段，默认关），由既有 watchdog/取消钩子机制触发，不新增常驻线程。
- **D-4 #1765 后端 API 形态：新入口 `safeRemoteRasterPath()`（src/geospatial/remote/），返回 range-cache 受控路径或 typed 拒绝**
  - 复用 #1456/#1545 已有构件（redirectGuard 传输、egress 策略、offline gate、RemoteRangeCache::cachedPath），只提供"一个可调用的稳定入口 + 回归契约"，不重写传输层；GUI（T4）与 RemoteDatasetPool（RIO-6）都改为消费它。
- **D-5 D 里程碑四状态导入会话：新建 `src/data/import_session.*`（白名单内），复用 ImportPreflight + DataManager**
  - 不改 `src/processing/framework/collection_import_service.*`（glob 外且非里程碑点名）；会话状态 Checking/Importing/Completed/Cancelled(+Failed)，journal 经 platform::sidecar 原子落盘，resume 用既有 dedup 查找幂等续作，产出机器可读操作摘要（schema 版本化）。
- **D-6 测试注册**：新增 Catch2 测试全部并入一个 `tests/test_r27_t02_*.cpp` 家族，`tests/CMakeLists.txt` 末尾 append-only 块注册。
