<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 雷达 SAR 处理（sar）

共 23 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:sar_backscatter

SAR 后向散射状态转换：在已定标强度数据上于 sigma0/gamma0/beta0 辐射状态及线性功率/dB 之间转换；不支持 DN 或 SLC 复数据。

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
- 参数：band（integer）、calibrationA（numeric）、calibrationLut（string）、incidenceDeg（numeric）、noiseLinear（numeric）、output（string）、outputDomain（enum）、polarizations（string）、sensor（string）
- 前置条件：SAR amplitude/DN raster; the calibration constant A must match the product convention (Sentinel-1 GRD: per-beam constant from the annotation, simplified here to one constant per run), or a per-row calibration LUT sidecar (one constant per input row).；SAR amplitude/DN raster; the calibration constant A must match the product convention (Sentinel-1 GRD: per-beam constant from the annotation, simplified here to one constant per run).
- 局限：LUT calibration reads a per-row sidecar (one constant per input row, no interpolation); annotation-XML LUTs are not parsed and a missing LUT is a typed refusal, never a constant-A fallback.；LUT-based calibration (per-block/per-pixel annotation LUTs) is not applied; use a constant A or pre-calibrated input.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 处理链第一步、多景 SAR 数据辐射统一
- 失败模式：
  - `CALIBRATION_MISMATCH` — 定标参数（LUT）缺失或与产品不匹配。处置：使用官方 LUT 或确认产品类型参数正确
- 教学概念：SAR 定标、雷达截面
- 适用课程：微波遥感
- 典型练习：定标两景不同日期 Sentinel-1 数据并比较水田后向散射的季节差异。
- 可接下游：rs:sar_change、rs:sar_temporal_stats

## rs:sar_change

SAR 双时相变化检测：比较两景配准 SAR 影像的后向散射幅度差异，探测地表变化（洪涝、倒伏、滑坡引起的幅度变化等；不含 InSAR 相位）。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
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

## rs:sar_coregister

SAR 配准精化：对同网格复 SLC 对做幅度域 patch 归一化互相关（抛物线亚像元精化+中位数稳健估计），估计全局平移并双线性复采样从景。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：master（raster）、slave（raster）
- 输出：dx（string）、dy（string）、output（raster）
- 参数：masterBand（numeric）、minPeakRatio（numeric）、output（string）、patchSize（numeric）、patchStride（numeric）、reportOnly（numeric）、searchRadius（numeric）、slaveBand（numeric）
- 前置条件：Two complex (CFloat32) SLC rasters on the same grid; granular misalignment within the search radius.；两景复 SLC 必须已粗配准到同一网格，残余偏移在 searchRadius 内。
- 局限：Global translation model only — no affine/polynomial warp, no DEM-based or range-Doppler coregistration.；Full planes are materialized behind a 4 GiB budget (MEMORY_BUDGET_EXCEEDED beyond).；仅全局平移模型——不含仿射/多项式/DEM 配准；整平面材料化受 4 GiB 预算约束。
- 适用地物：任意地物（SAR）
- 适用场景：InSAR 前置配准
- 失败模式：
  - `EXECUTION_FAILED` — 置信 patch 少于 3 个。处置：增大 searchRadius 或检查两景是否相关
  - `INSUFFICIENT_MEMORY` — 平面超出预算。处置：缩小 AOI
- 教学概念：影像配准、互相关
- 适用课程：微波遥感
- 典型练习：对人工平移的合成 SLC 对估计平移量并验证恢复精度（亚像元级）。

## rs:sar_coregister_local

