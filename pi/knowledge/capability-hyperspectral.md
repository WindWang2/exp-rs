<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 高光谱分析（hyperspectral）

共 12 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:ace

自适应相干估计（ACE）目标检测：在白化空间度量目标光谱匹配度，对噪声与背景变化稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：background（raster）、input（raster）
- 输出：output（raster）
- 参数：libraryMaterials（string）、libraryPath（string）、output（string）、target（string）、targetRef（string）
- 前置条件：'target' must have one finite value per input band.
- 局限：Pixels with degenerate whitened norm (no variance along any direction) score NaN.
- 适用地物：矿物、人工目标
- 适用场景：高光谱目标检测、小目标搜索
- 失败模式：
  - `INVALID_PARAMETER` — 目标光谱与影像波段数不匹配。处置：重采样目标光谱到影像波段
- 教学概念：ACE、目标检测、白化
- 适用课程：高光谱遥感
- 典型练习：以标布光谱为目标运行 ACE 并评估 ROC 检测性能。

## rs:cem_detection

约束能量最小化（CEM）目标检测：以场景相关矩阵建模背景，无失真约束下目标得分恒为 1，对乘性亮度变化稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：background（raster）、input（raster）
- 输出：output（raster）
- 参数：libraryMaterials（string）、libraryPath（string）、loading（numeric）、output（string）、target（string）、targetRef（string）
- 前置条件：'target' must have one finite value per input band.；At least 2*B+2 valid background pixels (B+1 when 'loading' > 0) — under-sampled scenes are refused.
- 局限：Background statistics come from the input scene by default; 'background' accepts an independent background raster (spectral statistics only — no spatial co-registration required).
- 适用地物：人工目标、自然背景
- 适用场景：高光谱目标检测、亚像素小目标场景
- 失败模式：
  - `INVALID_PARAMETER` — 目标光谱与影像波段数不匹配。处置：将目标光谱重采样到影像波段，保证每个波段对应一个有限值
  - `INVALID_PARAMETER` — 有效背景像元不足（无 loading 需 2B+2，loading>0 需 B+1）。处置：扩大场景范围或增大 loading 对角加载系数
- 教学概念：约束能量最小化、目标检测、背景相关矩阵、投影滤波
- 适用课程：高光谱遥感、目标检测
- 典型练习：在小场景高光谱影像上指定目标光谱运行 CEM，比较不同背景正则强度（loading）下目标得分的稳定性，并解释无失真约束下目标得分恒为 1 的含义。

## rs:continuum_removal

连续统去除：归一化吸收特征深度，突出矿物/植被的吸收峰位置与形状，便于特征比对。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：output（string）
- 前置条件：Input should be reflectance (0..1); DN values give meaningless ratios.
- 局限：输出是相对吸收深度（连续统归一化），不是反射率：只用于吸收特征位置/形状比对，不能回代辐射反演。；凸包拟合对参与波段范围敏感：拟合区间端点选取不当时吸收深度失真。
- 适用地物：矿物、植被
- 适用场景：矿物吸收特征分析、实验室光谱与影像光谱比对
- 失败模式：
  - `INVALID_PARAMETER` — 波段范围设置未覆盖吸收特征。处置：按目标吸收谷设置起止波段
- 教学概念：连续统、吸收深度
- 适用课程：高光谱遥感
- 典型练习：对蚀变矿物光谱做连续统去除并比较 2200nm 吸收深度。

## rs:endmember_extraction

端元提取：用 PPI（像元纯度指数）从影像中抽取数据云角点处的纯净地物光谱，是光谱解混的前置步骤。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：endmembers（string）、indices（string）
- 参数：endmembersOut（string）、nEndmembers（integer）、projections（integer）
- 前置条件：场景必须包含以纯像元形式存在的端元：PPI 抽取的是数据云角点光谱，端元未以纯像元出现时只能得到混合估计。
- 局限：PPI finds pixels at the data hull; it assumes endmembers are present as pure pixels in the scene.
- 适用地物：矿物、植被、土壤
- 适用场景：端元光谱库构建、解混前的端元估计
- 失败模式：
  - `INVALID_PARAMETER` — 端元数设置与场景复杂度不符。处置：按场景主要地物数量设置端元数
- 教学概念：端元、纯像元、单形
- 适用课程：高光谱遥感
- 典型练习：提取研究区 4 个端元并与 USGS 光谱库比对。

## rs:matched_filter

匹配滤波目标检测：以目标光谱与背景协方差构造最优滤波器，最大化目标-背景对比。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：background（raster）、input（raster）
- 输出：output（raster）
- 参数：libraryMaterials（string）、libraryPath（string）、output（string）、target（string）、targetRef（string）
- 前置条件：'target' must have one finite value per input band.
- 局限：Background statistics come from the input scene by default; 'background' accepts an independent background raster (spectral statistics only — no spatial co-registration required).
- 适用地物：矿物、植被胁迫目标
- 适用场景：矿物异常探测、肥料/胁迫高光谱识别
- 失败模式：
  - `NOT_SUPPORTED` — 背景协方差估计样本不足。处置：在足够大的背景区域上估计统计量
