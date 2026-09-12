<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 分类与机器学习（classification）

共 9 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:detect

目标检测算子：用训练好的检测模型在影像中定位目标（车辆/船舶/飞机等）并输出框/类别/置信度。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）
- 输出：backend（string）、classes（string）、detections（integer）、device（string）、model（string）、output（raster）、rawDetections（integer）、tileSize（integer）、tiles（integer）
- 参数：bands（integer）、batchCap（integer）、conf（numeric）、device（string）、model（string）、nms_iou（numeric）、output（string）、tta（enum）
- 前置条件：需要已部署的检测模型与匹配的输入规格。
- 适用地物：船舶、车辆、人工目标
- 适用场景：港口船舶监测、交通目标普查
- 失败模式：
  - `MODEL_NOT_READY` — 模型文件缺失或未加载。处置：先注册/加载模型再执行检测
  - `MODEL_INCOMPATIBLE` — 输入分辨率/波段与模型训练设置不符。处置：按模型卡要求重采样或调整波段
- 教学概念：目标检测、深度学习推理
- 适用课程：深度学习与遥感应用
- 典型练习：在港口影像上运行船舶检测模型并按置信度过滤结果。

## rs:embedding

特征嵌入提取：用预训练模型把影像编码为稠密特征向量/嵌入图，供检索、聚类与少样本分类复用。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）
- 输出：backend（string）、device（string）、embedding_dim（integer）、mean_vector（string）、model（string）、outBands（integer）、output（raster）、tileSize（integer）、tiles（integer）
- 参数：aggregate（enum）、bands（integer）、batchCap（integer）、device（string）、model（string）、output（string）、tta（enum）
- 前置条件：需要嵌入模型；输出为非显示用特征数据。
- 适用地物：任意地物
- 适用场景：样本检索与相似影像搜索、少样本分类的特征底座
- 失败模式：
  - `MODEL_NOT_READY` — 嵌入模型未加载。处置：先加载嵌入模型
- 教学概念：特征嵌入、自监督特征
- 适用课程：深度学习与遥感应用
- 典型练习：提取影像块嵌入并检索与目标样区最相似的 10 个区块。

## rs:feature_normalize

特征归一化：按 min-max 或 z-score 把各特征缩放到可比量纲，改善距离度量类分类器的表现（容差级算子）。

- 确定性：容差级（并行执行与串行结果在 1e-6 相对容差内一致）
- 模态：optical、sar、multimodal、dem
- 输入：input（raster）
- 输出：bands（integer）、method（string）、output（raster）、stats（string）
- 参数：inverse（boolean）、method（enum）、output（string）
- 前置条件：Input raster (feature cube recommended); inverse=true requires a cube whose contract carries normalization stats.
- 局限：NoData-aware: declared NoData sentinels and non-finite pixels are excluded from the fit and left unchanged.；Constant bands (std<=0 or zero range) are skipped with a warning.；robust approximates the 0.5/99.5 percentile range from a 512-bin histogram, not exact percentiles.
- 适用地物：任意地物
- 适用场景：SVM/KNN 等分类器的预处理、多源特征量纲统一
- 失败模式：
  - `INVALID_PARAMETER` — 统计范围参数非法或含 nodata 未剔除。处置：检查归一化区间并在统计中排除 nodata
- 教学概念：归一化、z-score、量纲
- 适用课程：模式识别
- 典型练习：对堆叠特征做 z-score 归一化并比较 K-Means 聚类结果的稳定性。
- 可接下游：rs:kmeans_classification

## rs:feature_select

特征筛选：按重要性/相关性挑选对分类最有贡献的特征子集，降低维度并抑制冗余（容差级算子）。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal、dem
- 输入：input（raster）
- 输出：bands（integer）、output（raster）、selected（string）
- 参数：complement（boolean）、ids（string）、indices（integer）、output（string）、roles（string）
- 前置条件：Input raster; ids and roles resolve against the cube contract (plain rasters expose synthetic ids band_1..N).
- 局限：Band selection only — pixels are copied without resampling, reprojection or value changes.；The normalization section of the contract is dropped (stored stats no longer match the subset).
- 适用地物：任意地物
- 适用场景：高维特征集精简、分类精度优化实验
- 失败模式：
  - `INVALID_PARAMETER` — 目标特征数大于可用特征数。处置：调整选择数量参数
- 教学概念：特征重要性、相关性过滤
- 适用课程：模式识别
- 典型练习：对 20 维特征集筛选出重要性前 8 的特征并评估精度变化。

## rs:feature_stack

特征堆叠：把多个波段/指数/纹理图层按像元对齐堆叠为多特征影像，是分类前的标准组织步骤。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal、dem
- 输入：reference（raster）
- 输出：bands（integer）、features（string）、modalities（string）、output（raster）
- 参数：feature_id（string）、features（json）、generator（string）、output（string）
- 前置条件：All inputs co-registered on a common grid (identical dimensions and CRS); align against the reference with gdal:reproject first. Grid mismatches are errors, not resampled.；所有输入图层须同一网格与范围（ADR 0098）。
- 局限：No hidden resampling: dimension or CRS mismatches fail the run.；The cube contract is stored as dataset metadata; oversized contracts spill to a '<file>.features.json' sidecar.
- 适用地物：任意地物
- 适用场景：分类特征工程、多源数据（光学+SAR+DEM）融合组织
- 失败模式：
  - `GRID_MISMATCH` — 参与堆叠的图层网格/尺寸不一致。处置：先统一网格（rs:align）再堆叠
