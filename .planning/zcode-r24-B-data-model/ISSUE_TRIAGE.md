# R24-B ISSUE TRIAGE — 开放 Issue vs master 5fe642ec6 实码验证

方法：4 个只读侦察代理在 worktree HEAD 上逐条读源码、引 fix commit（`git merge-base --is-ancestor` 确认在 HEAD 祖先链）。结论分五类：OPEN_LIVE / FIXED_IN_MASTER / FIXED_OPEN（已修但 Issue 仍开）/ COVERED_BY_PR / OUT_OF_SCOPE。

## 所有权区域候选（23 项）— 全部 FIXED_IN_MASTER

| Issue | 区域 | 判定 | 证据（file:line @ 5fe642ec6） |
|---|---|---|---|
| #1545 P1 range cache 探针不传 SSRF 标志 | geospatial/remote | FIXED_IN_MASTER | `range_cache.cpp:282-296` validatorOptions() 传播 blockPrivateNetworks 到 probe/revalidate/Stat；commit `192c524f7`+`987161c25`；回归 `test_io_range_cache.cpp:1952` |
| #1579 '%' 相对引用误拒 | geospatial/util | FIXED_IN_MASTER | `resource_uri.cpp:965-977` 仅拒 NUL/`..`；percentDecode 保持字面 '%'. 残余：无正例回归（见 GAP-G1） |
| #1541 P1 检测 lane CV_CN_MAX | operators/runtime | FIXED_IN_MASTER | `detection_tile_engine.cpp:324-336` typed refusal（commit `9b42bc93f`） |
| #1542 P1 cv::Mat↔ndarray 悬垂 | operators/python | FIXED_IN_MASTER | `opencv_type_caster.h:127-165` borrowOwningArray 拒绝越界视图 |
| #1544 P1 裸 GDALDatasetH | operators/rs | FIXED_IN_MASTER | `rs_continuum_removal_operator.cpp:29-49` DatasetFileGuard RAII；共享 helper `src/geospatial/gdal_guard.h:39-60` |
| #1553 P1 ChunkGraph::addSink 守卫 | runtime/chunk | FIXED_IN_MASTER | `chunk_graph.cpp:92-104` requireFreeConsumerLocked（commit `d9f1b886f`） |
| #1554 P1 ModelSessionPool 析构锁 | runtime/gpu | FIXED_IN_MASTER | `gpu_plane.cpp:43-62` lock_guard 作用域先于 delete |
| #1573 P2 effectiveBatchSize 忽略 halo | operators/runtime | FIXED_IN_MASTER | `tile_inference_engine.cpp:4265-4283` fedW/fedH=tile+2*halo 与 to_input 取 max |
| #1574 P2 OpenVINO ProviderTraits{} | operators/runtime | FIXED_IN_MASTER(设计如此) | `openvino_provider.cpp:259-271` 文档化；selectDevice 修复功能性 |
| #1575 P2 provider artifactPath | operators/runtime | FIXED_IN_MASTER | `openvino_provider.cpp:47-58,107`；`tensorrt_provider.cpp:84-86,148` |
| #1583 P2 auditFolds foldCount 基数 | dataset | FIXED_IN_MASTER | `fold_audit.cpp:79-96` 从 assignments 推导基数（commit `c8ba51990`） |
| #1584 P2 overlapping_patch 只查 left | dataset | FIXED_IN_MASTER | `leakage_audit.cpp:601-609` 双侧窗口证据 |
| #1585 P2 pre_post_pair 单向去重 | dataset | FIXED_IN_MASTER | `leakage_audit.cpp:429-437` 仅对称对跳过（`a27f8601b` 跟进） |
| #1586 P2 TemporalFactsProvider 只解析首个 | preflight | FIXED_IN_MASTER | `runtime_adapter.cpp:124-184` 非选中集合计数入 temporalTruncated，不再静默 |
| #1548 P1 allowAssumed 共享缓存空操作 | science_context | FIXED_IN_MASTER | `asset_state_provider.cpp:234-261` 严格性作为出栈投影，不入缓存 |
| #1587 P2 mirrorAllAssets (void) 丢弃 | data | FIXED_IN_MASTER | `workspace_service.cpp:222-337` 计数+audit("workspace.mirror_failed")。残余：`:314-317` removeAsset 仍 (void)（GAP-G3 低危） |
| #1546 P1 repeat_execution 变量遮蔽 | experiment | FIXED_IN_MASTER | `repeat_execution.cpp:201,225,252-263` 单一声明，拒绝分支复活（commit `831f8d600`） |
| #1547 P1 非身份维度误判 deviated | experiment | FIXED_IN_MASTER | `replay_deviation.cpp:119-134,159-167` identity-only 门 |
| #1580 P2 bundle 导入 run_id 未校验 | experiment | FIXED_IN_MASTER | `reproduction_bundle_import.cpp:342-378` RunId 权威校验+拒而不洗 |
| #1581 P2 lineage 切片 provenance.json | experiment | FIXED_OPEN(残余缺口) | `reproduction_bundle.cpp:418-435` 仍只存远端点/丢方向；importer `:466-506` 只警告不重挂 → **GAP-G2（本轨主攻）** |
| #1582 P2 分类评测静默跳行 | experiment | FIXED_IN_MASTER | `benchmark_runner.cpp:367-460` coverage 账目入 raw_metrics |
| #1603 P2 throttleWait 无界 | runtime/exec | FIXED_IN_MASTER | `execution_governor.cpp:42-50,581-620` 30s deadline+迭代上限 |
| #1604 P2 FileTraceSink 每条重建 ofstream | runtime/observability | FIXED_IN_MASTER | `trace.cpp:261-272` thread_local ofstream 复用 |
| #1605 P2 事件环 erase 四分之一 | runtime/observability | FIXED_IN_MASTER | `execution_telemetry.cpp:64-117` 环形游标覆写。残余：`execution_telemetry.h:144` 注释陈旧（GAP-G4 微） |

