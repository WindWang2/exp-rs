<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 雷达 SAR 处理（sar）

共 12 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:sar_backscatter

SAR 后向散射系数计算：从 SLC/GRD 复数据或定标数据生成 sigma0/gamma0 后向散射系数栅格。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：incidenceRaster（raster）、input（raster）
- 输出：bands（integer）、calibration（string）、domain（string）、output（raster）
- 参数：band（integer）、fromCalibration（enum）、incidenceDeg（numeric）、inputDomain（enum）、output（string）、outputDomain（enum）、polarizations（string）、sensor（string）、toCalibration（enum）
- 局限：beta0 needs the incidence angle from geometry: set incidenceDeg > 0 or provide incidenceRaster.；DN input is unsupported; calibrate first with rs:sar_calibrate.；inputDomain=db cannot be combined with a calibration-state conversion; convert the numeric domain in a separate step.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 定量分析入口、土壤水分与作物监测的输入
- 失败模式：
  - `POLARIZATION_MISMATCH` — 请求的极化通道在数据中不存在。处置：确认数据含 VV/VH 通道或改用存在的极化
  - `INVALID_RADIOMETRY` — 输入未定标导致 sigma0 量纲错误。处置：先用 rs:sar_calibrate 完成辐射定标
- 教学概念：后向散射、sigma0、gamma0
- 适用课程：微波遥感
- 典型练习：对 Sentinel-1 GRD 计算 gamma0 并比较农田与水体的散射差异。

## rs:sar_calibrate

SAR 辐射定标：把 SAR L1 数据的 DN 转换为定标后向散射系数（sigma0/gamma0/beta0）。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：input（raster）
- 输出：bands（integer）、calibration（string）、domain（string）、output（raster）
- 参数：band（integer）、calibrationA（numeric）、incidenceDeg（numeric）、noiseLinear（numeric）、output（string）、outputDomain（enum）、polarizations（string）、sensor（string）
- 前置条件：SAR amplitude/DN raster; the calibration constant A must match the product convention (Sentinel-1 GRD: per-beam constant from the annotation, simplified here to one constant per run).
- 局限：LUT-based calibration (per-block/per-pixel annotation LUTs) is not applied; use a constant A or pre-calibrated input.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 处理链第一步、多景 SAR 数据辐射统一
- 失败模式：
  - `CALIBRATION_MISMATCH` — 定标参数（LUT）缺失或与产品不匹配。处置：使用官方 LUT 或确认产品类型参数正确
- 教学概念：SAR 定标、雷达截面
- 适用课程：微波遥感
- 典型练习：定标两景不同日期 Sentinel-1 数据并比较水田后向散射的季节差异。
- 可接下游：rs:sar_change、rs:sar_temporal_stats

## rs:sar_change

SAR 双时相变化检测：比较两景配准 SAR 影像的后向散射差异，探测地表变化（洪涝、倒伏、形变前兆等）。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：sar
- 输入：inputA（raster）、inputB（raster）
- 输出：changedPercent（numeric）、changedPixels（integer）、evaluatedPixels（integer）、magnitudeDomain（string）、output（raster）、thresholdUsed（numeric）
- 参数：bandA（integer）、bandB（integer）、cleanup（enum）、cleanupIterations（integer）、inputDomain（enum）、minAreaPixels（integer）、output（string）、percentile（numeric）、polarizations（string）、sensor（string）、statisticalK（numeric）、threshold（numeric）、thresholdMethod（enum）
- 前置条件：两期 SAR 影像须同极化、同网格；建议先 rs:sar_speckle 滤波。
- 局限：Incoherent change only — no coherent/interferometric phase analysis.；Scenes must share CRS, pixel size, origin and extent; no hidden resampling is applied.；If either input declares SICNU_SAR_DOMAIN=db and inputDomain is left at linear_power, the operator refuses (pass inputDomain=db to convert, or convert first).
- 适用地物：水体、农田、建成区（SAR）
- 适用场景：洪涝范围快速制图、农作物倒伏监测
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先用 rs:align 或 rs:sar_geocode 配准到同一网格
  - `TIME_ORDER_INVALID` — before/after 时相顺序颠倒。处置：确认 before 早于 after
