<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 光学预处理（optical）

共 20 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:apply_mask

把掩膜栅格应用到目标影像：掩膜像元处写 nodata，是云/水体/质量掩膜的标准执行算子。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 波段角色要求：mask×1
- 输入：input（raster）、mask（raster）
- 输出：aligned（boolean）、maskedPercent（numeric）、maskedPixels（integer）、output（raster）、totalPixels（integer）
- 参数：align_mask（boolean）、no_data（numeric）、output（string）
- 前置条件：Input and mask share a CRS (same-CRS grid differences are auto-aligned).；Input bands must define a NoData value, or `no_data` must be provided for bands without one.；掩膜与目标影像须同一网格（可用 rs:align 最近邻重采样对齐）。
- 局限：Grid alignment uses nearest-neighbor sampling (appropriate for integer masks); CRS mismatches are not auto-corrected.
- 适用地物：任意地物
- 适用场景：云污染像元剔除、QA 波段解析后的质量过滤
- 失败模式：
  - `GRID_MISMATCH` — 掩膜与影像网格或尺寸不一致。处置：先用 rs:align 对齐掩膜（重采样为最近邻）再应用
  - `INVALID_PARAMETER` — 掩膜值域与期望掩膜值不匹配。处置：确认 mask_value 参数与掩膜栅格的实际编码一致
- 教学概念：掩膜、nodata、像元级过滤
- 适用课程：遥感数字图像处理
- 典型练习：用 QA 波段生成云掩膜并应用到反射率影像，统计被剔除像元比例。
- 可接上游：rs:qa_mask

## rs:atmospheric_correction

通用经验大气校正入口（facade）：按 method 组合 DN→辐亮度→地表反射率的经验校正链（DOS1/DOS2/QUAC），不含辐射传输模型反演。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：airmass（numeric）、band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、method（enum）、output（string）
- 前置条件：按 method 选择前置条件：dos1 适用于含可信暗像元（深水体、阴影）的 DN/TOA 场景；dos2 运行于 TOA 反射率空间（#610，需产品元数据带反射率系数）；quac 无需定标文件但输入应为多波段光学影像。sensor-calibration 路径先执行 rs:dn_to_radiance。
- 局限：Gain/bias are resolved from product metadata (MTL/MTD) when omitted; explicit values always win.
- 适用地物：植被、水体、城市、土壤
- 适用场景：地表反射率反演、植被指数计算前的定量预处理、多时相影像辐射一致性归一化
- 失败模式：
  - `INVALID_RADIOMETRY` — 输入是未定标的 DN 或辐亮度数据。处置：先用 rs:radiometric_calibration 或传感器导入算子得到 TOA 反射率再校正
  - `INVALID_PARAMETER` — method 取值与所选经验方法不符。处置：method 从 dn_to_radiance/dos1/dos2/quac 中选择；需要辐射传输反演时须外接专用工具
- 教学概念：经验大气校正、暗像元法、地表反射率
- 适用课程：遥感数字图像处理、定量遥感基础
- 典型练习：对 L1C 级 Sentinel-2 影像执行大气校正，比较校正前后 NDVI 数值分布的变化。
- 可接上游：rs:sentinel2_import、rs:landsat_import
- 可接下游：rs:spectral_index、rs:ndvi、rs:evi、rs:ndwi、rs:mndwi、rs:ndbi、rs:savi

## rs:atmospheric_dos1

DOS1 暗像元大气校正：假设场景内存在零反射暗像元，估计大气路径辐射，无需大气参数与光谱响应函数。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、output（string）
- 前置条件：场景内必须存在零反射暗像元（深水体、地形阴影等）：路径辐射估计直接来自暗像元 DN，缺少可信暗目标时估计有偏。
- 局限：单景全局暗像元估计：假设整景大气条件均匀，不建模地形效应与邻域效应；这是 DOS1 方法的固有适用范围。
- 适用地物：植被、土壤、水体
- 适用场景：缺少辅助大气参数时的快速校正、历史存档影像的相对辐射归一化
- 失败模式：
  - `NOT_SUPPORTED` — 场景内不存在真正的暗像元（如全部被云覆盖）。处置：先做云掩膜或改用 rs:atmospheric_quac
