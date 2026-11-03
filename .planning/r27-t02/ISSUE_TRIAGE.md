# ISSUE_TRIAGE — R27-T2

实时核对于 2026-11-02T13:23Z（gh issue view 逐条确认 state=OPEN；代码证据见 BASELINE.md）。

| Issue | 分流 | 依据 | 本轨动作 |
|---|---|---|---|
| #1743 (P1, RIO-1) | 本轨修复 | `src/runtime/chunk/chunk_graph.cpp` 在白名单；HEAD 未修 | A：重复输入 fail-closed（typed logic_error）+ 空 join 运行时拒绝（连带 RIO-3）+ 反例矩阵测试 |
| #1764 (P2, RIO-2) | 本轨修复（双文件） | `worker_lease.*` 在 `src/runtime/**` 白名单；`local_worker_pool.cpp` 在 `src/processing/**`——白名单 glob 未列，但目标文件里程碑 B 点名、Issue 归属 runtime-data-io 分轨、显式排除清单（app/operators-rs/agent）不含 processing。**决策 D-1：按里程碑 B 授权最小修改**（见 DECISIONS.md） | teardownWorker/retireWorker 释放时 `m_leases.reset()`；tracker 增加可观测 `trackedCount()`；测试断言固定并发下有界 |
| #1765 (P2, RIO-4) | 拆分：T2 后端 + T4 GUI | 目标文件条款：dialog 专归 T4；`src/geospatial/stac/**`、remote/range-cache 安全打开接口归 T2 | C：提供稳定后端 API `safeRemoteRasterPath`（scheme/offline/SSRF/redirect 守卫 + range-cache 路径 + typed 结果），含回归契约测试；GUI 改线由 T4 完成，本轨不动 `src/app/**` |
| #1766 (P2, RIO-5) | 本轨修复 | `src/runtime/chunk/resumable_tile_run.cpp` 白名单内 | OS 级独占锁（POSIX flock / Windows LockFileEx，Qt-free），第二执行者 typed fail-closed；崩溃自动释放 → 确定性恢复测试（子进程真实崩溃） |
| #1767 (P2, RIO-8) | 本轨修复 | `src/jobs/job_engine.cpp` 不在 glob，但里程碑 B 点名 + Issue 归属本轨（同 D-1 决策） | 有界 shutdown（deadline + detach 卡住 worker + immortal 单例使 detach 安全）；可选 per-job wall-clock 超时触发 cancel hooks |
| #1803 (P3) | 逐项分流 | 见下表 | 白名单内项全部修复；RIO-9/RIO-10 在 job_engine（D-1 授权内） |
| #1804/#1805/#1806 | 非本轨 | workflow-experiment / app-ui / sdk-python-plugins 分属其他轨 | 不动 |

#1803 逐项：

| 项 | 文件 | 白名单 | 动作 |
|---|---|---|---|
| RIO-3 空 join assert | chunk_graph.cpp:77 | ✅ | 随 #1743 一并改运行时拒绝 |
| RIO-6 acquire 无出口策略 | src/data/providers/remote_source_cache.cpp | ✅ | 经 C 的安全路径打开（range-cache guarded transport） |
| RIO-7 OGR SQL WHERE 拼接 | src/geospatial/vector/vector_reader.cpp | ✅ | 参数化/转义 + 拒绝危险字面量 |
| RIO-9 终态后进度可变更 | src/jobs/job_engine.cpp | D-1 | progress 回调补 isTerminalState 门（对齐 #1693 log 门） |
| RIO-10 晚到取消误报 | src/jobs/job_engine.cpp | D-1 | finishSuccess 报 Succeeded + "取消来晚"日志 |
| RIO-11 审计日志无 retention | src/data/governance/governance_store.cpp | ✅ | 有界保留（环形/按条数截断），去双写 |
| RIO-12 CRS 解析可触网/读文件 | src/geospatial/crs/crs_policy.cpp | ✅ | OSRSetFromUserInputEx + ALLOW_NETWORK/FILE_ACCESS=NO（注意旧 GDAL 无 Ex 的编译分支） |
| RIO-13 mirror v1 降级跳过哈希 | src/geospatial/fabric/mirror.cpp | ✅ | 带 v2 键的 manifest 拒绝按 v1 解释 |
| RIO-14 重复页守卫过松 | src/geospatial/fabric/query_planner.cpp | ✅ | href/offset 重复检测 + 降低上限 |

**FIXED_IN_MASTER**：无（六条候选全部 OPEN 且 HEAD 代码证据成立）。