SAR 局部配准：在同网格复 SLC 对上估计 patch NCC 偏移场（抛物线亚像元精化+中值滤波），把从景重采样到主景网格，输出偏移场与全局平移。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：master（raster）、slave（raster）
- 输出：offsetFieldOutput（raster）、output（raster）
- 参数：masterBand（numeric）、medianRadius（numeric）、minPeakRatio（numeric）、offsetFieldOutput（string）、output（string）、patchSize（numeric）、patchStride（numeric）、searchRadius（numeric）、slaveBand（numeric）
- 前置条件：Same-grid complex SLC pair (rs:sar_coregister preflight semantics apply).
- 局限：Translation-field model: no affine/polynomial warp and no DEM-based refinement; strong range ramps need a lattice finer than the ramp scale.；Both planes are materialized behind a 2 GiB gate (MEMORY_BUDGET_EXCEEDED beyond — use a smaller AOI).
- 失败模式：
  - `COMPLEX_BANDS_REQUIRED` — master 或 slave 的指定波段不是复数（CFloat32）SLC 波段。处置：输入复 SLC 数据或校正 masterBand/slaveBand 波段号
  - `GRID_MISMATCH` — master 与 slave 不共享 CRS、分辨率、原点与范围，或两景栅格尺寸不同。处置：先对齐为同网格 SLC 对（或换用可以外部对齐的产品）
  - `INSUFFICIENT_MEMORY` — 两景复平面物化超过 2 GiB 预算（约 48 字节/像素）。处置：缩小 AOI 后重试
  - `COREGISTRATION_FAILED` — 置信 patch 少于 3 个——两景去相关严重，或 searchRadius 小于真实配准偏差。处置：增大 searchRadius/patchSize，或检查两景相关性

## rs:sar_displacement

InSAR 形变：把解缠相位按 d_los = −λ·φ/(4π) 转换为视线向形变（米），附带 Itoh 不连续率诊断（输入疑似仍为缠绕相位时告警）。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：input（raster）
- 输出：discontinuityRatio（string）、output（raster）
- 参数：band（numeric）、output（string）、warnThreshold（numeric）、wavelengthUm（numeric）
- 前置条件：Unwrapped phase (rs:sar_unwrap or an external provider); declared or explicit radar wavelength.；输入必须是解缠相位；雷达波长通过 wavelengthUm 参数或场景 SICNU_SAR_WAVELENGTH_UM 声明。
- 局限：Sign convention: positive d_los = motion TOWARD the sensor.；Cannot reliably distinguish wrapped from unwrapped input; the Itoh discontinuity ratio is reported for the caller to judge.；算子无法从数据可靠区分缠绕/解缠——Itoh 不连续率仅供参考告警，判定权在调用方。
- 适用地物：城市（SAR）、农田、矿区
- 适用场景：地表形变定量制图
- 失败模式：
  - `INVALID_PARAMETER` — 波长未声明。处置：传 wavelengthUm（如 Sentinel-1 为 56235）或声明场景元数据
- 教学概念：形变测量、相位-位移转换
- 适用课程：微波遥感
- 典型练习：对已知形变梯度的合成解缠相位验证位移幅值与符号约定（正值朝向传感器）。

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
- 前置条件：The SAR scene must declare the orbit contract (SICNU_SAR_ORBIT_STATES, SICNU_SAR_AZIMUTH_START_UTC, SICNU_SAR_PRF, SICNU_SAR_RANGE_WINDOW, SICNU_SAR_RANGE_RATE) - missing declarations are typed refusals, never approximations.；Calibrate first: rs:sar_calibrate -> rs:sar_geocode. The input must declare SICNU_SAR_CALIBRATION=sigma0 (a legacy undeclared scene is accepted under the documented sigma0 assumption; gamma0/beta0/DN/derived declarations are typed refusals).；DEM carries a CRS and a north-up geotransform; the DEM defines the output grid.；Calibrate first: rs:sar_calibrate -> rs:sar_geocode.；需要覆盖研究区的 DEM；与光学联合分析时统一到同一 CRS。
- 局限：gamma0 applies the per-pixel radiometric-terrain factor sin(thetaL)/sin(theta0) (Ulander 1996, Small 2011 eq. 5) from REAL geometry - distinct from the constant-geometry plane-fit model of rs:sar_terrain_flatten.；The product is mixed: band 1 sigma0 backscatter, band 2 gamma0, bands 3-5 geometry. The dataset-level token names the radiometric input state (sigma0); SICNU_SAR_GEOCODE_BAND_STATES spells out every band.；Rotated DEM grids are refused (terrain-family north-up contract).；No antenna pattern or fading-noise correction is applied.
- 适用地物：任意地物（SAR）
- 适用场景：SAR 与光学数据联合分析前的正射化、多时相 SAR 叠加
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少 DEM 数据。处置：提供 DEM 路径或先准备研究区 DEM
  - `CRS_MISMATCH` — 目标 CRS 参数非法。处置：指定合法的 EPSG 或 WKT 目标坐标系