R22 PR #1710 四项（SSRF authority 解析 `offline_gate.cpp:519-536`、RangeDiskBlockStore 几何键 `range_cache_disk.cpp:298-314`、原子发布 `range_cache_disk.cpp:404-452`+`atomic_fs.cpp`、dataset provenance `dataset_store.h:95-100`/`sample_promotion.cpp`）与 #1709 多类别羽化（`tile_inference_engine.cpp` per-slot 缓冲）均确认在位。

## 非本轨区域（OUT_OF_SCOPE，供 CROSS_TRACK_FINDINGS）

- #1690/#1691/#1692/#1704/#1705/#1693 → PR #1714 已修（agent 轨）；#1694/#1678/#1688/#1689 → #1711；#1697/#1698/#1699/#1681/#1682/#1683 → #1708；#1706 → #1709；#1707 → #1712；#1591 → #1716 有 typed 回归。全部标 FIXED_OPEN（Issue 状态 ≠ 缺陷存活）。
- #1543（python worker 写超时）等 operators 家族项经 `9b42bc93f` 扫尾。

## 残余缺口清单（本轨工作输入）

- **GAP-G1**（WP-B1）: `resource_uri.cpp` 字面 '%' 文件名解析无正例回归 — 测试空缺。
- **GAP-G2**（WP-B7/B8，主攻）: reproduction bundle lineage 切片丢边方向 → 导入 run 成谱系孤岛；exporter 应输出 from/to 节点对，importer 应在现有 DatasetStore/ExperimentStore 事务内重挂（带环检测/预算），保持向后兼容（旧 bundle 无 run_edges 时维持现有警告路径）。
- **GAP-G3**（低危）: `workspace_service.cpp:314` ghost-reconcile removeAsset 结果丢弃。
- **GAP-G4**（微）: `execution_telemetry.h:144` 陈旧注释。
- 更多的 LIVE 缺陷通过第二波对抗审查热点文件获得（见 TASKS.md WP-DX）。
