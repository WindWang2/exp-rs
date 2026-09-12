<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 面向对象影像分析（obia）

共 7 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:obia_classify

对象级分类：基于对象特征用规则或模型给对象赋类别，输出矢量/栅格类别图。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）、labels（raster）、training（vector）
- 输出：accuracy（string）、classes（integer）、labeledSegments（integer）、output（raster）、segments（integer）
- 参数：bands（integer）、cellSize（integer）、classColors（string）、classField（string）、featureSelection（string）、features（enum）、method（enum）、minLabelPixels（integer）、minRegionSize（integer）、mlpHiddenLayerSize（integer）、mlpMaxIter（integer）、output（string）、outputUncertainty（string）、quantizeBins（integer）、rfMaxDepth（integer）、rfMinSampleCount（integer）、rfNumTrees（integer）、scale（boolean）、segmentClasses（string）、segmentMethod（enum）、smoothKernel（integer）
- 前置条件：需要对象特征表（rs:obia_features）。
- 适用地物：城市、农田、林地
- 适用场景：面向对象土地覆盖制图、建筑/植被对象提取
- 失败模式：
  - `TRAINING_INVALID` — 对象样本缺失或与特征表不匹配。处置：补采对象样本或重算特征
  - `DATASET_NOT_FOUND` — 缺少对象特征图层。处置：先运行 rs:obia_features
- 教学概念：对象分类、规则集
- 适用课程：面向对象遥感
- 典型练习：用 NDVI 与面积规则把对象分为植被/裸地/建筑三类。
- 可接上游：rs:obia_features

## rs:obia_features

对象特征计算：为分割对象统计光谱均值、形状指数、纹理等特征，形成对象级特征表。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）、labels（raster）
- 输出：bands（integer）、features（integer）、output（table）、segments（integer）
- 参数：bands（integer）、output（string）
- 前置条件：需要先有分割对象图层。
- 适用地物：城市、农田
- 适用场景：对象分类特征工程、对象级统计分析
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少分割对象图层。处置：先运行 rs:obia_segment/rs:segment
- 教学概念：对象特征、形状指数
- 适用课程：面向对象遥感
- 典型练习：为田块对象计算光谱与形状特征并导出属性表。
- 可接上游：rs:obia_segment
- 可接下游：rs:obia_classify

## rs:obia_hierarchy

对象层级管理：构建/查询多尺度对象层级（父-子对象关系），支持跨尺度聚合分析。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）、labelsCoarse（raster）、labelsFine（raster）、training（vector）
- 输出：coarseSegments（integer）、fineSegments（integer）、labeledSegments（integer）
- 参数：classColors（string）、classField（string）、classifyLevel（integer）、maxIterations（integer）、method（enum）、minLabelPixels（integer）、minRegionSize（integer）、mlpHiddenLayerSize（integer）、mlpMaxIter（integer）、outputClass（string）、outputCoarse（string）、outputFine（string）、outputParents（string）、outputUncertainty（string）、parents（string）、rangeRadius（numeric）、rfMaxDepth（integer）、rfMinSampleCount（integer）、rfNumTrees（integer）、segmentClasses（string）、spatialRadius（integer）、threshold（numeric）、watershedThreshold（numeric）
- 适用地物：城市、流域
- 适用场景：多尺度分析、地块→区域聚合统计
- 失败模式：
  - `DATASET_NOT_FOUND` — 层级数据缺失。处置：先完成多尺度分割
- 教学概念：对象层级、尺度
- 适用课程：面向对象遥感
- 典型练习：在两级对象层级上聚合田块产量到行政区。

## rs:obia_label

对象标注：把标签/样本赋予对象，生成对象级训练集，支持交互式补标。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）、labels（raster）、training（vector）
- 输出：labeled（integer）、output（table）
- 参数：classField（string）、minLabelPixels（integer）、output（string）
- 适用地物：任意地物
- 适用场景：对象样本采集、标注质量控制
- 失败模式：
  - `INVALID_PARAMETER` — 标签值超出类别体系。处置：按既定类别编码标注
- 教学概念：对象标注、样本管理
- 适用课程：面向对象遥感
- 典型练习：对 200 个田块对象赋予作物类别标签并导出训练集。

## rs:obia_segment

OBIA 多尺度分割：面向对象分析的专用分割算子，输出对象标签栅格（对象层级由 rs:obia_hierarchy 构建）。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）
- 输出：engine（string）、output（raster）、segments（integer）
- 参数：bands（integer）、engine（enum）、maxIterations（integer）、minRegionSize（integer）、output（string）、quantizeBins（integer）、rangeRadius（numeric）、smoothKernel（integer）、spatialRadius（integer）、threshold（numeric）
- 适用地物：城市、农田、林地
- 适用场景：对象层级构建、OBIA 分类底图
- 失败模式：
  - `INVALID_PARAMETER` — 分割引擎参数配置失衡。处置：按 engine 调整：simple 引擎用 smoothKernel/quantizeBins/minRegionSize，otb MeanShift 用 spatialRadius/rangeRadius/threshold
- 教学概念：多尺度分割、对象标签栅格
- 适用课程：面向对象遥感
- 典型练习：生成田块尺度的对象分割结果并叠加边界可视化。
- 可接下游：rs:obia_features

## rs:segment

深度学习分割模型推理（薄适配器）：加载平台模型库中的分割模型执行推理，输出类别/对象栅格；经典多尺度分割请用 rs:obia_segment。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）
- 输出：backend（string）、device（string）、height（integer）、model（string）、outBands（integer）、output（raster）、tileSize（integer）、tiles（integer）、width（integer）
- 参数：bands（integer）、batchCap（integer）、device（string）、format（enum）、model（string）、output（string）、tta（enum）
- 前置条件：需要平台模型库中的已注册分割模型。
- 适用地物：城市、农田、森林
- 适用场景：业务化地物要素提取、面向对象分析的对象底图生产
- 失败模式：
  - `MODEL_NOT_READY` — 模型未注册或权重缺失。处置：在模型库注册分割模型并确认权重路径
  - `MODEL_INCOMPATIBLE` — 输入波段/尺寸/归一化与模型规格不符。处置：按模型清单准备输入
- 教学概念：深度学习推理、分割模型、模型输入规格
- 适用课程：深度学习与遥感应用
- 典型练习：用建筑提取分割模型对城区影像推理并叠加矢量边界核查。

## rs:segment_stats

对象统计：按分割对象统计内部像元的均值/方差/占比等，输出对象属性表供后续分类与报表。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 输入：input（raster）、labels（raster）
- 输出：output（table）、segments（integer）
- 参数：bands（integer）、output（string）
- 适用地物：农田、城市、水体
- 适用场景：对象属性报表、异质性诊断
- 失败模式：
  - `DATASET_NOT_FOUND` — 缺少分割对象图层或统计输入。处置：先分割并确保统计输入对齐
- 教学概念：对象统计、区域统计
- 适用课程：面向对象遥感
- 典型练习：统计每个田块对象的 NDVI 均值与方差识别长势不均田块。