- 教学概念：SAR 变化检测、后向散射差异
- 适用课程：微波遥感
- 典型练习：对洪水前后两景 Sentinel-1 计算比值变化图并提取淹没区。
- 可接上游：rs:sar_calibrate、rs:sar_speckle

## rs:sar_dualpol_features

双极化特征提取：从 VV/VH 双通道计算比值、极化分解量等特征，服务作物分类与地表类型识别。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 波段角色要求：vh×1、vv×1
- 输入：input（raster）
- 输出：feature（string）、output（raster）
- 参数：domain（enum）、feature（enum）、output（string）、vh_band（integer）、vv_band（integer）
- 前置条件：Input should be radiometrically calibrated backscatter (rs:sar_calibrate).
- 局限：rvi is the dual-pol Sentinel-1 approximation 4VH/(VV+VH), NOT the quad-pol RVI (needs a second cross-pol channel this platform does not model).；Nonpositive linear-power values are outside the SAR domain and yield NaN, never clamped.
- 适用地物：农田、植被（SAR）
- 适用场景：雷达植被指数生产、SAR 分类特征工程
- 失败模式：
  - `POLARIZATION_MISMATCH` — 数据缺少双极化通道之一。处置：确认输入包含 VV 与 VH 两个通道
- 教学概念：极化比、双极化分解
- 适用课程：微波遥感
- 典型练习：计算 VH/VV 比值图并分析其在水稻生育期中的变化。

## rs:sar_geocode

SAR 地理编码（Range-Doppler 正射校正）：把 SAR 影像从斜距/地距坐标系重投影到地图坐标系，并做地形辐射校正。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：dem（raster）、input（raster）
- 输出：output（raster）
- 参数：band（numeric）、output（string）、resampling（enum）
- 前置条件：The SAR scene must declare the orbit contract (SICNU_SAR_ORBIT_STATES, SICNU_SAR_AZIMUTH_START_UTC, SICNU_SAR_PRF, SICNU_SAR_RANGE_WINDOW, SICNU_SAR_RANGE_RATE) - missing declarations are typed refusals, never approximations.；Calibrate first: rs:sar_calibrate -> rs:sar_geocode.；DEM carries a CRS and a north-up geotransform; the DEM defines the output grid.；需要覆盖研究区的 DEM；与光学联合分析时统一到同一 CRS。
- 局限：gamma0 applies the per-pixel radiometric-terrain factor sin(thetaL)/sin(theta0) (Ulander 1996, Small 2011 eq. 5) from REAL geometry - distinct from the constant-geometry plane-fit model of rs:sar_terrain_flatten.；Rotated DEM grids are refused (terrain-family north-up contract).；No antenna pattern or fading-noise correction is applied.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 与光学数据联合分析前的正射化、多时相 SAR 叠加
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少 DEM 数据。处置：提供 DEM 路径或先准备研究区 DEM
  - `CRS_MISMATCH` — 目标 CRS 参数非法。处置：指定合法的 EPSG 或 WKT 目标坐标系
- 教学概念：Range-Doppler、正射校正、地理编码
- 适用课程：微波遥感
- 典型练习：用 SRTM DEM 对 Sentinel-1 GRD 做地理编码并检查几何精度。

## rs:sar_ratio

SAR 双通道或多时相比值运算：突出散射机制差异，常用于水体/植被/建筑判别。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 波段角色要求：vh×1、vv×1
- 输入：inputA（raster）、inputB（raster）
- 输出：bands（integer）、output（raster）、outputType（string）
- 参数：bandA（integer）、bandB（integer）、inputDomain（enum）、output（string）、outputType（enum）、polarizations（string）、sensor（string）
- 局限：Scenes must share CRS, pixel size, origin and extent; no hidden resampling is applied.；If either input declares SICNU_SAR_DOMAIN=db and inputDomain is left at linear_power, the operator refuses (pass inputDomain=db to convert, or convert first).；Nonpositive power becomes NoData (NaN) for the log-domain outputs; B == 0 is NoData for ratio.
- 适用地物：水体、植被（SAR）
- 适用场景：快速 SAR 判别图生产、教学演示散射机制差异
- 失败模式：
  - `POLARIZATION_MISMATCH` — 通道缺失或极化不一致。处置：确认两通道均存在且同为定标后数据