- 教学概念：Range-Doppler、正射校正、地理编码
- 适用课程：微波遥感
- 典型练习：用 SRTM DEM 对 Sentinel-1 GRD 做地理编码并检查几何精度。

## rs:sar_interferogram

InSAR 干涉图：对同网格配准的复 SLC 对生成 s1·conj(s2) 干涉复栅格（缠绕相位+幅度）与窗口相干性质量层，可选稳健平地多项式去除。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：master（raster）、slave（raster）
- 输出：coherence（string）、output（raster）
- 参数：coherenceOutput（string）、coherenceWindow（numeric）、flattenRamp（enum）、masterBand（numeric）、output（string）、slaveBand（numeric）
- 前置条件：Co-registered complex (CFloat32) SLC pair on the SAME grid (CRS, resolution, origin, extent); use rs:sar_coregister first when the scenes are misaligned.；主/从景必须为 CFloat32 复 SLC 且位于同一网格（CRS/分辨率/原点/范围一致）。
- 局限：Same-grid contract — no hidden resampling.；flattenRamp is a low-order polynomial approximation of the flat-earth phase, not DEM/orbit-based topographic removal.；flattenRamp 是低阶多项式平地近似，不是基于 DEM/轨道的地形相位去除。；不包含大气校正、PSI/SBAS 时序分析——超出基础链的请求不属于本算子能力。
- 适用地物：任意地物（SAR）
- 适用场景：形变监测入口、地表变化检测
- 失败模式：
  - `GRID_MISMATCH` — 主/从景网格不一致。处置：先用 rs:sar_coregister 或外部配准对齐
  - `COMPLEX_BANDS_REQUIRED` — 输入不是复波段。处置：提供复 SLC 产品
- 教学概念：干涉测量、相干性
- 适用课程：微波遥感
- 典型练习：对合成 SLC 对生成干涉图并解释相干性从 1（完全相干）到 0（失相干）的物理含义。

## rs:sar_network_inversion

InSAR 网络反演：小基线线性反演，从连通成对网络的逐对解缠视线向位移栅格求解各历元位移与线性速度场，附拟合 RMS 与缺失数据质量账。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输出：displacementOutput（raster）、rmsOutput（raster）、velocityOutput（raster）
- 参数：displacementInputs（string）、displacementOutput（string）、epochTemporalYears（string）、maskStrategy（enum）、maxPatterns（integer）、pairWeights（string）、pairs（string）、rmsOutput（string）、velocityOutput（string）
- 前置条件：Connected pair network (rs:sar_pair_network) and per-pair unwrapped displacement rasters on one grid.
- 局限：LINEAR small-baseline model: atmospheric phase stays in the epoch displacements — NOT PSI (no PS selection, no APS separation).；Bounded scale: 64 pairs (pattern-mask bound), 200 epochs, 128 missing-data patterns (typed refusals beyond).
- 失败模式：
  - `NETWORK_INVERSION_RANK_DEFICIENT` — pair/epoch 契约无效（每对 master 索引未大于 slave、计数不一致或权重 ≤ 0），或某缺失模式的历元方程组秩亏。处置：校正 pairs 的 [masterEpoch, slaveEpoch]（注意其索引方向与 rs:sar_pair_network 输出相反）并保证权重 > 0
  - `NETWORK_INVERSION_PATTERN_BLOWUP` — 互异缺失数据模式数超过 maxPatterns 缓存上限。处置：改用 maskStrategy=intersect，或在确认代价后调大 maxPatterns
  - `NETWORK_INVERSION_PAIR_LIMIT` — 输入位移栅格（成对数）超过 64 对的模式掩码上限。处置：拆分网络分批反演
  - `NETWORK_INVERSION_EPOCH_LIMIT` — epoch 数超过 200 上限。处置：减少历元数（合并或裁剪场景）后重试

## rs:sar_pair_network