- 教学概念：暗像元法、路径辐射、经验大气校正
- 适用课程：遥感数字图像处理
- 典型练习：对同一景 Landsat 影像分别执行 DOS1 与 DOS2，比较暗像元估计差异对反射率的影响。

## rs:atmospheric_dos2

DOS2 经验大气校正（DOS + 透射率）：运行于 TOA 反射率空间（#610），需要带反射率系数的产品元数据；输出地表反射率。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：airmass（numeric）、band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、output（string）
- 前置条件：输入必须位于 TOA 反射率空间（#610）：DN 域场景需先经 rs:dn_to_radiance 与反射率定标，产品元数据需携带反射率尺度系数。
- 局限：在 DOS 暗像元基础上加入太阳天顶角透射率项，仍属经验校正：不含气溶胶类型反演、邻域效应与地形辐射校正。
- 适用地物：植被、土壤、水体
- 适用场景：无大气参数的定量预处理、教学演示经验校正与辐射传输校正的差异
- 失败模式：
  - `INVALID_PARAMETER` — 缺少带反射率系数的产品元数据（Landsat MTL REFLECTANCE_MULT/ADD + SUN_ELEVATION 或 Sentinel-2 MTD QUANTIFICATION_VALUE），且未提供 metadata_path、输入旁也无 MTL/MTD。处置：提供 metadata_path 或将 MTL/MTD 放在输入旁；只需辐亮度时改用 rs:dn_to_radiance
  - `INVALID_RADIOMETRY` — 输入不是 TOA 反射率空间（DOS1/DOS2 在 #610 后运行于 TOA 反射率空间）。处置：先用 rs:radiometric_calibration 或传感器导入算子得到 TOA 反射率再校正
  - `NOT_SUPPORTED` — 场景内不存在真正的暗像元（DOS 系列 Chavez 1996 暗像元假设不成立，如全部被云覆盖）。处置：先做云掩膜或改用 rs:atmospheric_quac
- 教学概念：暗像元法、大气透过率、路径辐射
- 适用课程：遥感数字图像处理
- 典型练习：以 DOS2 为基准，评估 DOS1 在高气溶胶场景下的反射率高估。

## rs:atmospheric_quac

QUAC 快速大气校正：从影像自身统计自动估计平均地表反射率与大气参数，近似精度、零配置。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：output（string）
- 前置条件：输入为多波段光学影像即可运行：QUAC 从影像自身统计估计平均地表反射率与大气参数，不读取传感器定标文件。
- 局限：精度为经验近似级（零配置的代价）：参数由场景统计估计、无辐射传输模型约束，估计质量依赖场景内地物光谱多样性。
- 适用地物：植被、土壤、城市
- 适用场景：批量存档影像的快速定量预处理、缺少传感器定标参数时的兜底校正
- 失败模式：
  - `NOT_SUPPORTED` — 影像波段配置超出 QUAC 支持范围。处置：改用 rs:atmospheric_dos1 / rs:atmospheric_dos2
- 教学概念：经验大气校正、辐射传输
- 适用课程：定量遥感基础
- 典型练习：对 MODIS 与 Sentinel-2 影像各执行 QUAC，检查反射率量纲一致性。

## rs:brdf_normalization

