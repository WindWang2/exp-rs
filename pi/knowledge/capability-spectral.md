<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 光谱指数与波段运算（spectral）

共 19 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:band_math

任意波段代数表达式计算器：以表达式形式组合多个波段做加减乘除与函数运算。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：expression（string）、height（integer）、output（raster）、width（integer）
- 参数：expression（string）、output（string）
- 前置条件：输入必须为多波段栅格；表达式中的波段引用为 1-based（b1, b2, ...），空表达式与越界引用在执行前即被类型化错误拒绝。
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
- 前置条件：ratio 模式要求分子/分母波段号不同（相同即类型化拒绝）；ihs 模式需 1-based R/G/B 波段号（默认 1/2/3）。
- 局限：IHS masks NoData/sentinel pixels to NaN in all components.
- 适用地物：植被、矿物、水体
- 适用场景：矿物吸收特征增强、教学演示比值运算
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 波段号越界或未指定。处置：确认波段序号在影像波段数范围内
- 教学概念：波段比值、光谱特征
- 适用课程：遥感数字图像处理
- 典型练习：用 Landsat band5/band4 比值复现简单植被指数并讨论其与 NDVI 的关系。

## rs:endmember_analysis

端元集合分析：按光谱角聚类把端元集压缩为非冗余代表，输出 SAM 角度矩阵，并可投影到传感器波段网格，生成带溯源的光谱表。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：inputRows（integer）、output（json）、outputRows（integer）
- 参数：angleMatrix（boolean）、endmembersRef（string）、mergeAngleDegrees（numeric）、output（string）、ppiCounts（integer）、requireFullCoverage（boolean）、sensor（string）
- 前置条件：Input must be an exp-rs:spectral-table with finite, non-zero endmember rows.
- 局限：Reduction caps at 512 input rows; the angle matrix embed caps at 64 rows (payload bound).；Projection reflects the reduced NATIVE-space set; angles change under resampling by design.
- 适用地物：矿物蚀变带、植被-土壤-水体混合区
- 适用场景：端元集精简与冗余剔除、光谱库构建与比对
- 适用性备注：输入为端元光谱表（exp-rs:spectral-table）。
- 失败模式：
  - `DATASET_NOT_FOUND` — endmembersRef 指向的端元表文件不存在。处置：确认 endmembersRef 路径（如上游 rs:endmember_extraction 的 endmembersOut）
  - `INVALID_PARAMETER` — endmembersRef 不是合法的 exp-rs:spectral-table（行含非有限值或零范数、行宽与 bandCount 不一致），或输入行数超过 512 行的归并上限。处置：重新生成合法端元表，或先筛选/合并到 512 行以内
  - `INVALID_PARAMETER` — ppiCounts 长度与表行数不符或含负值、angleMatrix/requireFullCoverage 非布尔、缩减后行数超过 64 仍请求角度矩阵。处置：按输入表行数对齐 ppiCounts，修正布尔参数，或在 64 行以内再请求角度矩阵
  - `WAVELENGTH_INCOMPATIBLE` — 传感器投影失败：输入表缺波长元数据、sensor id 未在 data/spectral/sensors.json 注册或未声明波段、端元波长覆盖不足且 requireFullCoverage 被开启。处置：为端元表补齐波长元数据、使用已注册的 sensor id，或关闭 requireFullCoverage 并接受未覆盖波段为 NaN
- 教学概念：端元、光谱角制图（SAM）、光谱表溯源、传感器波段响应
- 适用课程：高光谱遥感、矿物光谱
- 典型练习：对 PPI 提取的端元集执行 SAM 聚类压缩，比较压缩前后解混丰度图的差异，并解释投影到传感器波段网格的意义。

## rs:evi

增强型植被指数 EVI，在高生物量区比 NDVI 饱和更慢，并引入蓝波段校正大气与土壤背景。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：blue×1、nir×1、red×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：blue（integer）、nir（integer）、output（string）、red（integer）、scale（numeric）
- 前置条件：输入需含 NIR/Red/Blue 三波段：可由 SICNU_BAND_ROLE 解析或显式 1-based 波段号指定（默认 4/3/1）。；DN 域产品必须声明乘性 scale（如 Landsat Collection 2 的 0.0001）使常量工作在反射率域：显式 scale 参数 > 产品元数据印记 > 文档化量级回退。
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
- 前置条件：bands 数组为 1-based 波段号，数组顺序即输出顺序；越界选择在处理开始前类型化拒绝。
- 局限：纯波段拷贝/重排：不改像元值与地理参考，输出波段属性随数组顺序重排。
- 适用地物：任意地物
- 适用场景：制作指数计算所需的最小波段集、多源影像波段统一
- 失败模式：
  - `INVALID_PARAMETER` — 波段序号越界或重复。处置：检查 bands 列表都在 1..N 且无重复
