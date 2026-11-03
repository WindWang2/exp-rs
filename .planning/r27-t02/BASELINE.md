# BASELINE — R27-T2 数据资产、遥感 I/O 与分块/模型执行运行时：可靠性和可恢复性

- **BASE_SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa` (= `origin/master`, 2026-11-02T13:23:14Z UTC fetch)
- **Worktree**: `../exp-rs-r27-t02-20261102T132440Z`，分支 `agent/r27-t02-data-io-runtime-reliability-20261102T132440Z`
- **基线与任务文件初始参考一致**；本地 main 仓 HEAD 落后于 origin/master，未用作基线。

## 侦察结论（代码证据）

| Issue | 状态 | 证据（HEAD `10652fd003` 实读） |
|---|---|---|
| #1743 RIO-1 (P1) | OPEN，HEAD 可复现 | `src/runtime/chunk/chunk_graph.cpp:75-90`：`requireFreeConsumerLocked` 只查**已注册**节点输入与 `m_sinkInput`；`addJoin` 的新节点在循环后 push，同一实参内的重复 id 不可见。`runJoin`(:216-221) 对每个声明输入各 pop 一次 → `{A,A}` 每轮从同一队列 pop 两次（错位配对 / 奇偶计数时 `ChunkPartitionMismatch`）。既有测试只覆盖 addSink/addStage 二消费者拒绝，无 join 内重复。 |
| #1764 RIO-2 (P2) | OPEN，HEAD 可复现 | `src/processing/framework/local_worker_pool.cpp:226-234` 每次 spawn mint 新 `worker-N` 并 `m_leases.onLiveness` 插入 map；`teardownWorker`(:134)/`retireWorker`(:299) 只杀进程减计数，从不调 `WorkerLeaseTracker::reset`（`src/runtime/worker/worker_lease.cpp:120-124` 是唯一 erase 路径，全仓无生产调用）。 |
| #1765 RIO-4 (P2) | OPEN，HEAD 可复现 | `src/app/dialogs/stac_browser_dialog.cpp:216-221` 直接 `GDALOpenEx("/vsicurl/...")`，绕过 `src/geospatial/remote/` 的 #1456/#1545 redirect-guard/DNS-pin/egress 控制与 offline gate。`src/data/providers/gdal_raster_source_provider.cpp:217`（normalizeRemoteRasterSource）与 `remote_source_cache.cpp:171`（acquire）同样落 raw `/vsicurl/`（后者即 RIO-6）。 |
| #1766 RIO-5 (P2) | OPEN，HEAD 可复现 | `src/runtime/chunk/resumable_tile_run.cpp` `execute()` 全程无 statePath/runDir 独占锁：并发双 run 会交错 append journal、重复 consume、互相 wipeState。`sicnu_runtime` 是 Qt-free 库（CMakeLists 明示 no Qt），不能引 QLockFile。 |
| #1767 RIO-8 (P2) | OPEN，HEAD 可复现 | `src/jobs/job_engine.cpp:175-178` `shutdown()` 无界 `t.join()`；取消纯协作式（仅置 flag + onCancel hook），卡死的 operator（GDAL/网络读）→ shutdown 永久挂起。`shutdownForTests`(:547) 同样无界。`instance()` 是函数局部 static（进程退出析构）。 |
| #1803 (P3 汇总) | OPEN | 12 项：RIO-3 空 join 仅 assert；RIO-6 acquire 无出口策略；RIO-7 `vector_reader.cpp` OGR SQL WHERE 拼接；RIO-9 `job_engine.cpp:~1006` 终态后进度仍可变更（log 回调有 #1693 门，progress 回调没有）；RIO-10 `job_engine.cpp:~1130` 成功后才到的取消报 Cancelled；RIO-11 `governance_store.cpp:516-519` 审计日志无 retention；RIO-12 `crs_policy.cpp:137,175` `OSRSetFromUserInput` 可读文件/URL；RIO-13 `mirror.cpp:487-497` 删 index 即降级 v1 跳过哈希；RIO-14 `query_planner.cpp:340-345` 重复页守卫上限 10 万次请求。 |

## 近期已合并、不得重复实现

- **R25-B (#1726)**：`src/data/import_preflight.{h,cpp}`（只读导入预检解释报告 schema sicnu.import.preflight/1）、`src/operators/runtime/inference_plan`、`experiment/replay_deviation`。D 里程碑只能在其上深化。
- **R24-b (#1719)**：tile 尾批丢弃修复（detection_tile_engine）、dataset 事务门、#1581 lineage 重挂。
- **R26 (#1729-1731)**：harness/workbench/science 轨，与本轨路径无交叠。
- **R22-data (#1710)**：SSRF authority 解析、远程 COG 磁盘缓存几何键、原子发布崩溃一致性 —— #1765 的安全后端构件已在，缺的是**统一受控打开入口**（本轨交付）。

## 兄弟轨 worktree（同刻启动，冲突矩阵见 PR_COLLISION_MATRIX）

- `exp-rs-r27-t01-20261102T132443Z`（science-numeric-quality）
- `exp-rs-r27-t03-20261102T132444Z`（agent-security-sdk-plugins）
