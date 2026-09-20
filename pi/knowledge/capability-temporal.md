<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 时序分析（temporal）

共 22 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:temporal_anomaly

时序异常检测：以历史均值基线计算 z-score 或差值，标记显著偏离当期的异常像元。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、baseline_end（string）、baseline_start（string）、collection（string）、duplicate_policy（enum）、method（enum）、min_observations（integer）、output（string）、scenes（string）、target_time（string）、tile_size（integer）
- 适用地物：植被、水体、农田
- 适用场景：病虫害/旱情异常提示、水体异常扩张告警
- 失败模式：
  - `NOT_SUPPORTED` — 基线期观测过少。处置：基线至少需要 5 期以上可靠观测
  - `TIME_ORDER_INVALID` — 基线与当期时间标签错乱。处置：核对影像日期元数据
- 教学概念：Z 分数、基线偏离、时序异常
- 适用课程：遥感应用分析
- 典型练习：以 2015–2020 基线检测 2024 年夏季 NDVI 负异常区域。

## rs:temporal_breakpoints

时序断点检测：逐像元分段线性趋势拟合检测结构性突变点（扰动、撂荒、洪泛），定位变化发生时间。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、brokenPixelFraction（numeric）、maxBreaks（integer）、memory（json）、output（raster）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、maxBreaks（integer）、minImprovement（numeric）、minSegmentDays（numeric）、output（string）、outputBreakDates（boolean）、scenes（string）、tile_size（integer）
- 适用地物：森林、农田、水体
- 适用场景：森林扰动年份制图、土地利用转型检测
- 失败模式：
  - `NOT_SUPPORTED` — 时序过短无法建立稳定历史段。处置：建议 3 年以上周期观测
- 教学概念：分段线性回归、结构断点、突变检测
- 适用课程：遥感应用分析
- 典型练习：检测 2000–2024 NDVI 时序的断点年份并与采伐记录核对。

## rs:temporal_composite

时序合成：把多期影像按最优像元（质量分）/均值/中值合成一景，压制云与噪声。默认 quality_band=0 时各期得分相同，按目标日期就近选取；需要经典 MVC 时把指数波段显式设为 quality_band。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、periodCount（integer）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、method（enum）、output（string）、period（enum）、period_days（integer）、quality_band（integer）、scenes（string）、target_date（string）、tile_size（integer）
- 前置条件：所有期次影像须配准到同一网格并统一辐射量纲。
- 适用地物：植被、水体、任意地物
- 适用场景：月度/季度无云底图生产、NDVI 时序预处理
- 失败模式：
  - `TIME_ORDER_INVALID` — 各期影像未按时间排序或日期缺失。处置：提供含日期的时序输入集合并检查排序
  - `GRID_MISMATCH` — 各期影像网格不一致。处置：逐期 rs:align 对齐后再合成
- 教学概念：影像合成、最优像元合成、中值合成
- 适用课程：遥感数字图像处理
- 典型练习：把 12 期月度 NDVI 合成为年度最大值图并对比云污染情况。

## rs:temporal_decompose

时序分解：把时序拆为趋势/季节/残差分量（STL 思想），支撑长期趋势与季节动态分离（容差级算子）。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：optical
- 输出：bands（integer）、components（string）、output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、components（string）、duplicate_policy（enum）、output（string）、scenes（string）、seasonal_window_days（integer）、tile_size（integer）、trend_lambda（numeric）
- 适用地物：植被、水体、农田
- 适用场景：长期趋势与季节动态分离、城郊扩张的时序证据分析
- 失败模式：
  - `NOT_SUPPORTED` — 时序长度不足以估计季节分量。处置：至少需要两个完整周期的观测
- 教学概念：季节-趋势分解（Whittaker+气候态）、季节分量、趋势分量
- 适用课程：遥感应用分析、时间统计
- 典型练习：分解 5 年月度 NDVI 时序并解读趋势项的城市绿地变化。

