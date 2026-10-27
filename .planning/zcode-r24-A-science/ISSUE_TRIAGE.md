# R24-A Issue Triage（候选 → 判定 + 证据）

判定值：OPEN_LIVE（master 上仍存活，本轨修）/ FIXED_IN_MASTER（已修未关）/ COVERED_BY_PR / BLOCKED / OUT_OF_SCOPE（他轨）。
证据全部为对 `origin/master@5fe642ec6` 的实读（file:line）或 PR 正文。

## 本轨科学域候选

| Issue | 判定 | 证据 |
|---|---|---|
| **#1687** [R17-science][P3] math_utils `<=-999` 魔数 + 负反射率对判定；computeStatsFromAccumulators validCount 语义 | **OPEN_LIVE（第一项）** | `src/processing/algorithms/math_utils.cpp:180-189` bothNormalizable 仍有 `a <= -999.0f \|\| b <= -999.0f` 与 `a == b && a < 0.0f` 拒绝；流入 MathUtils::normalizedDifference → spectral_indices.cpp 7+ 处、change_detection.cpp（dNBR）、rs_spectral_index_operator.cpp（约 15 处）、rs_temporal_index_operator.cpp:97。第二项（validCount 文档）半存活：#1712 已加精度注释（:156-163），但 `stats.validCount = acc.count`（:146）与 computeStats 的"排除非有限"语义差异未文档化 → 随修复一并补契约注释。 |
| **#1646-6** nodata_utils 唯一所有权被违反（resampler/spectral_anomaly/spectral_local_rx 各自重实现） | **FIXED_IN_MASTER** | resampler.cpp:13-20 委托 `sicnu::rs::isNoDataValue`；spectral_local_rx.cpp:22-33 pixelValid 注释明示 resolve through nodata_utils.h；spectral_anomaly.cpp 共享谓词。#1712/#1676 期间修复。 |
| **#1646-5** algorithm_help_catalog 14 死键 | **FIXED_IN_MASTER（键名）**；**覆盖缺口存活** | `src/processing/algorithm_help_catalog.cpp` 键已改为真实注册名（`centroids`:32、`hillshade`:38、`cliprasterbyextent`:34、`gdaldem`:25、`gdalinfo`:26、`native_clip`:33、`native_intersection`:39、`native_union`:43…）。但 issue 第二半（"仓库没有测试校验键与注册表一致性，表会静默腐烂"）仍真：tests/ 只有 test_algorithm_meta_drift.cpp 校验 sidecar。→ WP-A7 补键↔注册一致性 drift 测试。 |
| **#1646-1** GDALAllRegister 绕过 ensureGdalInit（preflight/agent_workflow_executor） | OUT_OF_SCOPE（src/processing/framework 归 B/框架域）→ CROSS_TRACK | framework/algorithm_preflight.cpp、agent_workflow_executor.cpp 不在本轨 OWNED 列表 |
| **#1646-2** local_worker_pool 无锁读 | OUT_OF_SCOPE（framework）→ CROSS_TRACK | 同上 |
| **#1646-3** GdalDatasetWrapper move 丢 lastError | OUT_OF_SCOPE（src/processing/gdal/）→ CROSS_TRACK | 诊断价值高但非科学算法域；记录移交 |
| **#1646-4** json_params_converter 重复前向声明 | OUT_OF_SCOPE（framework 头文件卫生）→ CROSS_TRACK | 纯重复代码，非缺陷 |
| **#1631-1** rs_cross_validation std::shuffle 确定性 | **FIXED_IN_MASTER** | rs_cross_validation.cpp:55-66 手写 Fisher-Yates（注释引 RsSpatialCrossValidation 同纪律） |
| **#1631-2** kmeans/ISODATA y.at<int> 无类型校验 | **FIXED_IN_MASTER** | rs_classifier_kmeans.cpp:49-58 y.type()==CV_32S、cols==1、rows==X.rows 校验 fail-closed |
| **#1631-3** feature_scaler fromJson min/max 长度 | **FIXED_IN_MASTER** | rs_feature_scaler.cpp:160-168 四数组等长校验 return false |
| **#1590** sieve 边界多数投票计 NoData 标签 0 | **FIXED_IN_MASTER** | classification_postprocess.cpp:163 `kNoData=-1` 哨兵；:182-183 NoData 组件为 mask feature 永不消除；:193 投票跳过 NoData |
| **#1591** RsSegmentMap mutable 缓存竞态 | **FIXED_IN_MASTER** | #1676 工作包 17（QReadWriteLock 双检）；本基线 r21-A 正文确认 |
| **#1550** JM 可分性上溢报 0.0 | **FIXED_IN_MASTER** | rs_jm_separability.cpp:23-55 logDeterminant 用 SVD 奇异值对数和，非有限/非满秩 return false（调用方报 non-computable） |
| **#1538** HistogramEqualize 未拒 ±Inf | **FIXED_IN_MASTER（显示域）** | 归 #1534/#1663 Inf 治理 + r18-a；当前 HistogramEqualize 实现在 src/app/display（C 轨域），无本轨动作 |
| **#1589** science_context invalidateAsset 未接线 | OUT_OF_SCOPE（src/science_context 归 C/agent 面）→ CROSS_TRACK | — |
| **#1593** lab_pack Json 解析 | OUT_OF_SCOPE（teaching）→ CROSS_TRACK | — |
| **#1707/#1684/#1685/#1686** | FIXED_IN_MASTER | PR #1712（已合并） |
| **#1706** 多类别羽化拼接 | FIXED_IN_MASTER | PR #1709 |
| **#1704/#1690/#1691/#1692/#1705/#1693** | FIXED_IN_MASTER | PR #1714 |
| **#1694/#1689/#1678/#1688** | FIXED_IN_MASTER | PR #1711 |
| **#1697/#1698/#1699/#1681/#1682** | FIXED_IN_MASTER | PR #1708（+ r23-E 补证） |

## 结论

纯"已修未关"重做 = 0。本轨从 #1687（OPEN_LIVE 生产路径缺陷）+ #1646-5 覆盖缺口 + 家族审计（3 个 Explore 代理进行中）导出工作包。
