# PR & ISSUE DEDUPLICATION & REVIEW MATRIX — Direction B

**Date**: 2026-10-13  
**Base Commit**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`  
**Classification States**: `ALREADY_IN_MASTER` | `IN_OPEN_PR` | `STILL_BROKEN` | `NEEDS_REPRO` | `OUT_OF_SCOPE` | `BLOCKED`

---

## 1. 活跃 PR 交叉分析与冲突排查矩阵

| PR / 分支 | 状态 | 文件/领域重叠度 | 结论与动作 |
|---|---|---|---|
| **PR #1475**<br>`hardening/r9-science-data-integrity` | `IN_OPEN_PR` | **中度语义关联**：修改了 `query_planner.cpp`、`data_manager.cpp`、`workspace_catalog.cpp`、`dataset_store_*`、`qgsdatasourceresolver.cpp`、`rs_json_params.cpp` | **SEAM_ISOLATED**：严禁碰触该 PR 修改的任何文件及函数。其遗留的 2 条 P1 评论（`CatalogService.maxItems` 假阴性、`fileExists` 与 direct `GDALOpen` 脱节）在本分支通过底层 `src/geospatial/fabric/catalog_service.cpp` 与 `ResourceUri` / `RasterReader` 独立解耦闭环。 |
| **PR #1476**<br>`hardening/r9-ui-teaching-test-quality` | `IN_OPEN_PR` | **零重叠**：UI / 教学 / Faultlab 领域 (`guided_workflow_widget`, `rs_scan_pool`, `script_adapters`) | **OUT_OF_SCOPE**：无需处理，不产生代码与构建冲突。 |
| **PR #1477**<br>`hardening/r9-agent-execution-reliability` | `IN_OPEN_PR` | **零重叠**：Agent / 运行时 领域 (`ipc_channel`, `external_process`, `task_center`, `python/isolated`) | **OUT_OF_SCOPE**：无需处理，不产生代码与构建冲突。 |
| **PR #1478**<br>`hardening/r10-ui-teaching-help-consistency` | `IN_OPEN_PR` | **零重叠**：桌面 UI / 帮助文档 领域 (`workflow_session_controller`, `pipeline_editor_dock`, `workbenches.json`) | **OUT_OF_SCOPE**：无需处理，不产生代码与构建冲突。 |

---

## 2. 存量合并 PR 吸收情况 (`#1469` ～ `#1474`)

| PR | 提交 SHA | 主题 | 本方向关注项与结论 |
|---|---|---|---|
| **#1469** | `428dcef4ea` | fix/review-p0-p1-wave | `ALREADY_IN_MASTER`：内存泄漏与基本测试稳定性修复，已完全吸收。 |
| **#1470** | `87806e90e0` | hardening/r8-agentops-workflow-closure | `ALREADY_IN_MASTER`：AgentOps 工作流闭环，已完全吸收。 |
| **#1471** | `107e2b17d7` | hardening/r8-runtime-boundaries | `ALREADY_IN_MASTER`：运行时边界收敛，已完全吸收。 |
| **#1472** | `f0e44192a6` | hardening/r8-science-algorithm-closure | `ALREADY_IN_MASTER`：算法层闭环，已完全吸收。 |
| **#1473** | `cec4dc4491` | hardening/r8-data-experiment-integrity | `ALREADY_IN_MASTER`：数据与实验完整性，已完全吸收。 |
| **#1474** | `84d782c408` | hardening/r8-ui-teaching-context | `ALREADY_IN_MASTER`：UI 教学上下文收敛，已完全吸收。 |

---

## 3. 关联 Issue 筛查与 Direction B 采纳清单

| Issue / 缺陷源 | 状态 | 缺陷现象与根本原因 | 采纳 WP |
|---|---|---|---|
| **#1456 (P1 Review)** | `STILL_BROKEN` | `CatalogService::searchPage` 在达到 `query.maxItems` 时中断循环但未将 `page.truncatedByCap` 置为 `true`；`searchAll` 在 local backend 遇到多页且 `maxItems` 到达时进入空页死循环直至 `ResourceExhausted` (100,000次)。 | **WP-B3** (Epoch 1) |
| **#1472 Review / #1467** | `STILL_BROKEN` | `ResourceUri::parse` 对 `file://`、`file://localhost`、UNC `file://host/share` 缺乏原生解析，误判为 `Invalid`；底层 `RasterReader::open` 和 `inspectRaster` 直接喂 `file://` 给 GDAL 会导致 OpenFailed，与预检 `fileExists` 产生裂隙。 | **WP-B1** (Epoch 2) |
| **#1391 / #1174** | `STILL_BROKEN` | `atomic_fs::publishStagedGroup` 和 `publishStagedMembers` 在多成员/带 sidecar 发布失败时的备份恢复顺序；主文件与从属文件的原子发布与崩溃恢复一致性。 | **WP-B6** (Epoch 3) |
| **#1456 (Remote)** | `STILL_BROKEN` | 远程 COG / HTTP 抓取中 IPv4-mapped IPv6 (例如 `::ffff:127.0.0.1`, `::ffff:169.254.169.254`) 绕过私网 SSRF 过滤器的潜在风险；`offline::isEnabled()` 下的零出站强断言。 | **WP-B2** (Epoch 4) |
| **#1450 (Capsule)** | `STILL_BROKEN` | 实验复现包 (`reproduction_bundle`) 导入/导出时的路径遍历 (`../`)、绝对路径逃逸与非可移植环境声明欺骗。 | **WP-B9** (Epoch 5) |
| **#1457 (Tail)** | `STILL_BROKEN` | `dataset_store.cpp` 基础事务与表级操作中 prepare 失败错误传播与 ROLLBACK 安全性。 | **WP-B7** (Epoch 5) |
| **#1449** | `OUT_OF_SCOPE` | `asset_state_provider` 内部无锁并发（涉及 `science_context` 跨模块归属，归 Track C 协调）。 | `OUT_OF_SCOPE` |

---

## 4. 冲突防范总结

1. **绝对禁止**：在当前 worktree 中修改 PR #1475 的 12 个目标文件。
2. **接口解耦**：所有对 `file://` 和目录截断的修复，均在 Direction B 独占的 `src/geospatial/fabric/catalog_service.cpp`、`src/geospatial/util/resource_uri.cpp`、`src/geospatial/raster/raster_reader.cpp` 和 `src/geospatial/metadata/canonical_metadata.cpp` 中闭环，不制造 cherry-pick 冲突。