## rs:temporal_extract_regions

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：emptyCells（integer）、medianEnabled（boolean）、output（table）、pointRegions（integer）、polygonRegions（integer）、regionCount（integer）、rowsWritten（integer）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、max_regions（integer）、median_budget_mb（numeric）、output（string）、regions（string）、regions_file（string）、scenes（string）

## rs:temporal_extract_series

从影像集合中按 ROI 或像元抽取时序曲线，输出点/区时序表，用于时序建模输入与教学演示。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（table）、series（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、output（string）、point（string）、polygon（string）、scenes（string）
- 适用地物：植被、水体、农田
- 适用场景：物候曲线提取、样点时序采样
- 失败模式：
  - `DATASET_NOT_FOUND` — 时序集合中引用的影像缺失。处置：检查时序集合清单与文件路径
- 教学概念：时序曲线、物候
- 适用课程：遥感应用分析
- 典型练习：提取农田样区 NDVI 时序曲线并标注播种与收获窗口。

## rs:temporal_gap_fill

时序插值补洞：对含缺失（云/阴影）的时序按时间维度插值（线性/最近邻），恢复连续时序。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、filledFraction（numeric）、memory（json）、output（raster）、provenanceOutput（string）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、max_gap_days（numeric）、method（enum）、output（string）、provenance_output（string）、scenes（string）、tile_size（integer）
- 适用地物：植被、农田
- 适用场景：去云后的 NDVI 时序修复、物候分析前的时间连续化
- 失败模式：
  - `INVALID_PARAMETER` — 最大插值窗口超过时序可用长度。处置：缩短 max_gap 参数或补充影像期次
  - `TIME_ORDER_INVALID` — 时序日期乱序。处置：先按时间排序输入集合
- 教学概念：时间插值、缺失值
- 适用课程：遥感数字图像处理
- 典型练习：对含云 NDVI 时序执行线性插值并检查插值段与实测段的吻合度。
- 可接下游：rs:temporal_phenology

## rs:temporal_harmonic_breaks

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：epochDate（string）、meanBreakMagnitude（numeric）、memory（json）、output（raster）、pixelsWithBreaks（integer）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、direction（enum）、duplicate_policy（enum）、harmonics（integer）、maxBreaks（integer）、minImprovement（numeric）、minMagnitude（numeric）、minSegmentDays（numeric）、output（string）、recoveryTolerance（numeric）、robust（boolean）、scenes（string）、tile_size（integer）

## rs:temporal_harmonic_fit

谐波拟合：以正弦/余弦基拟合年内地物节律（HANTS 思想），可同时插值与去云。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、fittedPixelFraction（numeric）、harmonics（integer）、memory（json）、output（raster）、robust（boolean）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、ci_level（numeric）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、harmonics（integer）、minObservations（integer）、output（string）、robust（boolean）、scenes（string）、tile_size（integer）、writeCoefficients（boolean）
- 适用地物：农田、落叶林
- 适用场景：双季作物识别、物候参数（峰值/相位）提取
- 失败模式：
  - `NOT_SUPPORTED` — 一年内有效观测过少无法稳定拟合。处置：保证全年至少 10–12 个有效观测
- 教学概念：谐波回归、物候相位、HANTS
- 适用课程：遥感应用分析
- 典型练习：用两个谐波拟合一年 NDVI 并提取峰值日期图。

## rs:temporal_index_series

时序指数生产：对多期影像逐期计算光谱指数并堆叠为时序立方体，是时序分析的标准输入准备。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、bands（json）、collection（string）、duplicate_policy（enum）、index（enum）、output（string）、scenes（string）、tile_size（integer）
- 适用地物：植被、水体、城市
- 适用场景：NDVI/NDWI 时序立方体生产、长时序变化检测输入
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 个别期次影像缺少波段角色标注。处置：统一输入集合的波段角色标注
  - `GRID_MISMATCH` — 期次间网格不一致。处置：逐期 rs:align 对齐
