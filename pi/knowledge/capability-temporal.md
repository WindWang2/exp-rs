<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 时序分析（temporal）

共 22 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:temporal_anomaly

时序异常检测：以历史均值基线计算 z-score 或差值，标记显著偏离当期的异常像元。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、baseline_end（string）、baseline_start（string）、collection（string）、duplicate_policy（enum）、method（enum）、min_observations（integer）、output（string）、scenes（string）、target_time（string）、tile_size（integer）
- 前置条件：需要足够长的历史时序建立均值基线：基线期被污染（历史异常）会带入阈值并抬高/压低当期虚警。
- 局限：z-score/差值阈值对全场景统一适用：趋势与季节性强的像元（物候循环）需先去季节项，否则常态循环会被误报为异常。
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
- 前置条件：需要足够长的时序观测（分段线性拟合要求每段最少点数）：时相缺失或采样过稀会降低断点定位精度。
- 局限：检出的是结构性趋势突变点，不区分突变原因（扰动/撂荒/洪泛等需业务判读）；定位精度受时序长度、采样密度与噪声影响。
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
- 局限：合成质量取决于质量分：quality_band=0（默认）表示各期等分、按目标日期就近选取；经典 MVC 需把指数波段显式设为 quality_band。；合成会压制云与噪声，但云掩膜不完整时污染期仍会进入合成；输出为重构值而非单一观测。
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
- 前置条件：需要覆盖完整季节周期、等间隔或声明时间戳的时序：STL 思想的分解在观测不足时季节/趋势分离失真。
- 局限：容差级算子（并行归约的浮点重排在容差内）：分量解释依赖周期与窗宽参数；残差分量包含未建模噪声，不宜单独当物理量使用。
- 适用地物：植被、水体、农田
- 适用场景：长期趋势与季节动态分离、城郊扩张的时序证据分析
- 失败模式：
  - `NOT_SUPPORTED` — 时序长度不足以估计季节分量。处置：至少需要两个完整周期的观测
- 教学概念：季节-趋势分解（Whittaker+气候态）、季节分量、趋势分量
- 适用课程：遥感应用分析、时间统计
- 典型练习：分解 5 年月度 NDVI 时序并解读趋势项的城市绿地变化。

## rs:temporal_extract_regions

多区域时序提取：一次调用对多个点/面提取时序统计，输出区域×日期表。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：emptyCells（integer）、medianEnabled（boolean）、output（table）、pointRegions（integer）、polygonRegions（integer）、regionCount（integer）、rowsWritten（integer）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、max_regions（integer）、median_budget_mb（numeric）、output（string）、regions（string）、regions_file（string）、scenes（string）
- 前置条件：多个点/面区域以参数列表给出：区域几何需与影像 CRS 一致。
- 局限：输出区域×日期统计表：面区域统计为区域内像元聚合，大区域会平滑内部异质性；区域数量增长线性增加计算量。
- 适用地物：耕地、林地、水体
- 适用场景：多地块物候对比、时序监测
- 失败模式：
  - `INVALID_PARAMETER` — regions 为空或 id 重复。处置：为每个区域提供唯一调用方 id
- 教学概念：时序提取、区域统计
- 适用课程：遥感时序分析
- 典型练习：对多地块一次提取 NDVI 时序并比较物候差异。

## rs:temporal_extract_series

从影像集合中按 ROI 或像元抽取时序曲线，输出点/区时序表，用于时序建模输入与教学演示。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：output（table）、series（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、output（string）、point（string）、polygon（string）、scenes（string）
- 前置条件：影像集合以路径列表给出：影像需带地理参考，ROI 与影像 CRS 一致。
- 局限：云/无效像元按掩膜剔除后输出：缺失期在时序表中留空，连续建模前应接 rs:temporal_gap_fill 等插值链。
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
- 前置条件：时序中的缺失（云/阴影）需以掩膜/NoData 标示：插值仅按时间维度进行（线性/最近邻）。
- 局限：长缺口插值不可信：线性插值会抹平真实事件信号，最近邻跨大缺口产生阶跃；补洞结果应视为重构值并保留缺失位置信息。
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

