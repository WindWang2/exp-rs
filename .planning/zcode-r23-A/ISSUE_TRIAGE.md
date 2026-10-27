# r23-A ISSUE_TRIAGE — 逐条状态(基于 master@ad98a7a704 源码核验,2026-10-26/27)

方法:对 A 轨权属范围内(open issue 中与科学算法相关的全部条目)逐条读最新源码验证。
状态枚举: LIVE / FIXED_OPEN / DUPLICATE / STALE / IN_PROGRESS / OUT_OF_SCOPE / UNVERIFIABLE

## A 轨范围(白名单内)核验结果

| Issue | 标题摘要 | 状态 | 证据(源码级) |
|---|---|---|---|
| **#1687-1** | math_utils bothNormalizable 硬编码 ≤-999 NoData 阈值 + `a==b&&a<0`→NaN | **LIVE** | math_utils.cpp:182-188:三条规则仍在;合法同值负反射率对(a=b=-0.01)被误判 NaN;魔数阈值与"声明 NoData 为权威"的架构(readTileBand/readBandData/isNoData 预转 NaN)矛盾 |
| #1687-2 | computeStatsFromAccumulators validCount 语义未文档化 | LIVE(仅文档,弱) | math_utils.cpp:150-152 `stats.validCount = acc.count`;#1712 已加精度注释,validCount 语义注释仍缺 |
| #1589 | science_context 失效链未接线 | FIXED_OPEN | data_platform_tools.cpp:1436-1462 syncWorkspaceCatalogInvalidation(notifyProjectSwitch + generation);asset_state_provider 缓存由 setCatalogGeneration 清除 |
| #1549 | preflight allowAssumed 共享缓存空操作 | FIXED_OPEN | asset_state_provider.cpp:236-261:严格性作为出站投影,不烘焙进缓存 |
| #1649 | recipe_router 空串 find 假匹配 | FIXED_OPEN | recipe_router.cpp:283-298 kw.empty()/title.empty() 守卫 |
| #1631-1 | rs_cross_validation std::shuffle | FIXED_OPEN | rs_cross_validation.cpp:58-68 手写 Fisher-Yates(#1685 纪律) |
| #1631-2 | kmeans/ISODATA y 未校验 | FIXED_OPEN | rs_classifier_kmeans.cpp:52-60 y.type/cols/rows 校验(ISODATA 同构,#1671 F-05) |
| #1631-3 | feature_scaler fromJson min/max 长度 | FIXED_OPEN | rs_feature_scaler.cpp:166-170 四数组等长 fail-closed |
| #1646-6 | nodata_utils 唯一所有者名不副实 | FIXED_OPEN | resampler.cpp:16-22 / spectral_local_rx.cpp:26-31 / spectral_anomaly.cpp 均委托 sicnu::rs::isNoDataValue |
| #1550 | JM 可分性上溢报 0.0 | FIXED_OPEN | rs_jm_separability.cpp:150-185 溢出安全式 + 非可计算报 -1.0 |
| #1538 | HistogramEqualize 未拒 ±Inf | FIXED_OPEN | image_enhancement_streaming.cpp 多处 isfinite 门 |
| #1590 | sieve 边界投票计入 NoData 标签 0 | FIXED_OPEN | rs_parent_link.cpp:46-49 `c==0 continue`;majorityKeyWithTieBreak 契约 |
| #1591 | RsSegmentMap mutable 缓存竞态 | FIXED_OPEN | rs_segment_map.h:108-114 QReadWriteLock + int64 计数(#1676 第 17 项) |

## A 轨范围外(登记,不修)

| Issue | 状态 | 原因 |
|---|---|---|
| #1646-1..5 | OUT_OF_SCOPE | 文件在 src/processing/framework|gdal|根(白名单仅 src/processing/algorithms/**);归 B 轨/framework owner |
| #1642-1..4 | OUT_OF_SCOPE | src/operators/framework|io|runtime(B 轨);runtime 显式只读 |
| #1588 | OUT_OF_SCOPE | src/preflight 不在白名单 |
| #1575-1578 | OUT_OF_SCOPE | src/operators/runtime(只读禁区) |
| 其余 100+ agent/ui/infra/experiment/data 等 issue | OUT_OF_SCOPE | 与 A 轨科学算法权属无关 |
| #1491/#1467 | OUT_OF_SCOPE | CI/core-fork 遗留,非本轮科学域 |