- 教学概念：指数时序、时序立方体
- 适用课程：遥感数字图像处理
- 典型练习：生成 5 年 16 天合成的 NDVI 时序立方体供趋势分析。
- 可接下游：rs:temporal_trend、rs:temporal_phenology

## rs:temporal_model_select

逐像元有界模型选择：{谐波阶数} × {断点预算} 候选网格按 AICc/BIC/分块 CV 打分，平局取最小模型，退化像元如实拒绝。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：memory（json）、output（raster）、pixelsByBreaks（json）、pixelsByHarmonics（json）、pixelsFitted（integer）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、cvFolds（integer）、duplicate_policy（enum）、maxBreaks（integer）、maxHarmonics（integer）、minImprovement（numeric）、minSegmentDays（numeric）、output（string）、penalty（enum）、robust（boolean）、scenes（string）、tile_size（integer）
- 前置条件：>= 4 valid samples
- 适用地物：农田、草地、森林
- 适用场景：物候建模前的模型复杂度选择、谐波阶数论证、分区拟合诊断
- 失败模式：
  - `INVALID_PARAMETER` — maxHarmonics/maxBreaks/cvFolds 超范围。处置：maxHarmonics 0-3，maxBreaks 0-4，cvFolds 2-10
  - `NOT_SUPPORTED` — 样本不足或无候选可拟合。处置：输出 NaN 拒绝带（degraded），勿用其参与下游统计
- 教学概念：AICc/BIC、分块交叉验证、确定性平局规则、模型退化与拒绝
- 适用课程：时间序列分析、统计方法
- 典型练习：对农田与自然植被分区运行，比较 AICc 与 BIC 选择的谐波阶数分布并解释差异。

## rs:temporal_monitor

时序监测任务算子：以滑动窗口对最新观测做状态评估（继续/警戒/告警），面向业务化持续监测场景。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：method（string）、output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（string）、collection（string）、drift（numeric）、lambda（numeric）、max_pairwork（integer）、method（enum）、min_observations（integer）、output（string）、scenes（string）、tile_size（integer）
- 前置条件：Common grid, acquisition times, consistent radiometric state (temporal preflight).
- 局限：seasonal_mk is O(sum n_m^2) pairs per pixel; the max_pairwork guard refuses unbounded collections instead of degrading.；argmax bands are 0-based scene indices; scene dates travel in the collection metadata.
- 适用地物：植被、水体、农田
- 适用场景：耕地撂荒预警、裸土开发行为监测
- 失败模式：
  - `INVALID_PARAMETER` — 监测窗口或阈值配置缺失。处置：补全 window/threshold 监测参数
- 教学概念：滑动窗口、状态监测、阈值告警
- 适用课程：遥感应用分析
- 典型练习：配置 NDVI 季度滑动监测并在掉产 20% 时输出警戒状态。

## rs:temporal_phenology

物候参数提取：从重构时序中计算生长季开始/结束/峰值等物候期，输出物候图层。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、red×1
- 输出：bands（integer）、memory（json）、metrics（string）、output（raster）、sceneCount（integer）、timeEnd（string）、timeStart（string）、validPixelFraction（numeric）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、crossingFraction（numeric）、cycles（integer）、duplicate_policy（enum）、minValidPerSeason（integer）、output（string）、scenes（string）、season2EndDoy（integer）、season2StartDoy（integer）、seasonEndDoy（integer）、seasonStartDoy（integer）、tile_size（integer）
- 前置条件：建议先用 rs:temporal_smooth / rs:temporal_harmonic_fit 重构时序。
- 适用地物：农田、草地、落叶林
- 适用场景：作物生育期监测、物候对气候响应研究
- 失败模式：
  - `INVALID_PARAMETER` — 阈值或拟合参数与数据周期不匹配。处置：按影像时间分辨率与作物类型调整阈值
- 教学概念：生长季 SOS/EOS、物候期、返青
- 适用课程：植物遥感、农业气象
- 典型练习：提取研究区小麦 SOS/EOS 图并分析海拔梯度上的推迟效应。
- 可接上游：rs:temporal_index_series、rs:temporal_gap_fill、rs:temporal_smooth