用 Ross-Thick + Li-Sparse-Reciprocal 核将遥感反射率从观测太阳/视角几何归一化到参考几何（默认天底视角、太阳不变），使不同视角获取的影像在辐射上可比较。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bandCount（integer）、output（raster）
- 参数：f_geo（numeric）、f_vol（numeric）、output（string）、ref_relative_azimuth（numeric）、ref_view_zenith（numeric）、sun_azimuth（numeric）、sun_zenith（numeric）、view_azimuth（numeric）、view_zenith（numeric）
- 前置条件：Sun and view angles via parameters or SICNU_SUN_* / SICNU_VIEW_* dataset metadata — a missing angle is a typed refusal (use rs:solar_geometry to stamp sun angles).；Per-band kernel weights (f_vol, f_geo); the normalization denominator must stay positive.
- 局限：Single-scene weights cannot be fitted from the scene itself; for angle-less two-date leveling use the BrdfNormalization::PairStatistics::fitCFactor API (mean-preserving c = mean(ref)/mean(target)).；Non-finite pixels pass through as NaN NoData.
- 适用地物：植被、农田、林地
- 适用场景：多时相植被制图、不同观测几何影像的联合分析、多日期堆栈构建前的辐射一致性处理
- 失败模式：
  - `INVALID_PARAMETER` — 太阳或观测角度既未通过 sun_zenith/sun_azimuth/view_zenith/view_azimuth 参数给出，栅格元数据中也无 SICNU_SUN_*/SICNU_VIEW_*。处置：显式传入四个角度参数，或先用 rs:solar_geometry 给栅格打上 SICNU_SUN_* 元数据
  - `INVALID_PARAMETER` — f_vol/f_geo 缺失或既非数值、也非与波段数等长的数值数组；或太阳天顶角≥90°、视角天顶角/方位角越界，使各向异性因子 1+f_vol·k_vol+f_geo·k_geo ≤ 0（非物理）。处置：用标量或逐波段数值数组给出有限核权重；角度取太阳天顶角 [0,90)、视角天顶角 [0,90) 与方位角 [0,360)，保证归一化分母为正
  - `DATASET_NOT_FOUND` — 输入反射率栅格无法打开（路径无效/格式不支持），或为空（宽高或波段数为 0）。处置：检查输入路径与产品完整性，确认为至少含一个波段的反射率栅格
  - `OUTPUT_INVALID` — 输出栅格无法创建、分块写入失败或收尾失败（目标路径不可写、磁盘空间不足）。处置：更换可写输出路径并确认磁盘空间后重跑
- 教学概念：BRDF、核驱动模型、Ross-Thick 核、Li-Sparse-Reciprocal 核、反射各向异性
- 适用课程：定量遥感、遥感物理
- 典型练习：对同一区域两个不同观测几何获取的反射率影像执行 BRDF 归一化，比较多日期堆栈在归一化前后的辐射一致性差异。

## rs:contrast_stretch

对比度拉伸：按百分位或线性截断把影像灰度映射到显示动态范围，属于显示级增强，不改变定量语义。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：clipPercent（numeric）、method（enum）、output（string）、piecewisePoints（string）、stddevK（numeric）
- 前置条件：输入为待增强栅格；按方法给参：percent_clip→clipPercent（默认 2%），stddev→stddevK（默认 2），piecewise→piecewisePoints（≥2 个数值 [in,out] 点对，否则类型化拒绝）。
- 局限：统计量（min-max/百分位/σ/直方图）按波段、以两遍流式计算，声明 NoData 像元同时从统计与输出中掩除。；显示级增强：不改变定量语义，下游定量分析链不要以拉伸后的影像为输入。
- 适用地物：任意地物
- 适用场景：制图出图前的显示优化、 Screenshots 与报告插图的可视化增强
- 适用性备注：仅用于显示，不要对拉伸结果再做定量反演。
- 失败模式：
  - `INVALID_PARAMETER` — 百分位参数越界或 min>=max。处置：检查 percent/clip 参数，保证 0<=min<max<=100
- 教学概念：直方图拉伸、灰度映射
- 适用课程：遥感数字图像处理
- 典型练习：对全色影像分别做 2% 与 5% 百分位拉伸，比较地物细节表现。

## rs:dn_to_radiance

把传感器记录的原始 DN 值转换为辐亮度（radiance），是辐射定标链的第一步。

- 确定性：逐位一致（bit_exact）
- 模态：optical、thermal
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、output（string）
- 前置条件：需要传感器辐射定标参数（增益/偏置）：缺定标元数据的产品需先补齐定标链，或改用不依赖定标文件的经验校正族（quac）。
- 局限：输出为 TOA 辐亮度：仅完成传感器级定标、未做大气校正；地表反射率反演需接 DOS1/DOS2/QUAC 等经验链。
- 适用地物：任意地物
- 适用场景：Landsat/MODIS 等存档数据的定标入口、定量遥感处理链的起点
- 失败模式：
  - `INVALID_RADIOMETRY` — 缺少增益/偏移参数或元数据不完整。处置：在参数中显式给出 gain/offset，或改用 rs:landsat_import 自动解析元数据
