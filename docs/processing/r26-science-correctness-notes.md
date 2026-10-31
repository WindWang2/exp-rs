# R26-science correctness notes（2026-10-31）

本文件汇总 R26-science 轨修复的科学契约，供算法复现与教学实验引用。
每条契约均有 tests/test_r26_science_oracles.cpp 的独立参考 oracle 钉死
（RED→GREEN×2，证据：.planning/zcode-r26-science/TEST_RESULTS.md）。

## 增强与拉伸（processing/algorithms/image_enhancement.cpp）
- `percentClipStretch` 的两侧裁剪百分比钳制到 [0,50]：pct ≥ 100 或负值
  一律退化为全范围 [min,max] 拉伸（与流式 PercentClipSelector 的回退一致）。
  修复前 pct ≥ 100 使 `hi` size_t 下溢、越界读（Debug STL 断言崩溃）。

## 训练样本提取（analysis/classification/rs_training_data_extraction.cpp）
- `maxSamplesPerClass` 对**过滤后保留**的有效样本计数（头文件契约）：
  NoData/ignore 像素不再消耗配额。选择顺序 = 同 seed 便携 Fisher-Yates
  洗牌后每类前 N 个有效像素；无 NoData 时与历史行为逐位一致。

## 概率校准（analysis/classification/rs_probability_calibration.cpp）
- isotonic（PAV）对重复分数的相邻等均值块合并为单一 knot（加权均值），
  保证 knot x 严格递增、toJson/fromJson 对称可往返；sidecar 不再出现
  "可写不可读"导致整个分类工程文件报废的形态。

## 时序趋势（processing/algorithms/trend_analysis.cpp）
- Mann-Kendall var(S) 同时扣除 y 并列与时间并列项（Gilbert 16.5 形状，
  双轴）；采集时刻唯一时与 Gilbert 基准逐位一致（124 精确值保留）。
- τ 保持 τ-a（头文件契约与 Gilbert 基准钉死），并列时刻天然限制 |τ|<1。

## 交叉验证（analysis/classification/rs_cross_validation.cpp）
- kFold 以**升序类 id** 顺序消费共享 mt19937 种子流：同 seed 折划分与
  Qt 版本/哈希布局无关；保序重命名类 id 不改变样本→折分配。
- 聚类后端（KMeans 族）remap 表不可用时，预测一律按未分类(0)计分并告警，
  原始簇号永不与类号直接比较（簇号空间 ≠ 类号空间）。

## OBIA（analysis/segmentation/）
- 多数票平票规则"取较小 id"在 id 0 参与时同样成立（rs_majority_vote.h）。
- 类栅格矢量化的连通性与分割器一致（8 连通）：对角相邻同标签输出为
  单一多边形（GDALPolygonize 8CONNECTED=8），栅格↔矢量对象身份守恒。

## 变化检测（processing/algorithms/change_detector.cpp、operators/rs/rs_change_streaming.cpp）
- ChangeDetector::computeLogRatio 与库级 logRatio 语义对齐：非有限输入
  → NaN（NoData/云像元不得静默判"无变化"），负值钳 0 后加 eps。
- changedArea 对非米线性单位的投影 CRS（如 US survey foot）按
  (米/单位)² 换算，"always m2" 契约在地理/投影米/投影非米三种 CRS 下
  均成立（地理分支 Snyder 弧长换算不变）。

## 特征缩放（analysis/classification/rs_feature_scaler.cpp）
- fromJson 对未知 method 判别式与未来 version fail-closed（与 fit 的
  NaN 拒绝、四数组等长校验同一纪律）。

## 已核实不在本轨处理
- JM 可分性上溢（#1550）、直方图均衡 ±Inf（#1538）：master 已修（FIXED_IN_MASTER_BUT_OPEN）。
- spatial CV 的 CV_64F 输入：OpenCV 5.0 copyTo 行视图跨类型就地转换正确，
  判 UNREPRODUCED，测试保留为 dtype 契约守卫。
