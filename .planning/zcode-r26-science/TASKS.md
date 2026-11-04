# TASKS — R26-science 工作包（RED→GREEN×2）

| WP | 缺陷 | 文件 | 严重度 | 状态 |
|----|------|------|--------|------|
| S1 | percentClipStretch pct≥100 负 pct → size_t 下溢越界读 | processing/algorithms/image_enhancement.cpp + rs_image_enhancement_operator.cpp | P1 | DONE_TESTED (RED→GREEN×2) |
| S2 | maxSamplesPerClass 在 NoData 过滤前截断 → 类别失衡 | analysis/classification/rs_training_data_extraction.cpp | P1 | DONE_TESTED (RED→GREEN×2) |
| S3 | isotonic 重复分数产生重复 knot x：toJson/fromJson 不对称 → sidecar 报废 | analysis/classification/rs_probability_calibration.cpp | P1 | DONE_TESTED (RED→GREEN×2) |
| S4 | Mann-Kendall 并列时标：tau-b 分母 + varS 缺时间并列修正 | processing/algorithms/trend_analysis.cpp | P2 | DONE_TESTED（修订：tau-a 契约保留，仅 varS 补时间并列项，Gilbert 基准不变） |
| S5 | kFold 以 QHash 无序迭代消耗 RNG → 种子契约跨 Qt 版本破坏 | analysis/classification/rs_cross_validation.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| S6 | CV remap 表缺失时原始簇号被当类号计分（伪精度） | analysis/classification/rs_cross_validation.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| S7 | majorityKeyWithTieBreak key=0 平票违反"取小 id" | analysis/segmentation/rs_majority_vote.h | P2 | DONE_TESTED (RED→GREEN×2) |
| S8 | GDALPolygonize 4连通 vs 分割器 8连通 → 对象被拆分 | analysis/segmentation/rs_class_raster.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| S9 | computeLogRatio 非有限/负值 → 静默 0（丢失 NoData 语义） | processing/algorithms/change_detector.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| S10 | changedArea 非米投影 CRS 单位标错（ft² 当 m²） | operators/rs/rs_change_streaming.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| S11 | spatial CV 硬编码 CV_32F，CV_64F 输入抛异常 | analysis/classification/rs_spatial_cross_validation.cpp | P2 | UNREPRODUCED（OpenCV 5 copyTo 就地转换正确；测试保留为契约守卫，无代码改动） |
| S12 | scaler fromJson 不校验 method/version | analysis/classification/rs_feature_scaler.cpp | P2 | DONE_TESTED (RED→GREEN×2) |
| E2E | 生产链路端到端（穿真实注册/算子路径） | tests/ | — | DONE_TESTED（registry→rs:change_detection→GDAL 回读，152 断言） |
| PERF | 性能/资源基线（≥5 次中位数、峰值 RSS、输出一致性） | .planning | — | DONE（3 规格×5 进程，见 PERF_BASELINE.md；无提速主张） |

## 明确不做（已核实 master 已修/跨轨）
- #1538 直方图均衡 ±Inf（master 已修，FIXED_IN_MASTER_BUT_OPEN）
- #1550 JM 上溢（master 已改对数域，FIXED_IN_MASTER_BUT_OPEN）
- #1553 ChunkGraph、#1541 模型 lane（跨轨）
- #1728/#1726/#1727 所有 changed-files