- 教学概念：辐射定标、DN、辐亮度
- 适用课程：定量遥感基础
- 典型练习：读取 Landsat 8 MTLP 参数文件，把 DN 影像转换为辐亮度并检查数值范围。
- 可接下游：rs:radiometric_calibration

## rs:fusion_brovey

Brovey 全色锐化：按波段比例加权融合，计算快、色彩保持好，适合快速出图。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 波段角色要求：blue×1、green×1、red×1
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.
- 局限：按波段比例加权注入全色细节：计算快、色彩保持好，但存在光谱失真，适合快速出图而非定量分析。
- 适用地物：城市、农田
- 适用场景：快速可视化产品生产、出版级 RGB 影像制作
- 失败模式：
  - `GRID_MISMATCH` — 输入网格不一致。处置：先用 rs:align 统一网格
- 教学概念：Brovey 变换、全色锐化
- 适用课程：遥感数字图像处理
- 典型练习：对 Sentinel-2 10m 多光谱与 10m 全色重采样影像执行 Brovey 融合。

## rs:fusion_gram_schmidt

Gram-Schmidt 全色锐化：以正交化方法保持光谱信息，光谱保真度高于 Brovey，是定量用途的推荐融合方法。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 波段角色要求：blue×1、green×1、red×1
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.
- 局限：以 Gram-Schmidt 正交化模拟全色波段，光谱保真度高于 Brovey/IHS，是定量用途的推荐融合方法，但仍非无失真融合。
- 适用地物：城市、海岸带、农田
- 适用场景：高保真融合产品、融合后需继续做指数计算的场景
- 失败模式：
  - `GRID_MISMATCH` — 输入网格不一致。处置：先用 rs:align 统一网格
- 教学概念：Gram-Schmidt 正交化、光谱保真
- 适用课程：遥感数字图像处理、定量遥感基础
- 典型练习：比较 GS 与 PCA 融合后 NDWI 的偏差，评估光谱保真。

## rs:fusion_ihs

IHS 全色锐化：RGB→IHS 变换后以全色替换亮度分量，色彩鲜艳但光谱失真较大，仅推荐显示用途。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 波段角色要求：blue×1、green×1、red×1
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.
- 局限：RGB→IHS 后以全色替换亮度分量：色彩鲜艳但光谱失真较大，仅推荐显示用途，不用于定量反演。
- 适用地物：城市
- 适用场景：演示性可视化产品、教学比较不同融合方法的光谱失真
- 失败模式：
  - `GRID_MISMATCH` — 输入网格不一致。处置：先用 rs:align 统一网格
- 教学概念：IHS 彩色变换、全色锐化
- 适用课程：遥感数字图像处理
- 典型练习：量化 IHS 融合前后各波段反射率的偏差，说明其不适合定量分析的原因。

## rs:fusion_linear

线性全色锐化：以线性加权方式注入全色细节，参数可控、行为可预期。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.
- 局限：线性加权注入全色细节：参数可控、行为可预期；融合强度参数同时决定细节注入量与光谱改变量。
- 适用地物：农田、城市
- 适用场景：批量业务化融合生产、融合参数敏感性实验
- 失败模式：
  - `GRID_MISMATCH` — 输入网格不一致。处置：先用 rs:align 统一网格
- 教学概念：线性加权融合
- 适用课程：遥感数字图像处理
- 典型练习：扫描不同加权系数，观察空间细节与光谱失真的权衡。

## rs:fusion_pca

PCA 全色锐化：对多光谱做主成分变换后以全色替换第一主成分，适合波段较多的多光谱数据。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.
- 局限：以第一主成分承载空间结构并被全色替换：适合波段较多的多光谱数据；主成分方向随场景统计变化，跨场景行为不完全一致。
- 适用地物：城市、矿区
- 适用场景：多波段影像融合、融合前后信息量分析
- 失败模式：
  - `GRID_MISMATCH` — 输入网格不一致。处置：先用 rs:align 统一网格
  - `NOT_SUPPORTED` — 输入波段过少导致主成分退化。处置：至少保证 3 个以上多光谱波段
- 教学概念：主成分分析、全色锐化
- 适用课程：遥感数字图像处理
- 典型练习：对 6 波段 Landsat 影像做 PCA 融合，分析 PC1 方差贡献。