## rs:temporal_phenology_multi

物候 2.0：自动多周期候选（峰值检测）、跨年窗口按收获年归属、逐窗口质量旗标；低覆盖窗口拒绝输出而非硬猜。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：meanCyclesPerYear（numeric）、memory（json）、output（raster）、pixelsWithAnyCycle（integer）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、crossingFraction（numeric）、duplicate_policy（enum）、maxCyclesPerYear（integer）、maxGapFraction（numeric）、minCoverage（numeric）、minPeakFraction（numeric）、minValidPerSeason（integer）、output（string）、scenes（string）、tile_size（integer）
- 前置条件：Common grid, acquisition times; >= ~2 full years for stable climatology; >= ~12 valid samples
- 适用地物：农田、果园、草地
- 适用场景：多熟制识别、跨年作物窗口、物候质量分级
- 失败模式：
  - `INVALID_PARAMETER` — crossingFraction/maxGapFraction/minCoverage 超范围。处置：各参数均在 (0,1]；阈值按数据质量调整
  - `NOT_SUPPORTED` — 窗口样本不足/覆盖不足/间隙过大。处置：refusal 计数带如实记录；先用 rs:temporal_gap_fill 补齐再运行
- 教学概念：自动周期候选、跨年（收获年）窗口、质量旗标与拒绝语义、多熟制指数
- 适用课程：农业遥感、物候学
- 典型练习：对比华北冬小麦区（一年两熟）与东北地区（一熟）的 cycle_count 空间格局并核对统计数据。

## rs:temporal_region_features

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：featureCount（integer）、output（table）、regionCount（integer）、sceneCount（integer）、schema（string）、sidecar（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、change_harmonics（integer）、collection（string）、cycles（integer）、direction（enum）、duplicate_policy（enum）、max_regions（integer）、output（string）、regions（string）、regions_file（string）、scenes（string）、seasonEndDoy（integer）、seasonStartDoy（integer）、sidecar_path（string）、trend_method（enum）

## rs:temporal_regularize

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、cadenceDays（numeric）、calendarEnd（string）、calendarPoints（integer）、calendarStart（string）、filledFraction（numeric）、memory（json）、output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、cadence（string）、collection（string）、duplicate_policy（enum）、lambda（numeric）、max_gap_nodes（integer）、max_window_days（numeric）、method（enum）、output（string）、scenes（string）、tile_size（integer）

## rs:temporal_sar_fusion

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：optical（raster）、sar（raster）
- 输出：bands（integer）、memory（json）、opticalBands（integer）、output（raster）、sarBands（integer）
- 参数：grid_tolerance（numeric）、output（string）、tile_size（integer）

## rs:temporal_seasonal_breaks

季节分量突变检测与归因：联合谐波+趋势分段后，用嵌套模型 F 检验区分趋势突变与季节幅相突变，可选 bootstrap 置信区间。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：epochDate（string）、memory（json）、output（raster）、pixelsWithBreaks（integer）、pixelsWithSeasonalBreaks（integer）、sceneCount（integer）
- 参数：alpha（numeric）、apply_qa_masking（boolean）、band（integer）、band_role（enum）、bootstrap_resamples（integer）、bootstrap_seed（integer）、ci_level（numeric）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、harmonics（integer）、maxBreaks（integer）、minImprovement（numeric）、minSegmentDays（numeric）、output（string）、robust（boolean）、scenes（string）、tile_size（integer）
- 前置条件：Common grid, acquisition times, consistent radiometric state; >= ~2 years for stable harmonics
- 适用地物：森林、农田、灌草地
- 适用场景：植被物候突变监测、作物制度转换识别、干扰与恢复制图
- 失败模式：
  - `INVALID_PARAMETER` — harmonics/alpha/minImprovement 超出允许范围。处置：harmonics 1-3，alpha (0.001,0.5]，minImprovement (0,1]
  - `NOT_SUPPORTED` — 有效样本不足或单侧分段过短无法完成嵌套检验。处置：延长时序或降低 maxBreaks；untestable 用类别 4 如实标记