季节调整趋势断裂分割：逐段谐波+线性趋势重拟合（BFAST/CCDP 思想的诚实实现）。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：epochDate（string）、meanBreakMagnitude（numeric）、memory（json）、output（raster）、pixelsWithBreaks（integer）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、direction（enum）、duplicate_policy（enum）、harmonics（integer）、maxBreaks（integer）、minImprovement（numeric）、minMagnitude（numeric）、minSegmentDays（numeric）、output（string）、recoveryTolerance（numeric）、robust（boolean）、scenes（string）、tile_size（integer）
- 前置条件：需要覆盖完整季节周期的时序：逐段谐波+线性趋势重拟合要求每段最少观测数。
- 局限：断点密度受断点预算约束；谐波阶数过高会吸收真实突变——与 BFAST/CCDP 相同的阶数-灵敏度权衡。
- 适用地物：耕地、林地
- 适用场景：扰动检测、物候突变分析
- 失败模式：
  - `INVALID_PARAMETER` — 时序过短无法拟合。处置：保证足够时相数或降低谐波阶数
- 教学概念：谐波模型、趋势断裂、扰动恢复
- 适用课程：遥感时序分析
- 典型练习：对长时序 NDVI 检测扰动年份并解释 recovery 语义。

## rs:temporal_harmonic_fit

谐波拟合：以正弦/余弦基拟合年内地物节律（HANTS 思想），可同时插值与去云。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、fittedPixelFraction（numeric）、harmonics（integer）、memory（json）、output（raster）、robust（boolean）、sceneCount（integer）、timeEnd（string）、timeStart（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、ci_level（numeric）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、harmonics（integer）、minObservations（integer）、output（string）、robust（boolean）、scenes（string）、tile_size（integer）、writeCoefficients（boolean）
- 前置条件：需要至少覆盖一个年周期的时序并给定谐波阶数：采样过稀时高阶谐波不稳定。
- 局限：拟合重构会平滑掉短时突变（收割/火灾等事件信号），同时插值与去云属模型化重构而非观测值；高阶谐波对噪声敏感。
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
- 前置条件：多期影像需已完成辐射归一化且指数参数逐期一致：否则时序立方体内不可比。
- 局限：时序质量受最差一期限制：云未掩膜的期会在时序中产生尖峰，建议先做 QA 掩膜再进入时序分析。
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
- 局限：模型选择只在候选网格（谐波阶数 × 断点预算）内进行：网格外的模型形态不可达；按 AICc/BIC/分块 CV 打分，平局取最小模型，退化像元如实拒绝而不强制输出。
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
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、crossingFraction（numeric）、cycles（integer）、duplicate_policy（enum）、minValidPerSeason（integer）、output（string）、provenance（string）、scenes（string）、season2EndDoy（integer）、season2StartDoy（integer）、seasonEndDoy（integer）、seasonStartDoy（integer）、tile_size（integer）
- 前置条件：建议先用 rs:temporal_smooth / rs:temporal_harmonic_fit 重构时序。
- 局限：物候期提取依赖上游重构时序质量：云污染与假峰会导致生长季开始/结束误判；二季作区/干旱年的多峰情形需参数适配。
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
- 局限：低覆盖窗口拒绝输出而非硬猜（质量旗标逐窗口输出）；跨年窗口按收获年归属，物候年定义随窗口参数。
- 适用地物：农田、果园、草地
- 适用场景：多熟制识别、跨年作物窗口、物候质量分级
- 失败模式：
  - `INVALID_PARAMETER` — crossingFraction/maxGapFraction/minCoverage 超范围。处置：各参数均在 (0,1]；阈值按数据质量调整
  - `NOT_SUPPORTED` — 窗口样本不足/覆盖不足/间隙过大。处置：refusal 计数带如实记录；先用 rs:temporal_gap_fill 补齐再运行