## rs:image_enhancement

通用影像增强入口：集成滤波、锐化、直方图均衡等显示级增强算子，按 method 参数选择具体算法。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：band1（integer）、band2（integer）、band3（integer）、clipPercent（numeric）、damping（numeric）、filterType（enum）、kernelSize（integer）、method（enum）、noiseVariance（numeric）、output（string）、sigma（numeric）、speckleType（enum）、stddevK（numeric）、stretchType（enum）、transform（enum）
- 前置条件：先选 method 再给参：只有所选方法（stretch / filter / ratio_ihs transform / SAR speckle 族）的参数会被读取，其余忽略。
- 局限：瓦片流式 O(tile) 内存，输出规模不受栅格大小限制；直方图类 stretch 会先做一次逐波段统计遍历再写出。
- 适用地物：任意地物
- 适用场景：目视解译前的影像优化、教学演示各种增强方法
- 适用性备注：显示级增强会破坏辐射定量语义，定量反演前不要使用。
- 失败模式：
  - `INVALID_PARAMETER` — method 取值不在枚举内。处置：查看 schema 枚举选择支持的增强方法
- 教学概念：空间域增强、直方图均衡、图像锐化
- 适用课程：遥感数字图像处理
- 典型练习：同一影像分别用直方图均衡与拉普拉斯锐化增强，比较道路与建筑边缘的可见性。

## rs:image_fusion

通用全色锐化（pansharpening）入口：把高分辨率全色波段与多光谱波段融合，按 method 选择 Brovey/IHS/GS/PCA 等方法。

- 确定性：逐位一致（bit_exact）
- 模态：multimodal、optical
- 输入：ms（raster）、pan（raster）
- 输出：bands（integer）、method（string）、output（raster）、qualityPassed（boolean）、qualityReport（string）
- 参数：blueIdx（integer）、greenIdx（integer）、method（enum）、msWeights（numeric）、output（string）、panWeight（numeric）、qualityReport（string）、redIdx（integer）
- 前置条件：Pan and MS rasters must be co-registered.；全色与多光谱输入必须几何配准且位于同一网格（ADR 0098）。
- 局限：IHS requires exactly 3 MS bands mapped to R/G/B.
- 适用地物：城市、农田、海岸带
- 适用场景：多光谱影像空间分辨率提升、变化检测前的双时相配准融合
- 失败模式：
  - `GRID_MISMATCH` — 全色与多光谱影像网格或范围不一致。处置：先用 rs:align 对齐两组输入再融合
  - `BAND_ROLE_UNRESOLVED` — 输入缺少全色波段角色标注。处置：通过 SICNU_BAND_ROLE 标注 pan 波段或显式指定波段号
- 教学概念：全色锐化、空间分辨率、光谱保真
- 适用课程：遥感数字图像处理
- 典型练习：用 GS 与 Brovey 两种方法融合 Landsat 全色与多光谱，评估水体光谱保真度。

## rs:qa_mask

解析传感器 QA 波段（如 Landsat QA_PIXEL、Sentinel-2 SCL）生成质量/云掩膜，按位或类别挑选要剔除的像元。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：maskClasses（string）、maskedPercent（numeric）、maskedPixels（integer）、output（raster）、source（string）、totalPixels（integer）
- 参数：bits（integer）、mask（enum）、output（string）、qa_band（integer）、source（enum）
- 前置条件：Input must carry a QA band (Landsat QA_PIXEL or Sentinel-2 SCL) or an explicit qa_band.
- 局限：Sentinel-2 cloud shadow interpretation uses the SCL class only (no probability thresholds).
- 适用地物：任意地物
- 适用场景：Landsat/Sentinel 时序生产的去云步骤、质量筛选自动化
- 失败模式：
  - `NOT_SUPPORTED` — QA 波段编码与所选传感器模板不符。处置：确认传感器类型参数与实际数据一致
  - `BAND_ROLE_UNRESOLVED` — 输入缺少 QA 波段角色标注。处置：用 SICNU_BAND_ROLE 标注 qa 波段或显式指定波段号
- 教学概念：QA 位标志、云检测、像元质量
- 适用课程：遥感数字图像处理
- 典型练习：从 Landsat QA_PIXEL 提取云与云影标志，生成掩膜并统计云覆盖率。
- 可接下游：rs:apply_mask