- 教学概念：通道比值、散射机制
- 适用课程：微波遥感
- 典型练习：生成 VV/VH 比值图并解释镜面、体散射与二面角区域的取值差异。

## rs:sar_speckle

SAR 斑点噪声抑制：提供 Refine-Lee 等自适应滤波，保持边缘的同时压制相干斑点。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：sar
- 输入：input（raster）
- 输出：bands（integer）、kernelSize（integer）、method（string）、output（raster）
- 参数：band（integer）、companionScenes（string）、dampingFactor（numeric）、deviationK（numeric）、kernelSize（integer）、looks（integer）、method（enum）、noiseVariance（numeric）、output（string）、polarizations（string）、sensor（string）
- 前置条件：SAR intensity raster; calibrate first (rs:sar_calibrate) so filter statistics operate on physically scaled data.
- 局限：The multitemporal gate rejects companion pixels deviating from the reference by more than k · localStd (deviationK); gated-out pixels fall back to the temporal mean of the accepted scenes.；All filters assume intensity (power) data, not amplitude or dB.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 变化检测与分类前的必要滤波、图斑边界保持的平滑处理
- 失败模式：
  - `INVALID_PARAMETER` — 滤波窗口过小或过大。处置：窗口取 3–11 的奇数
- 教学概念：相干斑点、自适应滤波、Refine-Lee
- 适用课程：微波遥感
- 典型练习：比较 3x3 与 7x7 Refine-Lee 滤波对田块边缘保持的效果。
- 可接下游：rs:sar_change

## rs:sar_temporal_stats

SAR 时序统计：对多期定标后 SAR 影像按像元统计均值/方差/分位数等，刻画散射时序特征。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输出：output（raster）
- 参数：band（numeric）、changeThresholdDb（numeric）、inputDomain（enum）、inputs（string）、minValid（numeric）、output（string）
- 前置条件：Scenes must be co-registered on an identical grid (no hidden resampling).；Calibrate scenes first (rs:sar_calibrate): statistics of raw DN are not physical.；所有输入须定标并配准到同一网格。
- 局限：Incoherent analysis only — no interferometric coherence.；Radiometric normalization between scenes (incidence/season) is the caller's responsibility.；Valid samples are finite and strictly positive; nonpositive power is NoData.
- 适用地物：农田、水体（SAR）
- 适用场景：作物物候的雷达表达、永久散射体预筛选
- 失败模式：
  - `GRID_MISMATCH` — 多期影像网格不一致。处置：逐期 rs:sar_geocode + rs:align 后再做时序统计
- 教学概念：时序统计、后向散射时序
- 适用课程：微波遥感
- 典型练习：对全年 12 期 Sentinel-1 计算均值与标准差，区分稳定水体与波动农田。
- 可接上游：rs:sar_calibrate

## rs:sar_terrain_correction

SAR 地形校正：消除坡度坡向对后向散射的几何畸变（叠掩/阴影/透视收缩的辐射影响）。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：dem（raster）、input（raster）
- 输出：bands（integer）、calibration（string）、incidenceBand（integer）、layout（string）、maskBand（integer）、output（raster）
- 参数：band（integer）、demUnit（enum）、flagIncidence（boolean）、flagMask（boolean）、headingDeg（numeric）、incidenceDeg（numeric）、lookAzimuthDeg（numeric）、lookDirection（enum）、output（string）、polarizations（string）、sensor（string）
- 前置条件：sigma0 raster (linear power) and a DEM on the exact same grid (radar geometry for GRD products), plus the scene incidence angle and platform heading.；需要 DEM；建议在 rs:sar_geocode 之后执行。
- 局限：Plane-fit RTC model, NOT range-Doppler terrain correction.；The DEM must be co-registered with the input in radar geometry; no resampling is performed.；Facets with a local incidence angle >= 85° are masked as layover/shadow.；The output includes the layover/shadow validity mask and the local incidence angle band (both optional via flagMask/flagIncidence).
- 适用地物：山地（SAR）
- 适用场景：山区 SAR 定量分析、SAR 与光学联合制图
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少 DEM。处置：提供与研究区匹配的 DEM
- 教学概念：地形辐射校正、局部入射角
- 适用课程：微波遥感
- 典型练习：在山区比较地形校正前后 gamma0 的坡向依赖性。

## rs:sar_terrain_flatten