- 教学概念：匹配滤波、背景协方差
- 适用课程：高光谱遥感
- 典型练习：对矿区分执行 matched filter 并提取异常得分图。

## rs:mnf

MNF 最小噪声分离变换：按信噪比排序的正交变换，先白化噪声再做 PCA，是高光谱去噪与降维的标准起点。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：numComponents（integer）、output（raster）
- 参数：numComponents（integer）、output（string）、transformOut（string）
- 前置条件：Input should be calibrated reflectance/radiance; wavelength metadata is not required for MNF itself.
- 局限：A numerically singular noise covariance (e.g. constant bands, too few valid pixels) is a typed refusal, not a degraded fit.
- 适用地物：任意地物（高光谱）
- 适用场景：高光谱数据降维、去噪后的波段精简
- 失败模式：
  - `INSUFFICIENT_MEMORY` — 波段数过大导致协方差矩阵内存超限。处置：分块统计或先做波段筛选
- 教学概念：MNF、信噪比、噪声白化
- 适用课程：高光谱遥感
- 典型练习：对 200 波段影像做 MNF 并保留前 20 个高信噪比分量。

## rs:osp_detection

正交子空间投影（OSP）目标检测：将目标光谱投影到已知干扰子空间的正交补空间上逐像元打分，不估计背景统计量（单遍流式），干扰光谱得分恒为 0。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：output（raster）
- 参数：interference（string）、interferenceRef（string）、libraryMaterials（string）、libraryPath（string）、output（string）、target（string）、targetRef（string）
- 前置条件：'target' must have one finite value per input band.；'interference' (or 'interferenceRef') must resolve to at least one finite, non-zero spectrum per input band, linearly independent of the others.
- 局限：A target that lies (numerically) inside the undesired subspace is refused — no filter can suppress the interference and keep the target at the same time.
- 适用地物：人工目标、自然背景
- 适用场景：高光谱目标检测、已知干扰子空间的场景
- 失败模式：
  - `INVALID_PARAMETER` — 缺少干扰光谱或干扰光谱波段数不匹配。处置：通过 interference / interferenceRef 提供至少一个与影像波段数一致的非零干扰光谱
  - `INVALID_PARAMETER` — 干扰光谱线性相关（UᵀU 奇异），或目标投影后能量不足。处置：剔除重复或共线的干扰光谱；确认目标不在干扰子空间内
  - `INVALID_PARAMETER` — 为 OSP 提供了 background 参数。处置：OSP 不消费背景栅格；需要背景驱动检测时使用 rs:tcimf_detection 或 rs:cem_detection
- 教学概念：正交子空间投影（OSP）、干扰子空间、正交补投影、目标检测
- 适用课程：高光谱遥感、目标检测
- 典型练习：给定目标光谱与干扰光谱运行 OSP，验证干扰光谱像元得分恒为 0，并与 CEM 的背景统计路线比较各自的适用条件。

## rs:rx_anomaly

RX 异常检测（Reed-Xiaoli）：以背景统计检测与局部背景显著不同的异常像元，无需先验目标光谱。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：max（numeric）、mean（numeric）、output（raster）
- 参数：output（string）
- 前置条件：输入应为定标后的反射率/辐亮度多波段影像：RX 以全场景为背景统计，场景污染（云/大面积异常）会抬高虚警。
- 局限：Global RX uses the whole scene as background; for local background use a windowed variant.
- 适用地物：任意异常目标
- 适用场景：未知异常目标搜索、安全监测筛查
- 失败模式：
  - `INSUFFICIENT_MEMORY` — 大场景全局协方差计算内存超限。处置：使用局部 RX 或分幅处理
- 教学概念：RX 算法、异常检测、马氏距离
- 适用课程：高光谱遥感
- 典型练习：对港口高光谱影像运行 RX 并定位异常停泊目标。

## rs:spectral_resample

光谱重采样：把高光谱数据按传感器光谱响应函数重采样到多光谱波段设置，或统一不同传感器的波段。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：output（string）、sourceWavelengths（numeric）、wavelengths（numeric）
- 前置条件：Input bands carry WAVELENGTH metadata (product-stacked) or sourceWavelengths is provided.
- 局限：Linear interpolation between band centers; target wavelengths outside the source range yield NaN.
- 适用地物：任意地物（高光谱）
- 适用场景：高光谱→多光谱模拟、跨传感器光谱统一
- 失败模式：
  - `NOT_SUPPORTED` — 缺少目标传感器响应函数。处置：选择内置传感器模板或提供响应函数