## rs:radiometric_calibration

通用辐射定标：按传感器参数把 DN 转换为辐亮度或 TOA 反射率，统一多源数据的辐射量纲。

- 确定性：逐位一致（bit_exact）
- 模态：optical、thermal
- 输入：input（raster）
- 输出：bandCount（integer）、output（raster）、unit（string）
- 参数：bands（integer）、metadata_path（string）、output（string）、unit（enum）
- 前置条件：需要传感器定标参数（增益/偏置；TOA 反射率还需太阳角度与距离因子）：参数缺失时以类型化错误拒绝，不臆测定标系数。
- 局限：Coefficients are read from MTL/MTD metadata; provide metadata_path for stacked rasters without embedded coefficients.；Brightness temperature requires thermal-band K1/K2 constants.
- 适用地物：任意地物
- 适用场景：多源影像的辐射统一、时间序列分析前的定标准备
- 失败模式：
  - `INVALID_RADIOMETRY` — 缺少定标参数（增益/偏移/太阳高度角）。处置：补全元数据或在参数中显式提供定标系数
- 教学概念：辐射定标、TOA 反射率、表观反射率
- 适用课程：定量遥感基础
- 典型练习：把两景不同日期的影像定标到 TOA 反射率，比较季节光照差异。
- 可接上游：rs:dn_to_radiance

## rs:radiometric_qa

为反射率栅格的每个波段逐像素导出 uint16 质量标志（饱和、负值、超范围、非有限值、云/云影/雪、QA_RADSAT 饱和位），多源标志取并集，0 表示干净像元，并给出汇总报告。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：cloud_mask（raster）、input（raster）
- 输出：bandCount（integer）、output（raster）
- 参数：mask_flag（enum）、output（string）、qa_radsat_band（integer）、qa_radsat_bits（string）、saturation_level（numeric）
- 前置条件：The cloud mask must share the input grid (CRS, geotransform, size); mismatched grids are refused, never resampled.
- 局限：Flags describe the delivered values, not the sensor's full quality model; pair with QaMask on QA_PIXEL/SCL for the complete classification.；A QA_RADSAT band given via qa_radsat_band also receives its own reflectance-domain flag band; consumers should ignore the flag band of the QA band itself.
- 适用地物：含云/雪/饱和场景的质检
- 适用场景：多源反射率产品的统一质量层生产、时序合成前的像元级质检
- 适用性备注：输出 uint16 质量标志图层（0 = 干净像元），多源标志取并集。
- 失败模式：
  - `INVALID_PARAMETER` — 缺少必需参数 input/output，或 qa_radsat_band 为负、超过输入波段数，或 qa_radsat_bits 条目数与输入波段数不符、取值超出 [0, 65535]。处置：补齐 input/output；qa_radsat_band 用 0（关闭）或 1..波段数；qa_radsat_bits 按输入波段数给出逗号分隔的 [0, 65535] 掩码
  - `GRID_MISMATCH` — cloud_mask 与输入栅格的 CRS、地理变换或尺寸不一致（不做重采样）。处置：提供与输入同网格的掩膜，或先用 rs:resample 对齐到同一网格
  - `DATASET_NOT_FOUND` — 输入反射率栅格或 cloud_mask 无法打开（路径无效/格式不支持），或输入为空（宽高或波段数为 0）。处置：检查输入与掩膜路径、驱动及文件权限，确认输入为含至少一个波段的反射率栅格
  - `OUTPUT_INVALID` — 输出标志栅格无法创建、分块写入失败或收尾失败（目标路径不可写、磁盘空间不足）。处置：更换可写输出路径并确认磁盘空间后重跑
- 教学概念：辐射质量标志、饱和与超范围、QA_RADSAT 位、像元级 QC
- 适用课程：遥感数字图像处理、定量遥感
- 典型练习：对导入后的反射率产品生成质量标志层，统计各标志位占比并在时序合成前解释掩膜策略。

## rs:solar_geometry

