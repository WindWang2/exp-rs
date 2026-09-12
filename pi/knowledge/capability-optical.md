<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 光学预处理（optical）

共 17 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

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

基于辐射传输模型的大气校正，把大气顶层（TOA）反射率转换为地表反射率，是多光谱定量分析的标准前置步骤。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：airmass（numeric）、band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、method（enum）、output（string）
- 局限：Gain/bias are resolved from product metadata (MTL/MTD) when omitted; explicit values always win.
- 适用地物：植被、水体、城市、土壤
- 适用场景：地表反射率反演、植被指数计算前的定量预处理、多时相影像辐射一致性归一化
- 失败模式：
  - `INVALID_RADIOMETRY` — 输入是未定标的 DN 或辐亮度数据。处置：先用 rs:radiometric_calibration 或传感器导入算子得到 TOA 反射率再校正
  - `NOT_SUPPORTED` — 无法识别传感器光谱响应，无法选择大气模型。处置：改用经验方法 rs:atmospheric_dos1 / rs:atmospheric_dos2 或 rs:atmospheric_quac
- 教学概念：大气散射、辐射传输模型、地表反射率
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
- 适用地物：植被、土壤、水体
- 适用场景：缺少辅助大气参数时的快速校正、历史存档影像的相对辐射归一化
- 失败模式：
  - `NOT_SUPPORTED` — 场景内不存在真正的暗像元（如全部被云覆盖）。处置：先做云掩膜或改用 rs:atmospheric_quac
- 教学概念：暗像元法、路径辐射、经验大气校正
- 适用课程：遥感数字图像处理
- 典型练习：对同一景 Landsat 影像分别执行 DOS1 与 DOS2，比较暗像元估计差异对反射率的影响。

## rs:atmospheric_dos2

DOS2 暗像元校正的改进版，在路径辐射估计中考虑大气透过率项，精度高于 DOS1，同样不需要外部大气参数。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：band（integer）、method（string）、output（raster）
- 参数：airmass（numeric）、band（integer）、bias（numeric）、gain（numeric）、metadata_path（string）、output（string）
- 适用地物：植被、土壤、水体
- 适用场景：无大气参数的定量预处理、教学演示经验校正与辐射传输校正的差异
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
- 适用地物：植被、土壤、城市
- 适用场景：批量存档影像的快速定量预处理、缺少传感器定标参数时的兜底校正
- 失败模式：
  - `NOT_SUPPORTED` — 影像波段配置超出 QUAC 支持范围。处置：改用 rs:atmospheric_dos1 / rs:atmospheric_dos2
- 教学概念：经验大气校正、辐射传输
- 适用课程：定量遥感基础
- 典型练习：对 MODIS 与 Sentinel-2 影像各执行 QUAC，检查反射率量纲一致性。

## rs:contrast_stretch

对比度拉伸：按百分位或线性截断把影像灰度映射到显示动态范围，属于显示级增强，不改变定量语义。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：clipPercent（numeric）、method（enum）、output（string）、piecewisePoints（string）、stddevK（numeric）
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
- 输出：bands（integer）、method（string）、output（raster）
- 参数：blueIdx（integer）、greenIdx（integer）、method（enum）、msWeights（numeric）、output（string）、panWeight（numeric）、redIdx（integer）
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
- 局限：Coefficients are read from MTL/MTD metadata; provide metadata_path for stacked rasters without embedded coefficients.；Brightness temperature requires thermal-band K1/K2 constants.
- 适用地物：任意地物
- 适用场景：多源影像的辐射统一、时间序列分析前的定标准备
- 失败模式：
  - `INVALID_RADIOMETRY` — 缺少定标参数（增益/偏移/太阳高度角）。处置：补全元数据或在参数中显式提供定标系数
- 教学概念：辐射定标、TOA 反射率、表观反射率
- 适用课程：定量遥感基础
- 典型练习：把两景不同日期的影像定标到 TOA 反射率，比较季节光照差异。
- 可接上游：rs:dn_to_radiance

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