- 教学概念：特征堆叠、特征工程
- 适用课程：遥感数字图像处理
- 典型练习：把 NDVI、NDWI、DEM、VV/VH 堆叠为 5 维特征影像用于土地覆盖分类。
- 可接下游：rs:supervised_classification

## rs:infer

通用深度学习推理算子：加载平台模型库中的分割/分类/检测模型执行推理，输出类别图或目标列表。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal、temporal
- 输入：input（raster）
- 输出：backend（string）、device（string）、height（integer）、model（string）、outBands（integer）、output（raster）、tileSize（integer）、tiles（integer）、width（integer）
- 参数：bands（integer）、batchCap（integer）、blend（enum）、device（string）、model（string）、named_inputs（json）、output（string）、tta（enum）
- 前置条件：Model must be loadable by cv::dnn::readNetFromONNX.；需要平台模型库中的已注册模型。
- 适用地物：任意地物
- 适用场景：模型业务化落地、地物要素智能提取
- 失败模式：
  - `MODEL_NOT_READY` — 模型未注册或权重文件缺失。处置：在模型库中注册模型并确认权重路径
  - `MODEL_INCOMPATIBLE` — 模型输入规格（波段/尺寸/归一化）不匹配。处置：按模型清单准备输入
  - `INSUFFICIENT_MEMORY` — 大图一次推理内存超限。处置：启用分块推理参数或降低输入尺寸
- 教学概念：深度学习推理、模型部署
- 适用课程：深度学习与遥感应用
- 典型练习：用建筑提取模型对城区影像推理并叠加矢量边界核查。

## rs:kmeans_classification

K-Means 非监督分类：按光谱聚类自动划分地物类别，无需训练样本，结果受初始中心影响。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）
- 输出：k（integer）、output（raster）、samplesUsed（integer）
- 参数：algorithm（enum）、bands（integer）、k（integer）、maxSamples（integer）、output（string）、scale（boolean）
- 适用地物：任意地物
- 适用场景：无训练数据时的快速聚类、类内光谱结构探索
- 失败模式：
  - `INVALID_PARAMETER` — 类别数 k 设置不合理。处置：k 取 5–20 并结合研究区复杂度调整
  - `NOT_SUPPORTED` — 未固定随机种子导致两次结果不同。处置：固定 seed 参数保证结果可复现
- 教学概念：非监督分类、K-Means、聚类中心
- 适用课程：遥感数字图像处理
- 典型练习：对研究区做 8 类 K-Means 聚类并人工归并类别为土地覆盖图。
- 可接上游：rs:feature_normalize

## rs:sam_classify

光谱角制图（SAM）分类：以光谱向量夹角度量相似度，对光照差异不敏感，适合矿物/目标精细分类。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）
- 输出：bands（integer）、classes（integer）、output（raster）
- 参数：angleOut（string）、bands（integer）、metric（enum）、output（string）、refs（string）
- 前置条件：Reference spectra must use the same band order and units as the input raster.
- 局限：SID requires non-negative reflectance-like spectra (a zero or negative band invalidates the pair).
- 适用地物：矿物、植被、目标光谱
- 适用场景：矿物填图、已知端元的目标识别
- 失败模式：
  - `BAND_ROLE_UNRESOLVED` — 端元光谱波段数与影像不一致。处置：重采样端元光谱到影像波段设置
- 教学概念：光谱角、端元光谱
- 适用课程：高光谱遥感
- 典型练习：以实验室光谱为端元对影像做 SAM 分类并对比真实矿物分布。

## rs:supervised_classification

监督分类：基于训练样本（随机森林/ SVM 等按 model 参数）对影像逐像元分类，输出类别图与精度报告。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）、training（vector）
- 输出：classes（integer）、imbalanceWarnings（string）、kappa（numeric）、meanConfidence（numeric）、mode（string）、output（raster）、overallAccuracy（numeric）、perClassMetrics（string）、trainSamples（integer）、trainSamplesByClass（string）
- 参数：bands（integer）、classField（string）、maxSamplesPerClass（integer）、method（enum）、modelIn（string）、modelOut（string）、output（string）、probabilityOutput（string）、scale（boolean）、seed（integer）、testSplit（numeric）
- 前置条件：Train mode: training polygons must overlap the raster；Predict-only: modelIn must match method and band set；需要标注训练数据；输入特征建议先统一网格与量纲。
- 适用地物：农田、森林、城市、水体
- 适用场景：土地覆盖/利用制图、作物分布制图
- 失败模式：
  - `TRAINING_INVALID` — 训练样本缺失、类间不平衡或类别数不一致。处置：检查训练样本覆盖所有类别且每类有足够样本
  - `MODEL_INCOMPATIBLE` — 输入波段数与模型期望特征数不符。处置：让模型与特征波段集一致，或用 rs:feature_stack 重排特征
- 教学概念：监督分类、训练样本、混淆矩阵
- 适用课程：遥感数字图像处理、模式识别
- 典型练习：用 5 类训练样本完成研究区土地覆盖分类，并以 30% 独立样本评价总体精度。
- 可接上游：rs:pca、rs:feature_stack

