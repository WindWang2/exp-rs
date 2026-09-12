<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 光谱指数与波段运算（spectral）

共 12 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:band_math

任意波段代数表达式计算器：以表达式形式组合多个波段做加减乘除与函数运算。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：expression（string）、height（integer）、output（raster）、width（integer）
- 参数：expression（string）、output（string）
- 局限：Expression uses 1-based band references (b1, b2, ...).
- 适用地物：任意地物
- 适用场景：自定义指数构建、教学演示波段运算
- 失败模式：
  - `INVALID_PARAMETER` — 表达式语法错误或引用了不存在的波段。处置：检查表达式变量名与波段列表是否一致
  - `EXECUTION_FAILED` — 表达式除零产生异常值。处置：在表达式中加入分母下限保护或后处理过滤
- 教学概念：波段代数、表达式求值
- 适用课程：遥感数字图像处理
- 典型练习：用 band_math 表达式实现 NBR = (NIR-SWIR2)/(NIR+SWIR2) 并用于火烧迹地分析。

## rs:band_ratio

通用波段比值运算：任选两个波段做比值，突出吸收特征与波段差异。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：blueBand（integer）、denominatorBand（integer）、greenBand（integer）、mode（enum）、numeratorBand（integer）、output（string）、redBand（integer）
- 局限：IHS masks NoData/sentinel pixels to NaN in all components.
- 适用地物：植被、矿物、水体
- 适用场景：矿物吸收特征增强、教学演示比值运算
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 波段号越界或未指定。处置：确认波段序号在影像波段数范围内
- 教学概念：波段比值、光谱特征
- 适用课程：遥感数字图像处理
- 典型练习：用 Landsat band5/band4 比值复现简单植被指数并讨论其与 NDVI 的关系。

## rs:evi

增强型植被指数 EVI，在高生物量区比 NDVI 饱和更慢，并引入蓝波段校正大气与土壤背景。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：blue×1、nir×1、red×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：blue（integer）、nir（integer）、output（string）、red（integer）、scale（numeric）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：森林、密集农田
- 适用场景：高生物量区植被监测、森林蓄积量相关研究
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 nir/red/blue 波段角色。处置：补齐三个波段的角色标注或显式波段号
- 教学概念：EVI、饱和效应、土壤背景校正
- 适用课程：植物遥感
- 典型练习：在密林区比较 NDVI 与 EVI 的饱和差异。
- 可接上游：rs:atmospheric_correction

## rs:extract_bands

从多波段影像中抽取指定波段子集，重排为新的影像，是特征筛选与带宽压缩的基础操作。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：bands（integer）、output（string）
- 适用地物：任意地物
- 适用场景：制作指数计算所需的最小波段集、多源影像波段统一
- 失败模式：
  - `INVALID_PARAMETER` — 波段序号越界或重复。处置：检查 bands 列表都在 1..N 且无重复
- 教学概念：波段选择、特征子集
- 适用课程：遥感数字图像处理
- 典型练习：从 Sentinel-2 13 波段中抽取 B02/B03/B04/B08 做后续 NDVI 生产。

## rs:mndwi

改进型归一化水体指数 MNDWI = (Green-SWIR)/(Green+SWIR)，对城镇背景中的水体更稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：green×1、swir×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：green（integer）、output（string）、swir（integer）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：水体、城市水体、湿地
- 适用场景：城市水体提取、细小水体识别
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 green/swir 波段角色。处置：标注角色或显式给波段号
- 教学概念：MNDWI、短波红外
- 适用课程：遥感数字图像处理
- 典型练习：在城市区比较 NDWI 与 MNDWI 的水体提取精度。
- 可接上游：rs:atmospheric_correction

## rs:ndbi

归一化建筑指数 NDBI = (SWIR-NIR)/(SWIR+NIR)，突出不透水面与建筑信息。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、swir×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：nir（integer）、output（string）、swir（integer）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：城市、不透水面
- 适用场景：城市扩张监测、不透水面制图
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 swir/nir 波段角色。处置：标注角色或显式给波段号
- 教学概念：NDBI、不透水面
- 适用课程：城市遥感
- 典型练习：结合 NDVI 与 MNDWI 做三指数约束的城市建成区提取。
- 可接上游：rs:atmospheric_correction

## rs:ndvi

归一化植被指数 NDVI = (NIR-Red)/(NIR+Red)，最常用的植被活力与覆盖度指标。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、red×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：nir（integer）、output（string）、red（integer）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：植被、农田、林地、草地
- 适用场景：植被长势监测、农作物估产预处理、生态变化监测
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 nir/red 波段角色或波段号。处置：标注角色（SICNU_BAND_ROLE）或显式传参波段号
  - `INVALID_RADIOMETRY` — 未做大气校正导致 NDVI 系统性偏低。处置：先执行 rs:atmospheric_correction
- 教学概念：NDVI、植被指数、红边
- 适用课程：遥感数字图像处理、植物遥感
- 典型练习：计算研究区生长季 NDVI 并按土地覆盖类型统计分布差异。
- 可接上游：rs:atmospheric_correction

## rs:ndwi