- 教学概念：自动周期候选、跨年（收获年）窗口、质量旗标与拒绝语义、多熟制指数
- 适用课程：农业遥感、物候学
- 典型练习：对比华北冬小麦区（一年两熟）与东北地区（一熟）的 cycle_count 空间格局并核对统计数据。

## rs:temporal_region_features

区域级时序特征表：质量、分布、Sen/OLS 趋势、异常与多物候特征，带版本化 schema sidecar。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：featureCount（integer）、output（table）、regionCount（integer）、sceneCount（integer）、schema（string）、sidecar（string）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、change_harmonics（integer）、collection（string）、cycles（integer）、direction（enum）、duplicate_policy（enum）、max_regions（integer）、output（string）、regions（string）、regions_file（string）、scenes（string）、seasonEndDoy（integer）、seasonStartDoy（integer）、sidecar_path（string）、trend_method（enum）
- 前置条件：输入为区域×日期时序（或时序立方体与区域定义）：区域几何需与影像 CRS 一致。
- 局限：特征表带版本化 schema sidecar：下游应按 schema 版本解析列，不假设固定表结构。
- 适用地物：耕地、林地、水体
- 适用场景：样本特征生成、监督学习前处理
- 失败模式：
  - `INVALID_PARAMETER` — region id 与时序表不匹配。处置：先运行 rs:temporal_extract_regions 保持一致 id
- 教学概念：时序特征工程、Sen 趋势
- 适用课程：遥感时序分析
- 典型练习：生成区域特征表并与标签联接构建分类样本。

## rs:temporal_regularize

规则日历重采样：将不规则时相重排到 16 天/月等规则日历，带有效计数与填充计数溯源波段。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bands（integer）、cadenceDays（numeric）、calendarEnd（string）、calendarPoints（integer）、calendarStart（string）、filledFraction（numeric）、memory（json）、output（raster）、sceneCount（integer）
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、cadence（string）、collection（string）、duplicate_policy（enum）、lambda（numeric）、max_gap_nodes（integer）、max_window_days（numeric）、method（enum）、output（string）、scenes（string）、tile_size（integer）
- 前置条件：不规则时相输入需声明观测日期/时间戳，并给定目标规则日历（16 天/月等）。
- 局限：规则化是按目标日历的采样/聚合：非观测期的值来自邻近期或插值——有效计数与填充计数溯源波段必须参与下游质检。
- 适用地物：耕地、林地
- 适用场景：规则时序构建、多源时相对齐
- 失败模式：
  - `INVALID_PARAMETER` — calendar 非法或外推请求。处置：使用 16d/monthly 等受支持日历；不外推
- 教学概念：规则日历、重采样、插值
- 适用课程：遥感时序分析
- 典型练习：将不规则获取重排到 16 天日历并检查 filled_count。

## rs:temporal_sar_fusion

Fuse co-registered optical and SAR temporal feature rasters into one stacked feature cube

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：optical（raster）、sar（raster）
- 输出：bands（integer）、memory（json）、opticalBands（integer）、output（raster）、sarBands（integer）
- 参数：grid_tolerance（numeric）、output（string）、tile_size（integer）
- 前置条件：输入必须为已配准（同网格）的光学与 SAR 时序特征栅格。
- 局限：融合是特征堆叠（stacked feature cube）：不消除光学/SAR 特征的辐射语义差异，各特征定义由上游算子决定。
- 适用地物：光学/SAR 联合覆盖区
- 适用场景：光学-SAR 时序特征融合、云污染区的时序补全
- 适用性备注：输入为已配准的光学与 SAR 时序特征栅格。
- 失败模式：
  - `GRID_MISMATCH` — the optical and SAR inputs are not on the same pixel grid。处置：co-register both inputs first (rs:align or io:warp); the operator never resamples
  - `INVALID_PARAMETER` — a band name in the fusion band list is missing from one of the inputs。处置：check band names against each input's wavelength metadata before running