- 教学概念：光谱响应函数、波段模拟
- 适用课程：高光谱遥感
- 典型练习：把 EO-1 Hyperion 重采样到 Sentinel-2 波段设置并对比 NDVI。

## rs:spectral_spatial_fuse

光谱—空间融合：对目标检测得分图做局部窗口聚合（mean 均值或 bilateral 保边），抑制孤立单像素虚警，无效像元不参与邻域聚合。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：output（raster）
- 参数：beta（numeric）、method（enum）、output（string）、radius（integer）、sigmaRange（numeric）
- 前置条件：Input must be a single-band score raster.
- 局限：The bilateral method is O(pixels * (2r+1)^2) with no interior cancellation point, like the mean; radius is bounded to [0, 128].
- 适用地物：人工目标
- 适用场景：目标检测后处理、孤立虚警抑制
- 失败模式：
  - `INVALID_PARAMETER` — 输入不是单波段得分栅格。处置：先用 rs:matched_filter/rs:ace/rs:cem_detection 生成单波段得分图
  - `INVALID_PARAMETER` — sigmaRange 非正或非有限值。处置：method='bilateral' 时提供有限的 sigmaRange（得分单位）
- 教学概念：得分图邻域聚合、均值滤波、双边滤波（保边）、虚警抑制
- 适用课程：目标检测、高光谱遥感
- 典型练习：对目标检测得分图分别用 mean 与 bilateral 聚合，比较孤立虚警抑制效果与目标边缘保持的权衡。

## rs:spectral_unmixing

光谱解混：按线性混合模型求解每个像元中各端元的丰度，输出丰度图，服务亚像元定量分析。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：endmembers（integer）、meanError（numeric）、output（raster）
- 参数：bands（integer）、endmembers（string）、endmembersRef（string）、errorOut（string）、libraryMaterials（string）、libraryPath（string）、method（enum）、output（string）
- 前置条件：Endmembers must use the same band order and units as the input raster.；需要端元光谱（rs:endmember_extraction 或外部库）。
- 局限：Abundances are least-squares estimates clipped to [0,1] and renormalized to unit sum (approximate fully constrained unmixing).
- 适用地物：矿物、稀疏植被、城市混合区
- 适用场景：亚像元植被覆盖估计、矿物丰度制图
- 失败模式：
  - `INVALID_PARAMETER` — 端元数与波段数不匹配。处置：保证波段数大于端元数
  - `NOT_SUPPORTED` — 端元光谱缺失。处置：先运行 rs:endmember_extraction 或提供外部端元
- 教学概念：线性混合模型、丰度、和为一约束
- 适用课程：高光谱遥感
- 典型练习：解混得到植被/土壤/水体丰度图并验证丰度和为 1。

## rs:tcimf_detection

目标约束干扰最小化滤波（TCIMF）目标检测：在 CEM 的无失真约束之外，对已知干扰光谱施加精确零约束（干扰得分恒为 0），以场景相关矩阵建模背景。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：background（raster）、input（raster）
- 输出：output（raster）
- 参数：interference（string）、interferenceRef（string）、libraryMaterials（string）、libraryPath（string）、loading（numeric）、output（string）、target（string）、targetRef（string）
- 前置条件：'target' must have one finite value per input band.；At least 2*B+2 valid background pixels (B+1 when 'loading' > 0) — under-sampled scenes are refused.；'interference' (or 'interferenceRef') must resolve to at least one finite, non-zero spectrum per input band.
- 局限：Background statistics come from the input scene by default; 'background' accepts an independent background raster (spectral statistics only — no spatial co-registration required).；Interference spectra must be linearly independent under the background metric; a target inside the interference span is refused.
- 适用地物：人工目标、自然背景
- 适用场景：高光谱目标检测、存在已知干扰光谱的场景
- 失败模式：
  - `INVALID_PARAMETER` — 缺少干扰光谱或干扰光谱波段数不匹配。处置：通过 interference / interferenceRef 提供至少一个与影像波段数一致的非零干扰光谱
  - `INVALID_PARAMETER` — 干扰光谱在背景度量下线性相关，或目标位于干扰子空间内。处置：剔除重复或共线的干扰光谱；确认目标不在干扰张成的子空间内
  - `INVALID_PARAMETER` — 有效背景像元不足（无 loading 需 2B+2，loading>0 需 B+1）。处置：扩大场景范围或增大 loading 对角加载系数
- 教学概念：目标约束干扰最小化滤波（TCIMF）、零约束、干扰光谱、背景相关矩阵
- 适用课程：高光谱遥感、目标检测
- 典型练习：在已知干扰光谱条件下对比 CEM 与 TCIMF：验证 TCIMF 对干扰光谱得分恒为 0 的精确零约束，并讨论目标灵敏度的差异。