- 教学概念：季节分量突变、嵌套 F 检验、归因（趋势 vs 季节）、bootstrap 置信区间
- 适用课程：植物遥感、时间序列分析
- 典型练习：对 2019-2024 NDVI 时序运行本算子，区分灌溉启用（季节突变）与采伐（趋势突变）空间分布。

## rs:temporal_sen_trend

Theil-Sen 稳健趋势 + Mann-Kendall 检验：对含噪声时序估计稳健斜率并做显著性检验，抗离群值优于普通最小二乘。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、memory（json）、output（raster）、sceneCount（integer）、significantPixelFraction（numeric）、timeEnd（string）、timeStart（string）
- 参数：alpha（numeric）、apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、output（string）、scenes（string）、tile_size（integer）
- 适用地物：植被、干旱区、农田
- 适用场景：干旱区退化监测、含云时序的稳健趋势估计
- 失败模式：
  - `NOT_SUPPORTED` — 期数不足导致 MK 检验失效。处置：建议至少 8–10 期有效观测
- 教学概念：Theil-Sen、Mann-Kendall、稳健趋势
- 适用课程：遥感应用分析、统计方法
- 典型练习：对比 OLS 与 Theil-Sen 在含云 NDVI 时序上的趋势差异。

## rs:temporal_smooth

时序平滑：Savitzky-Golay 等方法压制时序残余噪声，恢复物候曲线形态（容差级算子）。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：optical
- 输出：bands（integer）、memory（json）、method（string）、output（raster）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、degree（integer）、duplicate_policy（enum）、lambda（numeric）、method（enum）、moving_average_window（integer）、output（string）、robust_iterations（integer）、scenes（string）、tile_size（integer）、window（integer）、window_days（numeric）
- 适用地物：植被、农田
- 适用场景：物候曲线整形、时序异常检测前的平滑
- 失败模式：
  - `INVALID_PARAMETER` — 平滑窗口大于时序长度。处置：缩短窗口或补充期次
- 教学概念：Savitzky-Golay、时序平滑
- 适用课程：遥感应用分析
- 典型练习：对比原始与 SG 平滑后的 NDVI 时序，量化噪声压制幅度。
- 可接下游：rs:temporal_phenology

## rs:temporal_summary

时序摘要统计：对时序立方体按像元输出最小/最大/均值/方差等概要图层，用于快速体检时序数据。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、sceneCount（integer）、validFraction（numeric）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、include_median（boolean）、output（string）、scenes（string）、tile_size（integer）
- 适用地物：任意地物
- 适用场景：时序数据质量体检、变化检测前的概要统计
- 失败模式：
  - `GRID_MISMATCH` — 时序各期网格不一致。处置：先统一网格（rs:align）再做摘要
- 教学概念：时序统计量、数据概要
- 适用课程：遥感数字图像处理
- 典型练习：对全年 NDVI 时序生成最小值/方差图层定位不稳定区域。

## rs:temporal_trend

逐像元时序趋势拟合：普通最小二乘估计斜率/截距并输出 R²/RMSE 佐证拟合质量；显著性检验请用 rs:temporal_sen_trend。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、output（string）、scenes（string）、tile_size（integer）
- 适用地物：植被、城市、水体
- 适用场景：绿化/退化趋势制图、围填海等长期变化速率估计
- 失败模式：
  - `NOT_SUPPORTED` — 有效观测期数过少。处置：每个像元至少需要 2 期有效观测，建议 ≥3 期以获得稳定 R²/RMSE
- 教学概念：最小二乘趋势、变化速率、拟合优度
- 适用课程：遥感应用分析
- 典型练习：计算 20 年生长季 NDVI 趋势斜率，用 R² 过滤拟合可信区域；需要显著性检验时改用 rs:temporal_sen_trend。
- 可接上游：rs:temporal_index_series