InSAR 成对网络：在重处理前按时间/垂直基线约束从场景真值栈（轨道、波长、采集时刻）筛选干涉对并构建连通性网络，fail-closed 类型化拒绝。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输出：outputFile（json）
- 参数：allowDisconnected（boolean）、maxPerpendicularM（numeric）、maxTemporalDays（numeric）、minPerpendicularM（numeric）、outputFile（string）、referenceIdx（numeric）、scenes（string）、strategy（enum）
- 前置条件：Scene truth stack: orbit states + acquisition UTC + one common wavelength.
- 局限：Screening B⊥ is evaluated at each master's orbit mid-time nadir — a graph metric, not a per-pixel baseline product.；Bounded scale: 512 scenes, 65536 pairs (typed refusals beyond).
- 失败模式：
  - `SCENE_TRUTH_INVALID` — 任一场景真值缺失或非法（缺 acquisitionUtc/wavelengthUm），或场景数超过 512、可用对超过 65536、referenceIdx 越界。处置：补齐各场景的采集时刻、波长与轨道状态，并裁剪网络规模
  - `ORBIT_SEGMENT_INVALID` — 轨道状态段无效（少于 2 个状态、时间非严格升序有限或速度退化）。处置：按 SICNU_SAR_ORBIT_STATES 语法修复该场景的轨道串
  - `WAVELENGTH_INCOMPATIBLE` — 某场景波长与参考场景波长相对偏差超过 1e-9。处置：统一全栈为同一雷达波长（µm）
  - `PAIR_GRAPH_DISCONNECTED` — 约束过滤后图为空，或滤波后的图有多个连通分量且未设 allowDisconnected。处置：放宽 maxTemporalDays 或垂直基线约束，或设 allowDisconnected=true 取分量图做 QA

## rs:sar_phase_filter

InSAR 相位滤波：对复干涉图做 Goldstein-Werner 空间自适应滤波，抑制相位噪声，输出单位相量复栅格。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：input（raster）
- 输出：output（raster）
- 参数：alpha（numeric）、band（numeric）、output（string）、window（numeric）
- 前置条件：Complex interferogram from rs:sar_interferogram.；输入应为 rs:sar_interferogram 输出的复干涉图。
- 局限：Spatial Goldstein-Werner form; the output is a UNIT phasor (magnitude information is deliberately dropped — the filter is defined on phase).；输出为单位相量（模长被有意归一）——滤波定义在相位上，幅度信息不保留。
- 适用地物：任意地物（SAR）
- 适用场景：形变监测链路中间步骤
- 失败模式：
  - `COMPLEX_BANDS_REQUIRED` — 输入不是复波段。处置：提供复干涉图
- 教学概念：相位滤波、Goldstein-Werner
- 适用课程：微波遥感
- 典型练习：对比滤波前后干涉图条纹清晰度，理解 α 参数对平滑强度的影响。

## rs:sar_polsar_decompose

全极化分解：从 HH/HV/VV 复散射通道提取 Pauli 分量、H/A/α 熵-各向异性-平均散射角、Freeman-Durden 三分量或 Yamaguchi 四分量功率，刻画地表散射机制。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：input（raster）
- 输出：bands（string）、output（raster）
- 参数：assumeReciprocity（numeric）、decomposition（enum）、hhBand（numeric）、hvBand（numeric）、output（string）、vhBand（numeric）、vvBand（numeric）、windowSize（numeric）
- 前置条件：Complex CFloat32 HH/HV/VV (reciprocal) channels from a full-pol SLC product; calibrated scattering amplitudes.；必须为 CFloat32 复 HH/HV/VV 全极化通道；dual-pol/detected 输入被拒绝（POLARIZATION_MISMATCH）。
- 局限：Dual-pol detected inputs are refused — no quad-pol approximation from dual-pol data.；Single-look ensembles are rank 1: H is identically 0 and anisotropy is NaN — use windowSize > 1 for H/A/alpha.；Freeman-Durden/Yamaguchi powers may clamp negative residuals to 0 (documented SPAN break near the noise floor).；Cost is O(windowSize^2) per pixel: windowSize=101 is a deliberately expensive ensemble — use the smallest window that decorrelates the speckle.；单视集合秩为 1：H 恒为 0、anisotropy 为 NaN——用 windowSize>1 的窗口平均获得有效 H/A/α，代价是有效分辨率下降。；Freeman-Durden/Yamaguchi 负残差被钳制为 0（噪声底附近 SPAN 不严格守恒）。
- 适用地物：农田、森林、城市（SAR）
- 适用场景：机制识别与分类输入、地表覆盖制图
- 失败模式：
  - `POLARIZATION_MISMATCH` — 输入缺 HH/HV/VV 复通道或声明不完整。处置：确认数据为全极化复产品，或在元数据/参数中声明通道
  - `COMPLEX_BANDS_REQUIRED` — 指定波段不是 CFloat32。处置：使用复 SLC 产品或改用 detected 域算子