由成像日期/UTC 时间与场景中心经纬度，按 Spencer 1971 / NOAA 公式计算太阳高度角、方位角、赤纬、日地距离与平方反比因子，可选把 SICNU_SUN_* 元数据写回输入栅格供定标/BRDF 使用。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：earth_sun_factor（numeric）、sun_azimuth（numeric）、sun_elevation（numeric）
- 参数：date（string）、latitude（numeric）、longitude（numeric）、utc_time（string）、write_metadata（boolean）
- 前置条件：Acquisition date and UTC time (e.g. Landsat MTL DATE_ACQUIRED + SCENE_CENTER_TIME) and the scene centre latitude/longitude.
- 局限：Below-horizon suns are still stamped for traceability; downstream operators refuse to calibrate with them (sun_above_horizon=false and elevation <= 0 in the record).；In-place metadata update requires a writable raster; read-only sources are refused with a typed error.
- 适用地物：任意光学处理链
- 适用场景：辐射定标前的太阳几何计算、BRDF 归一化的角度输入准备
- 适用性备注：可把 SICNU_SUN_* 元数据写回输入栅格供定标/BRDF 链使用。
- 失败模式：
  - `INVALID_PARAMETER` — 缺少必需参数 date/utc_time，或 date 不是合法 ISO YYYY-MM-DD、utc_time 不是 HH:mm 或 HH:mm:ss。处置：按 schema 提供成像日期与 UTC 时间（如 Landsat MTL 的 DATE_ACQUIRED + SCENE_CENTER_TIME）
  - `INVALID_PARAMETER` — latitude/longitude 缺失，或超出 [-90, 90] / [-180, 180] 度范围。处置：提供场景中心经纬度并保证落在合法范围内
  - `INVALID_PARAMETER` — write_metadata=true 但未提供 input 栅格。处置：补上要写元数据的 input 栅格，或将 write_metadata 置为 false 只取计算结果
  - `NOT_SUPPORTED` — 以 GA_Update 打开 input 栅格失败（只读源或无写权限），无法原地写入 SICNU_SUN_* 元数据。处置：改用可写副本后重试，或将 write_metadata 置为 false
- 教学概念：太阳高度角/方位角、太阳赤纬、日地距离、平方反比因子、Spencer 公式
- 适用课程：遥感物理、定量遥感
- 典型练习：由成像日期/UTC 时间与场景中心经纬度计算太阳几何并写回元数据，追踪 rs:dn_to_radiance 与 rs:brdf_normalization 如何消费这些角度。

## rs:topographic_correction

地形辐射校正：利用 DEM 与太阳几何消除坡面光照差异，恢复山地地表的真实反射率。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：dem（raster）、input（raster）
- 输出：bandCount（integer）、height（integer）、method（string）、output（raster）、width（integer）
- 参数：method（enum）、output（string）、solar_azimuth（numeric）、solar_zenith（numeric）
- 前置条件：DEM must be on the same grid as the input (CRS, geotransform, size) — mismatched grids are refused, never resampled.；Solar zenith/azimuth of the scene must be supplied explicitly (import metadata does not carry them yet).；DEM 与影像须覆盖同一区域且波段含太阳角度信息；推荐先 rs:align 统一网格。
- 局限：Self-shadowed pixels (cos_i <= 0, or cos_i + c <= 0 for the C model) and Minnaert-domain violations become NaN NoData.；The empirical C factor requires a usable radiance~illumination regression (|b| >= 1e-6, >= 2 valid pairs); otherwise the operator refuses with a typed error instead of passing data through.；c_correction is also the SCS+C form (Soenen et al. 2005) when c is estimated from the same radiance~illumination regression, as done here.
- 适用地物：山地植被、林地、梯田
- 适用场景：山区遥感定量分析、山地植被变化监测
- 失败模式：
  - `INVALID_PARAMETER` — 输入 DEM 与影像网格或范围不一致。处置：先用 rs:align 把 DEM 与影像对齐到同一网格
  - `CRS_MISMATCH` — DEM 与影像坐标系不同。处置：对 DEM 或影像做重投影后再执行地形校正
- 教学概念：地形效应、余弦校正、C 校正、光照系数
- 适用课程：定量遥感基础、山地遥感专题
- 典型练习：在山区 DEM 上分别执行 C 校正与 SCS 校正，比较阴坡 NDVI 的恢复程度。