归一化水体指数 NDWI = (Green-NIR)/(Green+NIR)，用于地表水体提取与判读。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：green×1、nir×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：green（integer）、nir（integer）、output（string）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：水体、湿地、河流、湖泊
- 适用场景：水体范围制图、洪涝监测的输入特征
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 green/nir 波段角色。处置：标注角色或显式给波段号
- 教学概念：NDWI、水体指数
- 适用课程：遥感数字图像处理
- 典型练习：提取丰水期与枯水期湖泊范围并计算面积变化。
- 可接上游：rs:atmospheric_correction

## rs:pca

主成分分析（PCA/K-L 变换）：把相关波段压缩为按方差排序的独立主成分，用于降维、去相关与信息浓缩。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：numComponents（integer）、output（raster）
- 参数：numComponents（integer）、output（string）
- 适用地物：任意地物
- 适用场景：多波段数据压缩、分类前特征降维、变化检测（PC 差异法）的输入
- 失败模式：
  - `INSUFFICIENT_MEMORY` — 超大影像全量协方差计算内存超限。处置：改用空间子区估计统计量，或先分幅处理
- 教学概念：主成分分析、K-L 变换、降维
- 适用课程：遥感数字图像处理
- 典型练习：对 6 波段影像做 PCA，统计各主成分方差贡献并解释 PC1 的地物含义。
- 可接下游：rs:supervised_classification

## rs:savi

土壤调节植被指数 SAVI，通过 L 系数降低裸土背景对植被指数的影响，适合低覆盖度植被区。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、red×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：nir（integer）、output（string）、red（integer）、scale（numeric）
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：干旱半干旱植被、稀疏草地、裸土混合区
- 适用场景：干旱区植被监测、低覆盖度植被估测
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 nir/red 波段角色。处置：标注角色或显式给波段号
- 教学概念：SAVI、土壤线、背景调节
- 适用课程：植物遥感
- 典型练习：在荒漠草原区比较 NDVI 与 SAVI 对稀疏植被的敏感度。
- 可接上游：rs:atmospheric_correction

## rs:spectral_derivative

光谱导数运算：沿波段维计算一阶/二阶导数，突出红边等吸收特征细节，面向高光谱与多光谱精细分析。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bandsOut（integer）、order（integer）、output（raster）
- 参数：order（integer）、output（string）、wavelengths（numeric）
- 前置条件：Bands must carry WAVELENGTH metadata (nm) or an explicit 'wavelengths' parameter; the axis must be strictly ascending in band order.
- 局限：Output band count shrinks by the derivative order (B−1 / B−2); output band b carries the midpoint wavelength of its input pair.；NaN pixels propagate to every output derivative that touches them.
- 适用地物：植被、矿物、水体
- 适用场景：红边位置分析、作物胁迫探测
- 失败模式：
  - `NOT_SUPPORTED` — 波段数过少无法求导。处置：光谱波段数需满足导数阶数要求
- 教学概念：光谱导数、红边、吸收特征
- 适用课程：高光谱遥感
- 典型练习：对作物高光谱曲线求一阶导数并定位红边位置随生育期的移动。

## rs:spectral_index

角色解析的通用光谱指数计算器：按波段角色（nir/red/…）或显式波段号计算 NDVI/EVI/SAVI/NDWI/NDBI/MNDWI 等常用指数。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）、postfire（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：blue（integer）、green（integer）、index（enum）、nir（integer）、output（string）、red（integer）、rededge（integer）、scale（numeric）、swir（integer）、swir2（integer）
- 前置条件：Input raster must have sufficient bands for the selected index.；建议输入经大气校正的地表反射率数据。
- 局限：Band numbers are 1-based and must exist in the input raster. When a band parameter is omitted, it is resolved from the input's SICNU_BAND_ROLE product metadata (semantic band roles) instead of the positional default.；EVI and SAVI constants assume unit reflectance [0,1]. When the input carries SICNU_NUMERIC_SCALE (stamped by rs:landsat_import / rs:sentinel2_import for verbatim DN-scale Level-2 stacks), or when params.scale is a finite multiplicative scale (e.g. 0.0001 for Landsat Collection 2 DN), the participating bands are mapped to unit reflectance for the computation; an explicit scale param wins over the metadata stamp. Ratio indices are scale-invariant and inputs are never rescaled on disk.；MSAVI, EVI2 and BAI are unit-reflectance-anchored like EVI/SAVI; without declared scale metadata they fall back to the same documented magnitude heuristic (grid-and-radiometric-policy §2), which can misfire on all-dark DN scenes — declare the scale for honest results.
- 适用地物：植被、水体、建筑、裸土
- 适用场景：植被/水体/建筑指数制图、时间序列指数生产（配合 rs:temporal_* 算子）
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 影像缺少 SICNU_BAND_ROLE 角色标注且未给显式波段号。处置：标注波段角色或以参数形式指定 red/nir 等波段序号
  - `INVALID_RADIOMETRY` — 输入为 DN/辐亮度导致指数物理意义失真。处置：先完成辐射定标与大气校正（rs:radiometric_calibration → rs:atmospheric_correction）
- 教学概念：归一化指数、波段角色、光谱特征工程
- 适用课程：遥感数字图像处理、定量遥感基础
- 典型练习：对同一景影像分别计算 NDVI、NDWI、NDBI 并合成 RGB 假彩色判读。
- 可接上游：rs:atmospheric_correction
- 可接下游：rs:change_normalized_difference