- 教学概念：极化分解、Pauli 基、散射熵
- 适用课程：微波遥感
- 典型练习：对全极化数据做 Freeman-Durden 分解，区分表面散射（裸土/水面）与体散射（植被）主导区。

## rs:sar_ratio

SAR 双通道或多时相比值运算：突出散射机制差异，常用于水体/植被/建筑判别。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 波段角色要求：vh×1、vv×1
- 输入：inputA（raster）、inputB（raster）
- 输出：bands（integer）、output（raster）、outputType（string）、radiometricState（string）
- 参数：bandA（integer）、bandB（integer）、inputDomain（enum）、output（string）、outputType（enum）、polarizations（string）、sensor（string）
- 局限：Scenes must share CRS, pixel size, origin and extent; no hidden resampling is applied.；Both inputs must declare the same recognized radiometric state (or nothing); a sigma0/gamma0 mix or a derived input is a typed refusal.；The output is a derived pair metric (SICNU_RADIOMETRIC_STATE=sar_pair_metric), not a backscatter calibration; rs:sar_calibrate and rs:sar_backscatter refuse it.；If either input declares SICNU_SAR_DOMAIN=db and inputDomain is left at linear_power, the operator refuses (pass inputDomain=db to convert, or convert first).；Nonpositive power becomes NoData (NaN) for the log-domain outputs; B == 0 is NoData for ratio.
- 适用地物：水体、植被（SAR）
- 适用场景：快速 SAR 判别图生产、教学演示散射机制差异
- 失败模式：
  - `POLARIZATION_MISMATCH` — 通道缺失或极化不一致。处置：确认两通道均存在且同为定标后数据
- 教学概念：通道比值、散射机制
- 适用课程：微波遥感
- 典型练习：生成 VV/VH 比值图并解释镜面、体散射与二面角区域的取值差异。

## rs:sar_remove_topographic_phase

InSAR 地形相位剔除：用两景轨道的逐像素严格距离差几何，从复干涉图中移除 DEM/轨道地形相位，留下形变与大气相位。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：dem（raster）、interferogram（raster）
- 输出：output（raster）、topoPhaseOutput（raster）
- 参数：band（numeric）、demBand（numeric）、masterOrbitStates（string）、output（string）、slaveOrbitStates（string）、topoPhaseOutput（string）、wavelengthUm（numeric）
- 前置条件：Complex interferogram; DEM above the WGS84 ellipsoid in the interferogram CRS; both scene orbit state vectors; radar wavelength.
- 局限：North-up axis-aligned grids only; DEM must share the interferogram CRS and cover it (warp/clip otherwise).；Height sensitivity degenerates near zero B⊥ — the removal is exact for the given DEM, but pairs without perpendicular baseline carry no height signal to remove.
- 失败模式：
  - `TOPO_PHASE_METADATA_MISSING` — master/slave 轨道状态串无法解析，或雷达波长既无 wavelengthUm 参数也无 SICNU_SAR_WAVELENGTH_UM 元数据。处置：按 SICNU_SAR_ORBIT_STATES 语法补两景轨道状态，并传 wavelengthUm 或在干涉图上声明波长元数据
  - `COMPLEX_BANDS_REQUIRED` — 干涉图的指定 band 不是复（CFloat32）波段。处置：输入复干涉图或校正 band 波段号
  - `DEM_CRS_MISMATCH` — DEM 与干涉图 CRS 不一致（算子不执行隐式重投影）。处置：先把 DEM 重投影到干涉图 CRS/网格再执行
  - `DEM_EXTENT_INSUFFICIENT` — DEM 范围未完整覆盖干涉图像元中心所在范围。处置：扩大 DEM 覆盖范围，或裁剪干涉图 AOI 到 DEM 范围内

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

## rs:sar_temporal_events