SAR 地形辐射平坦化（Gamma Flat / terrain flattening）：以实际散射面积归一化后向散射，使山地 gamma0 接近真实地表。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：dem（raster）、input（raster）
- 输出：bands（integer）、calibration（string）、demUnit（string）、headingDeg（numeric）、incidenceDeg（numeric）、layout（string）、lookAzimuthDeg（numeric）、maskBand（integer）、output（raster）
- 参数：band（integer）、demUnit（enum）、headingDeg（numeric）、incidenceDeg（numeric）、lookAzimuthDeg（numeric）、lookDirection（enum）、output（string）、polarizations（string）、sensor（string）
- 前置条件：sigma0 raster (linear power) and a DEM on the exact same grid (radar geometry for GRD products), plus the scene incidence angle and the antenna look azimuth (headingDeg + lookDirection, or an explicit lookAzimuthDeg).；需要 DEM；在定标（rs:sar_calibrate）之后执行。
- 局限：Plane-fit RTC model, NOT range-Doppler terrain correction.；The DEM must be co-registered with the input in radar geometry; no resampling is performed.；Facets with a local incidence angle >= 85° are masked as layover/shadow.；Always writes two bands (gamma0 + validity mask). rs:sar_terrain_correction is the 3-band product (gamma0 + mask + local incidence).
- 适用地物：山地植被（SAR）
- 适用场景：山区森林制图、大区域 SAR 时序生产的标准步骤
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少 DEM。处置：提供 DEM 后重试
- 教学概念：地形平坦化、实际散射面积
- 适用课程：微波遥感
- 典型练习：对山区 Sentinel-1 执行 terrain flattening 并评估坡面gamma0的稳定性。

## rs:sar_terrain_masks

SAR 地形掩膜生成：基于 DEM 与雷达几何标记叠掩、阴影与透视收缩区域，供后续分析剔除不可靠像元。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：dem（raster）
- 输出：output（raster）、product（string）
- 参数：heading（numeric）、incidence（numeric）、lookAzimuth（numeric）、lookAzimuthDeg（numeric）、lookDirection（enum）、look_azimuth（numeric）、output（string）、product（enum）
- 前置条件：DEM on any grid; the DEM defines the geometry, no co-registered SAR input is required.
- 局限：Constant-geometry approximation: per-scene incidence/heading, not per-pixel orbit geometry.；The radiometric terrain factor cos(theta_i)/cos(theta_l) is exposed via the kernel for providers, not written by this operator (use rs:sar_terrain_flatten for normalized backscatter).
- 适用地物：山地（SAR）
- 适用场景：山区 SAR 数据质量评估、时序 SAR 像元可靠性筛选
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少 DEM 或入射角信息。处置：提供 DEM 与轨道入射角参数
- 教学概念：雷达阴影、叠掩、透视收缩
- 适用课程：微波遥感
- 典型练习：生成山区雷达阴影掩膜并统计不可信像元比例。

## rs:sar_texture

SAR 纹理特征提取：基于 GLCM 等计算方差/对比度/熵等纹理量，补充后向散射之外的地表信息。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：sar
- 输入：input（raster）
- 输出：bands（integer）、measures（string）、output（raster）
- 参数：band（integer）、directionDeg（enum）、displacement（integer）、measures（string）、output（string）、polarizations（string）、quantLevels（integer）、sensor（string）、windowSize（integer）
- 前置条件：Calibrated SAR intensity raster (rs:sar_calibrate first) so window statistics operate on physically scaled data.；Each window is quantized to quantLevels equal-width bins spanning the window's [min, max].
- 局限：Windows containing NoData produce NaN in every measure.；Quantization is equal-width per window, so measures are not directly comparable across windows with different dynamic ranges.
- 适用地物：城市、农田、森林（SAR）
- 适用场景：SAR 分类特征增强、建筑区提取
- 失败模式：
  - `INVALID_PARAMETER` — 窗口或位移参数非法。处置：窗口取奇数且大于 1
  - `INSUFFICIENT_MEMORY` — 超大影像 GLCM 全图计算内存超限。处置：分幅处理或减小窗口
- 教学概念：GLCM、纹理特征
- 适用课程：微波遥感
- 典型练习：提取 8 邻域 GLCM 对比度与熵，评估其对城中村边界的刻画能力。