- 教学概念：波段选择、特征子集
- 适用课程：遥感数字图像处理
- 典型练习：从 Sentinel-2 13 波段中抽取 B02/B03/B04/B08 做后续 NDVI 生产。

## rs:library_select

光谱库检索与子集：按材料或波长窗口筛选经验库条目，可投影到传感器波段网格。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：entries（integer）、output（json）
- 参数：libraryPath（string）、materials（string）、nearDuplicateAngleDeg（numeric）、output（string）、sensor（string）、wavelengthMax（numeric）、wavelengthMin（numeric）
- 前置条件：The source library must pass strict validation (loadValidated); sensor projection needs entries with wavelength grids.
- 局限：Near-duplicate detection reports pairs below the SAM threshold; it never removes entries by itself.
- 适用地物：矿物/材料样区
- 适用场景：光谱库构建、端元筛选
- 失败模式：
  - `INVALID_PARAMETER` — libraryPath 缺失或格式非法。处置：提供 data/spectral 格式的已验证光谱库
- 教学概念：光谱库、SAM 匹配
- 适用课程：高光谱遥感
- 典型练习：从光谱库中筛选研究区端元并投影到传感器波段。

## rs:local_rx_anomaly

局部（双窗口）RX 异常检测：以内窗口作保护带，计算各像元对局部背景的马氏距离，捕捉全局 RX 会平均掉的局部异常。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：covariance（string）、max（numeric）、mean（numeric）、output（raster）、scoredPixels（integer）、unscoredPixels（integer）
- 参数：covariance（enum）、innerWindow（integer）、loading（numeric）、minSamples（integer）、outerWindow（integer）、output（string）、qualityOut（string）
- 前置条件：需给定内窗口（guard window）与外窗口尺寸；有效背景样本不足的像元输出 NaN（计入 unscoredPixels），绝不硬评分。；波段数很高时建议 covariance=diagonal（全协方差在窗口样本量下不稳定）。
- 局限：Pixels whose window has fewer valid background samples than the minimum stay NaN (reported in unscoredPixels), never faked.；Raster edges use clamped (shrunken) windows; no replicated border pixels enter the statistics.
- 适用地物：港口、机场等背景相对均质的区域、背景与目标尺度差异明显的城区
- 适用场景：局部异常目标定位、小目标检测预处理
- 适用性备注：内窗口为保护带：目标位于内窗口、背景统计取自内外窗之间。
- 失败模式：
  - `DATASET_NOT_FOUND` — 输入多波段栅格文件不存在或无法用 GDAL 打开。处置：确认 input 路径存在且为可读栅格
  - `INVALID_PARAMETER` — outerWindow 非奇数或小于 3、innerWindow 非奇数/小于 1/不小于 outerWindow、covariance 取值不在 full/diagonal 之内、loading 为负或非有限。处置：按约束设置窗口与协方差参数（outer 为 >=3 的奇数，inner 为小于 outer 的奇数）
  - `NOT_SUPPORTED` — 输入栅格波段数少于 2（无法估计局部协方差），或 full 协方差模式下波段数超过 8192。处置：提供至少 2 个波段；高光谱场景改用 covariance=diagonal
  - `EXECUTION_FAILED` — 瓦片 halo 读取、分数/质量栅格写出或栅格 finalize 失败，或 RX 内核计算返回错误。处置：检查输出路径可写与磁盘空间，缩小 AOI 后重试
- 教学概念：局部 RX、双窗口（guard window）、马氏距离、背景协方差
- 适用课程：高光谱遥感、异常检测
- 典型练习：对同一高光谱场景分别运行全局 RX 与局部 RX，比较局部背景差异区域的检出差异，并解释内窗口保护带的作用。

## rs:mndwi

改进型归一化水体指数 MNDWI = (Green-SWIR)/(Green+SWIR)，对城镇背景中的水体更稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：green×1、swir×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：green（integer）、output（string）、swir（integer）
- 前置条件：输入需含 Green 与 SWIR 波段：可由 SICNU_BAND_ROLE 解析或显式 1-based 波段号指定（默认 2/5）。
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：水体、城市水体、湿地
- 适用场景：城市水体提取、细小水体识别
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 green/swir 波段角色。处置：标注角色或显式给波段号
- 教学概念：MNDWI、短波红外
- 适用课程：遥感数字图像处理
- 典型练习：在城市区比较 NDWI 与 MNDWI 的水体提取精度。
- 可接上游：rs:atmospheric_correction