- 教学概念：光学-SAR 融合、特征堆栈、时序立方体
- 适用课程：微波遥感、时序分析
- 典型练习：融合光学与 SAR 时序特征栅格，检查输出特征堆栈的波段组织与缺失标记方式。

## rs:temporal_seasonal_breaks

季节分量突变检测与归因：联合谐波+趋势分段后，用嵌套模型 F 检验区分趋势突变与季节幅相突变，可选 bootstrap 置信区间。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：epochDate（string）、memory（json）、output（raster）、pixelsWithBreaks（integer）、pixelsWithSeasonalBreaks（integer）、sceneCount（integer）
- 参数：alpha（numeric）、apply_qa_masking（boolean）、band（integer）、band_role（enum）、bootstrap_resamples（integer）、bootstrap_seed（integer）、ci_level（numeric）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、harmonics（integer）、maxBreaks（integer）、minImprovement（numeric）、minSegmentDays（numeric）、output（string）、robust（boolean）、scenes（string）、tile_size（integer）
- 前置条件：Common grid, acquisition times, consistent radiometric state; >= ~2 years for stable harmonics
- 局限：嵌套模型 F 检验区分趋势突变与季节幅相突变：归因结论受样本量与噪声影响；bootstrap 置信区间为可选输出，开启后计算量上升。
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
- 参数：alpha（numeric）、apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、compute_ci（boolean）、duplicate_policy（enum）、output（string）、provenance（string）、scenes（string）、tile_size（integer）
- 前置条件：需要足够长的时序（Mann-Kendall 检验的统计功效随样本量增长）；建议先完成去云与平滑。
- 局限：显著性不等于幅度：检验结论需与 Theil-Sen 斜率联合解读；时序自相关较强时显著性会被高估。
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
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、degree（integer）、duplicate_policy（enum）、lambda（numeric）、method（enum）、moving_average_window（integer）、output（string）、provenance（string）、robust_iterations（integer）、scenes（string）、tile_size（integer）、window（integer）、window_days（numeric）
- 前置条件：时序应先完成去云/QA 掩膜：平滑无法恢复被云污染期的真实信号。
- 局限：容差级算子（Savitzky-Golay 等）：窗宽与阶数决定平滑强度，过强会削平真实物候峰值、使曲线形态失真。
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
- 前置条件：输入为时序立方体（多期堆叠）：各期网格需一致。
- 局限：概要统计是快速体检工具：最小/最大对单期噪声敏感，方差含季节信号——趋势与结构分析应接 trend/decompose 族。
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
- 参数：apply_qa_masking（boolean）、band（integer）、band_role（enum）、collection（string）、duplicate_policy（enum）、output（string）、provenance（string）、scenes（string）、tile_size（integer）
- 前置条件：需要足够期数的时序做 OLS 拟合：建议先完成去云、插值与平滑。
- 局限：OLS 对离群值（云污染期）敏感：显著性检验不在本算子范围（用 rs:temporal_sen_trend）；R²/RMSE 是拟合质量佐证，不是变化显著性结论。
- 适用地物：植被、城市、水体
- 适用场景：绿化/退化趋势制图、围填海等长期变化速率估计
- 失败模式：
  - `NOT_SUPPORTED` — 有效观测期数过少。处置：每个像元至少需要 2 期有效观测，建议 ≥3 期以获得稳定 R²/RMSE
- 教学概念：最小二乘趋势、变化速率、拟合优度
- 适用课程：遥感应用分析
- 典型练习：计算 20 年生长季 NDVI 趋势斜率，用 R² 过滤拟合可信区域；需要显著性检验时改用 rs:temporal_sen_trend。
- 可接上游：rs:temporal_index_series