SAR 时序事件定年：对多期定标 SAR 影像按像元检测相对中位数基线的超阈值变化，输出事件标志/首末事件/持续天数等，全部带真实获取日期语义（ISO 8601 声明）。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输出：dates（string）、output（raster）
- 参数：band（numeric）、changeThresholdDb（numeric）、dates（string）、inputDomain（enum）、inputs（string）、minValid（numeric）、output（string）
- 前置条件：Scenes co-registered on an identical grid; acquisition dates via the dates parameter or per-scene SICNU_SAR_ACQUISITION_UTC metadata (missing dates are refused — no index-only products).；Calibrate scenes first (rs:sar_calibrate).；每景必须有获取日期（dates 参数或 SICNU_SAR_ACQUISITION_UTC 元数据，严格递增）——缺日期类型化拒绝，不产出无语义索引。
- 局限：Incoherent amplitude analysis only — no interferometric phase.；Dates must be UTC ISO 8601 and strictly ascending.；Pixels under minValid observations are NaN everywhere except valid_count.；非相干幅度分析；不规则间隔是常态（所有时间量按实际天数偏移）。
- 适用地物：农田、水体（SAR）
- 适用场景：作物物候的雷达表达、洪水/倒伏事件定年
- 失败模式：
  - `ACQUISITION_DATES_MISSING` — 景无日期声明且未给 dates 参数。处置：提供 dates 数组或为每景写 SICNU_SAR_ACQUISITION_UTC
  - `DATES_NOT_ASCENDING` — 日期非严格递增。处置：按时间排序场景与日期数组
- 教学概念：时序变化检测、事件定年
- 适用课程：微波遥感
- 典型练习：用不规则重访（12 天/6 天混合）的时序定位农田淹没或倒伏发生日期，理解不规则间隔按实际天数计算。

## rs:sar_temporal_stats

SAR 时序统计：对多期定标后 SAR 影像按像元统计均值/方差/分位数等，刻画散射时序特征。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输出：output（raster）
- 参数：band（numeric）、changeThresholdDb（numeric）、dates（string）、inputDomain（enum）、inputs（string）、minValid（numeric）、output（string）
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

SAR 地形掩膜生成：基于 DEM 与雷达几何把像元标记为叠掩/阴影/Normal（不区分透视收缩），供后续分析剔除不可靠像元。

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
- 教学概念：雷达阴影、叠掩、几何畸变
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

## rs:sar_unwrap

InSAR 相位解缠：内建参考实现（质量引导洪泛、确定性），从缠绕相位恢复绝对相位；提供外部 provider 接口（未注册名称类型化拒绝，绝不静默替代）。

- 确定性：逐位一致（bit_exact）
- 模态：sar
- 输入：input（raster）、qualityInput（raster）
- 输出：output（raster）、unwrappedPixels（string）
- 参数：band（numeric）、output（string）、provider（string）、providerArgs（string）、providerBin（string）、providerTimeoutSec（numeric）、qualityBand（numeric）
- 前置条件：Complex interferogram (ideally filtered via rs:sar_phase_filter); optional coherence raster for quality guidance.；输入应为滤波后的复干涉图（rs:sar_phase_filter）；可选相干性栅格引导种子顺序。
- 局限：Single-scale plane unwrapping: 2 GiB plane budget (MEMORY_BUDGET_EXCEEDED beyond; use a smaller AOI).；Dense residue fields yield wrong 2π branches — no branch-cut/MCF global optimization is claimed.；参考实现不感知残差点、不做全局最优（非 SNAPHU/MCF 语义）；残差密集场会产生错误 2π 分支。；单尺度整平面解缠，2 GiB 预算（超出拒绝 MEMORY_BUDGET_EXCEEDED）——用更小 AOI 或外部 provider。
- 适用地物：任意地物（SAR）
- 适用场景：形变定量前处理
- 失败模式：
  - `UNWRAP_PROVIDER_UNAVAILABLE` — 请求未注册的外部解缠 provider。处置：使用 builtin，或先注册对应外部工具
  - `INSUFFICIENT_MEMORY` — 栅格超出整平面预算。处置：缩小 AOI 或使用外部 provider
- 教学概念：相位解缠、Itoh 条件
- 适用课程：微波遥感
- 典型练习：在合成相位坡面上验证解缠的精确性，并观察残差点密集时的 2π 跳变错误。

