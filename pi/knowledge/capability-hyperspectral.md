<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 高光谱分析（hyperspectral）

共 8 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:ace

自适应相干估计（ACE）目标检测：在白化空间度量目标光谱匹配度，对噪声与背景变化稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：output（raster）
- 参数：output（string）、target（numeric）
- 前置条件：'target' must have one finite value per input band.
- 局限：Pixels with degenerate whitened norm (no variance along any direction) score NaN.
- 适用地物：矿物、人工目标
- 适用场景：高光谱目标检测、小目标搜索
- 失败模式：
  - `INVALID_PARAMETER` — 目标光谱与影像波段数不匹配。处置：重采样目标光谱到影像波段
- 教学概念：ACE、目标检测、白化
- 适用课程：高光谱遥感
- 典型练习：以标布光谱为目标运行 ACE 并评估 ROC 检测性能。

## rs:continuum_removal

连续统去除：归一化吸收特征深度，突出矿物/植被的吸收峰位置与形状，便于特征比对。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：bands（integer）、output（raster）
- 参数：output（string）
- 前置条件：Input should be reflectance (0..1); DN values give meaningless ratios.
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
- 参数：nEndmembers（integer）、projections（integer）
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
- 输入：input（raster）
- 输出：output（raster）
- 参数：output（string）、target（numeric）
- 前置条件：'target' must have one finite value per input band.
- 局限：Background statistics come from the input scene itself; a separate background raster is a future extension.
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
- 参数：numComponents（integer）、output（string）
- 前置条件：Input should be calibrated reflectance/radiance; wavelength metadata is not required for MNF itself.
- 适用地物：任意地物（高光谱）
- 适用场景：高光谱数据降维、去噪后的波段精简
- 失败模式：
  - `INSUFFICIENT_MEMORY` — 波段数过大导致协方差矩阵内存超限。处置：分块统计或先做波段筛选
- 教学概念：MNF、信噪比、噪声白化
- 适用课程：高光谱遥感
- 典型练习：对 200 波段影像做 MNF 并保留前 20 个高信噪比分量。

## rs:rx_anomaly

RX 异常检测（Reed-Xiaoli）：以背景统计检测与局部背景显著不同的异常像元，无需先验目标光谱。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：max（numeric）、mean（numeric）、output（raster）
- 参数：output（string）
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

## rs:spectral_unmixing

光谱解混：按线性混合模型求解每个像元中各端元的丰度，输出丰度图，服务亚像元定量分析。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输入：input（raster）
- 输出：endmembers（integer）、meanError（numeric）、output（raster）
- 参数：bands（integer）、endmembers（string）、errorOut（string）、output（string）
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