## rs:mnf_inverse

MNF 逆变换：由 MNF 分量重建原始波段空间，支持噪声分量置零后的去噪重建。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：output（raster）、spectrumOut（json）
- 参数：components（integer）、errorOut（string）、output（string）、spectrumOut（string）、spectrumRef（string）、transform（string）
- 前置条件：Requires the transform artifact written by rs:mnf (transformOut); the model is digest-verified on load.
- 局限：Component subsets are a documented approximation: the dropped components' contribution is reported via errorOut / reconstructionError, never silently ignored.
- 适用地物：任意光学场景
- 适用场景：MNF 去噪、分量空间分析回写波段空间
- 失败模式：
  - `INVALID_PARAMETER` — 输入缺少 MNF 变换元数据。处置：先运行 rs:mnf 并保留其统计 sidecar
- 教学概念：MNF、逆变换、噪声分离
- 适用课程：高光谱遥感
- 典型练习：将噪声分量置零后逆变换，对比去噪前后的光谱曲线。

## rs:ndbi

归一化建筑指数 NDBI = (SWIR-NIR)/(SWIR+NIR)，突出不透水面与建筑信息。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、swir×1
- 输入：input（raster）
- 输出：height（integer）、index（string）、output（raster）、width（integer）
- 参数：nir（integer）、output（string）、swir（integer）
- 前置条件：输入需含 SWIR 与 NIR 波段：可由 SICNU_BAND_ROLE 解析或显式 1-based 波段号指定（默认 5/4）。
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
- 前置条件：输入需含 NIR 与 Red 波段：可由 SICNU_BAND_ROLE 解析或显式 1-based 波段号指定（默认 4/3）；建议在大气校正后计算。
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
- 前置条件：输入需含 Green 与 NIR 波段：可由 SICNU_BAND_ROLE 解析或显式 1-based 波段号指定（默认 2/4）。
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

主成分分析（PCA/K-L 变换）：把相关波段压缩为按方差排序的互不相关主成分，用于降维、去相关与信息浓缩。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：numComponents（integer）、output（raster）
- 参数：numComponents（integer）、output（string）
- 前置条件：输入为波段数 ≥2 的多波段影像，建议同量纲（反射率域）输入：混合量纲会使方差最大波段主导第一成分。；输出主成分按方差贡献降序排列，应结合各成分方差贡献选择保留数。
- 局限：载荷与成分由场景统计决定：跨场景/跨时相的主成分不可直接比较，语义解释必须结合当景方差贡献。
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
- 前置条件：输入需含 NIR 与 Red 波段（默认 4/3，可由 SICNU_BAND_ROLE 解析或显式指定）；L = 0.5 常量按反射率域工作，DN 域产品需声明乘性 scale。
- 局限：Band numbers are resolved from SICNU_BAND_ROLE when omitted.
- 适用地物：干旱半干旱植被、稀疏草地、裸土混合区
- 适用场景：干旱区植被监测、低覆盖度植被估测
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 缺少 nir/red 波段角色。处置：标注角色或显式给波段号
- 教学概念：SAVI、土壤线、背景调节
- 适用课程：植物遥感
- 典型练习：在荒漠草原区比较 NDVI 与 SAVI 对稀疏植被的敏感度。
- 可接上游：rs:atmospheric_correction

## rs:sparse_unmixing

稀疏解混（L1 + 非负 FISTA）：在（可超完备的）端元字典上估计每像元稀疏丰度，支持端元数多于波段数的过完备字典。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：atoms（integer）、convergedFraction（numeric）、lambda（numeric）、meanError（numeric）、meanIterations（numeric）、output（raster）
- 参数：bands（integer）、collinearAngleDegrees（numeric）、endmembers（string）、endmembersRef（string）、errorOut（string）、lambda（numeric）、libraryMaterials（string）、libraryPath（string）、maxIterations（integer）、output（string）、sumToOnePenalty（numeric）、tolerance（numeric）
- 前置条件：Atoms must use the same band order and units as the input raster.
- 局限：Sum-to-one is a penalty (like the FCLS solver), not a hard constraint; report mean |sum-1| via sumToOnePenalty QA if needed.；Near-duplicate atoms (below collinearAngleDegrees) refuse: an L1 split across near-identical atoms is not interpretable.
- 适用地物：矿物丰度制图区、植被-土壤-不透水面混合区
- 适用场景：过完备光谱库解混、丰度图生产
- 适用性备注：端元字典可多于波段数（过完备），lambda 控制稀疏度。
- 失败模式：
  - `DATASET_NOT_FOUND` — 输入多波段栅格文件不存在或无法用 GDAL 打开。处置：确认 input 路径存在且为可读栅格（必要时先转换格式）
  - `INVALID_PARAMETER` — 必填参数 input/output 缺失，解混参数越界（lambda/sumToOnePenalty 为负、tolerance<=0、maxIterations<1），端元字典（endmembers/endmembersRef/libraryPath）未提供或同时提供多种，或端元波段数与影像波段数不一致且任一侧无波长元数据。处置：补齐必填参数、把解混参数调到合法范围，只通过一种形式提供字典，并使端元波段数与影像一致或补齐波长元数据
  - `WAVELENGTH_INCOMPATIBLE` — 端元字典与输入影像的波长范围不重叠，或所选输入波段落在端元波长覆盖之外。处置：选择落在端元覆盖范围内的输入波段，或提供波长范围匹配的字典
  - `NOT_SUPPORTED` — 字典构建被拒绝：两原子光谱角小于 collinearAngleDegrees（近共线）、端元为零向量或含非有限值、原子数超过 2048、Gram 矩阵退化。处置：删除重复或近共线端元、调高 collinearAngleDegrees（或置 0 显式关闭守卫），并检查端元有限且非零
