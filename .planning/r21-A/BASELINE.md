# BASELINE — r21-A（遥感科学算法轨）

勘查时间：2026-10-24 02:45–03:10 UTC（LIVE）

## 仓库事实基线

- `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（"Merge pull request #1672"），与任务锚点一致；fetch --prune 后无变化 → **这是执行时刻远程事实基线**。
- Open PR = 0（`gh pr list --state open` 返回 `[]`）。
- Open Issues = 126（全量已拉取，编号 1467–1658，中间 1468–1534 已关闭）。
- 本地 `master` 分支（原仓库工作树）= `28e1891390`，落后于 origin/master（缺 R20 五个 PR 合并）；原工作树 clean，未触碰。
- 历史合并链（origin/master 顶 20 条）：#1672(r20-04)→#1669(r20-02)→#1671(r20-01)→#1670(r20-03)→#1668(r20-05)→#1667/#1666/#1665(r19-*)。

## R20 #1671（本轨前作）契约地图 —— merge-loss 防护清单

文件与语义（HEAD=911e0b17 全部实读确认在位）：

| 契约 | 文件 | 防回退测试 |
|---|---|---|
| 趋势时间戳配对（紧凑化值+时间戳同步） | trend_analysis.cpp | tests/test_d16_temporal_trend.cpp |
| viewshed NoData 写 nodataByte | terrain_viewshed.cpp | tests/test_terrain_viewshed.cpp |
| 镶嵌 X 翻转 fail-closed（xDirectionMismatch） | mosaic_plan.cpp | tests/test_mosaic_plan.cpp |
| 分类器标签契约（MLP 单类 classOrder 严格递增、sidecar round-trip） | rs_classifier_mlp.cpp | tests/test_classifier_mlp.cpp |
| ISODATA fit y 校验 fail-closed | rs_classifier_isodata.cpp | tests/test_classifier_isodata.cpp |
| SAR 入射角 (0,90) 域拒绝 + 常量角域 | sar_calibration.cpp | tests/test_sar_kernels.cpp |
| InSAR 相位斜坡中心化坐标（大场景条件数） | sar_insar.cpp | tests/test_sar_insar.cpp |

R20-01 显式记档未修（→ R21-A 候选 backlog）：
1. `fast_6s_lookup.cpp:115-116` 水汽吸收上/下行 0.5 不对称系数（无 6S/MODTRAN oracle，当时定性不了）。
2. `image_fusion.cpp` 内存核：均值样本集与协方差样本集不一致（流式路径正确，内存 seam 仅 tests/panel 调用）。
3. `image_fusion.cpp` 内存核 ±Inf 门只测 `isnan`（流式已洗）。
4. `#1591` RsSegmentMap mutable 缓存无同步（潜在竞态，无活跃跨线程调用方）。

## R20-01 判为"已修未关"issue 的 LIVE 复核结果（HEAD=911e0b17，全部实读源码）

| # | 声称 | 复核结论 | 证据 |
|---|---|---|---|
| 1550 (P1) | JM 对数域 | **确认已修** | rs_jm_separability.cpp:48 `logDeterminant()`、:149 注释引用 #1550、rank-deficient → -1.0 |
| 1538 (P1) | HistEq 拒 ±Inf | **确认已修** | image_enhancement_streaming.cpp:984-989 `!std::isfinite(v)‖v==nodata` 注释引用 #1538 |
| 1590 (P2) | sieve 0 不投票 | **确认已修** | rs_post_process.cpp:122-128 `if (vote == 0) continue` + 注释 |
| 1631-1 (P3) | Fisher-Yates | **确认已修** | rs_cross_validation.cpp:55-59 |
| 1631-2 (P3) | KMeans/ISODATA y 守卫 | **确认已修** | rs_classifier_kmeans.cpp:47+ ADR 0061 注释块 |
| 1631-3 (P3) | feature_scaler fromJson fail-closed | **确认已修** | rs_feature_scaler.cpp:166-172 |
| 1646-6 (P3) | nodata_utils 统一判据 | **确认已修** | consumers 覆盖 resampler/spectral_anomaly/spectral_local_rx/segment_features/multires_segmenter/sar_calibration 等（grep 实读） |
| 1544 (P1) | DatasetFileGuard RAII | 采信 R20-01 复核（本轮抽样验证见 ISSUE_TRIAGE） | — |

以上 7 项本轮**不重复修**，但全部跑既有回归两遍绿（O-A1）作为 merge-loss 防护。

## 构建基线

- worktree：`/home/kevin/project/exp-rs-r21-A-20261023T184835Z`，分支 `agent/r21-a-scientific-algorithms-20261023T184835Z`
- 配置：Debug / ENABLE_TESTS=ON / vendored prefix（GDAL/PROJ/GEOS/GSL/NETCDF/CURL 来自 `paleo-singlefactor-closure/vendor/prefix`）+ `/usr/lib/cmake/opencv5`
- 并行纪律：`flock -x /tmp/exp-rs-r21-build.lock` + `-j2`；机器 16 核 62GB，勘查时可用 46G，负载 ~3.8
- ASan/UBSan preset 存在（sanitizer-debug）→ 视需要局部使用；Tsan/Win/mac 不可用（记录约束）