- 教学概念：稀疏解混、L1 正则、FISTA 迭代、丰度、过完备字典
- 适用课程：高光谱遥感
- 典型练习：调节 lambda 与容差观察丰度稀疏性与收敛行为，并解释 sum-to-one 作为软约束（惩罚项）而非硬约束的含义。

## rs:spectral_band_select

按索引或波长范围选择/剔除波段（坏波段剔除与子集提取）。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：bands（integer）、excludeRanges（json）、output（string）、wavelengthMax（numeric）、wavelengthMin（numeric）
- 前置条件：wavelengthMin/Max and excludeRanges need WAVELENGTH band metadata; the explicit 'bands' mode does not.
- 局限：Band selection renumbers bands; wavelength metadata of kept bands is preserved and normalized to nm.
- 适用地物：任意光学场景
- 适用场景：坏波段剔除、波段子集
- 失败模式：
  - `INVALID_PARAMETER` — 波段索引越界或波长窗口为空。处置：检查波段数与波长范围
- 教学概念：坏波段、波长选择
- 适用课程：高光谱遥感
- 典型练习：剔除水汽吸收波段后重跑分类对比精度。

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

## rs:spectral_similarity

SID-SAM 混合光谱相似度：把光谱角（形状）与信息散度（分布）融合为单一有界相似度，将每像元标注到最相似的参考光谱。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：form（string）、meanScore（numeric）、output（raster）、refs（integer）
- 参数：bands（integer）、form（enum）、libraryMaterials（string）、libraryPath（string）、output（string）、refs（string）、refsRef（string）、scoreOut（string）
- 前置条件：References must be reflectance-like (non-negative) on the same band grid as the input.
- 局限：Spectra with negative bands or zero norm are unlabelled (NaN score), never forced into a class.
- 适用地物：矿物/植被光谱匹配区
- 适用场景：参考光谱逐像元标注、光谱库比对分类
- 适用性备注：参考光谱需为同网格、反射率域的非负光谱。
- 失败模式：
  - `DATASET_NOT_FOUND` — 输入多波段栅格文件不存在或无法用 GDAL 打开。处置：确认 input 路径存在且为可读栅格
  - `INVALID_PARAMETER` — 必填参数 input/output 缺失，form 取值不在 product_normalized/classic_tan 之内，参考光谱（refs/refsRef/libraryPath）未提供或同时提供多种，或参考波段数与影像波段数不一致且任一侧无波长元数据。处置：补齐必填参数、只通过一种形式提供参考光谱，并使参考波段数与影像一致或补齐波长元数据
  - `INVALID_RADIOMETRY` — 参考光谱为 DN/辐亮度等非反射率量纲或含负波段，SID 的概率分布假设不成立，这些像元只能留空（NaN 分数）而不被强行归类。处置：先做辐射定标与大气校正，提供反射率量纲、非负的参考光谱
  - `WAVELENGTH_INCOMPATIBLE` — 参考光谱与输入影像的波长范围不重叠，或所选输入波段超出参考光谱波长覆盖。处置：选择落在参考覆盖范围内的输入波段，或提供波长匹配的参考光谱
- 教学概念：光谱信息散度（SID）、光谱角（SAM）、混合相似度度量
- 适用课程：高光谱遥感
- 典型练习：调整 SID-SAM 混合形式观察标注变化，并解释负值波段/零范数光谱输出 NaN（未标注）而非强行归类的设计。

