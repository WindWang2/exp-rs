<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 栅格空间分析（raster_spatial）

共 15 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:align

网格对齐（shared-grid builder）：把多幅栅格重采样/裁剪到统一网格（原点、分辨率、范围），是 ADR 0098 同一网格契约的标准执行算子。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）、reference（raster）
- 输出：alreadyAligned（boolean）、height（integer）、output（raster）、width（integer）
- 参数：categorical（boolean）、output（string）、resampling（enum）、warpMemoryLimitBytes（numeric）
- 前置条件：作为网格修复器，建议在任何多输入栅格运算前执行。
- 局限：Rotated or flipped reference grids are refused; no hidden resampling of the reference itself.
- 适用地物：任意地物
- 适用场景：多源数据融合前的网格统一、变化检测/堆叠/掩膜前的配准
- 失败模式：
  - `CRS_MISMATCH` — 输入坐标系不一致。处置：先统一重投影或在参数中声明目标 CRS
  - `INVALID_PARAMETER` — 目标分辨率或参考图层缺失。处置：指定参考栅格或显式给出目标网格参数
- 教学概念：重采样、网格对齐、配准
- 适用课程：GIS 原理、遥感数字图像处理
- 典型练习：把 DEM 与 Sentinel-2 影像对齐到 10m 统一网格。

## rs:connected_components

连通组件标记：对二值栅格标记连通斑块并编号，输出斑块 ID 图与面积属性。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、connectivity（enum）、output（string）
- 适用地物：水体、建筑、烧迹地
- 适用场景：斑块计数与编号、洪水斑块追踪
- 失败模式：
  - `INVALID_PARAMETER` — 连通性规则与数据不匹配。处置：按目标形态选择 4/8 邻域
- 教学概念：连通性、斑块标记、四邻域/八邻域
- 适用课程：GIS 原理
- 典型练习：标记洪水淹没斑块并统计面积大于 1 公顷的斑块数量。

## rs:fill_holes

孔洞填充：把类别图或掩膜内部完全闭合的孔洞按周边类别填充，修复分类碎洞。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、connectivity（enum）、output（string）
- 适用地物：任意地物
- 适用场景：分类图后处理、水体掩膜修复
- 失败模式：
  - `INVALID_PARAMETER` — 最大孔洞面积限制过小。处置：按目标孔洞尺寸调整面积阈值
- 教学概念：孔洞填充、连通区域
- 适用课程：遥感数字图像处理
- 典型练习：填充土地覆盖图中的孤立孔洞并评估面积变化。

## rs:focal_stats

焦点统计：对每个像元的邻域计算均值/方差/多数等统计量，实现连续场平滑与邻域特征生产。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、output（string）、stat（enum）、window（integer）
- 适用地物：任意地物
- 适用场景：邻域特征生产、景观格局分析
- 失败模式：
  - `INVALID_PARAMETER` — 窗口或统计方法参数非法。处置：检查 window 为奇数且 statistic 在枚举内
- 教学概念：焦点统计、邻域分析、卷积窗口
- 适用课程：GIS 原理
- 典型练习：计算 5x5 邻域 NDVI 标准差图刻画植被空间异质性。

## rs:local_extrema

局部极值检测：在窗口内标记局部极大/极小值点，用于峰值点与坑穴识别。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、output（string）、window（integer）
- 适用地物：地形、温度场
- 适用场景：山脊点识别、城市热岛峰值检测
- 失败模式：
  - `INVALID_PARAMETER` — 窗口过小导致大量伪极值。处置：增大窗口并叠加阈值过滤
- 教学概念：局部极值、窗口分析
- 适用课程：数字图像处理
- 典型练习：在地表温度图上标记热岛核心峰值点。

## rs:majority_filter

多数值滤波：以邻域多数类别替换中心像元，平滑分类图同时保持边界，是分类后处理的常用手段。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：kernel（integer）、output（string）
- 前置条件：Input raster must be a single-band integer classification raster.
- 适用地物：任意地物
- 适用场景：分类图平滑、类别噪声抑制
- 失败模式：
  - `INVALID_PARAMETER` — 窗口尺寸非法。处置：使用大于 1 的奇数窗口
- 教学概念：多数滤波、邻域投票
- 适用课程：遥感数字图像处理
- 典型练习：用 3x3 多数滤波平滑分类图并统计边界像元变化。

## rs:morphology

形态学运算：腐蚀/膨胀/开闭运算处理二值或灰度栅格，常用于掩膜修整与对象形状整形。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、connectivity（enum）、iterations（integer）、op（enum）、output（string）
- 适用地物：任意地物
- 适用场景：云掩膜修补、建筑斑块整形
- 失败模式：
  - `INVALID_PARAMETER` — 结构元尺寸非法。处置：使用大于 1 的奇数窗口
- 教学概念：腐蚀、膨胀、开闭运算
- 适用课程：数字图像处理
- 典型练习：对水体二值图做开运算去除细碎误检斑块。

## rs:mosaic

影像镶嵌：把多幅相邻影像拼接为一幅，支持羽化与接缝线处理，输出大区域底图。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输出：height（integer）、inputCount（integer）、output（raster）、width（integer）
- 参数：inputs（string）、output（string）
- 适用地物：任意地物
- 适用场景：区域底图生产、分幅成果拼接
- 失败模式：
  - `CRS_MISMATCH` — 输入影像坐标系不一致。处置：先统一重投影再镶嵌
  - `GRID_MISMATCH` — 输入分辨率不一致。处置：先 rs:resample 统一分辨率
- 教学概念：镶嵌、接缝线、羽化
- 适用课程：遥感数字图像处理
- 典型练习：把 4 景相邻 Sentinel-2 镶嵌为全市底图并检查接缝色差。

## rs:proximity

proximity 距离栅格：计算每个像元到目标要素的欧氏距离，生产可达性/影响区分析图层。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、connectivity（enum）、output（string）
- 前置条件：距离计算要求米制投影坐标系。
- 适用地物：河流、道路、居民点
- 适用场景：河流缓冲分析、城市设施可达性
- 失败模式：
  - `CRS_MISMATCH` — 输入为地理坐标系导致距离单位为度。处置：先重投影到米制投影坐标系
- 教学概念：欧氏距离、缓冲区
- 适用课程：GIS 原理
- 典型练习：生成距最近河流距离图并按 500m 阈值划定缓冲带。

## rs:rasterize

矢量转栅格：按属性字段把面/线/点要素烧录到参考网格，生成类别或掩膜栅格。

- 确定性：逐位一致（bit_exact）
- 模态：vector
- 输入：input（raster）、vector（vector）
- 输出：output（raster）
- 参数：allTouched（boolean）、field（string）、layer（string）、output（string）、value（numeric）
- 前置条件：Reference raster carries a CRS and a north-up geotransform.；Vector features carry geometry; zone/attribute fields are numeric when used as burn values.；需要指定与目标输出一致的参考网格。
- 局限：Overlapping geometries: last feature wins (input order).；Unburned pixels are NaN (declared NoData) — not zero.；The feature set is byte-budgeted (256 MiB); subset larger vectors first.
- 适用地物：行政区划、地块、道路
- 适用场景：样本掩膜生产、参考数据栅格化
- 失败模式：
  - `CRS_MISMATCH` — 矢量与参考网格坐标系不一致。处置：先重投影矢量或声明目标 CRS
  - `INVALID_PARAMETER` — 属性字段类型不支持。处置：使用数值或类别型字段
- 教学概念：栅格化、属性烧录
- 适用课程：GIS 原理
- 典型练习：把耕地地块矢量按作物类别栅格化为分类参考图。

## rs:recode

类别重编码：按映射表把旧类别值映射为新值（合并/二值化/重排序），不改几何只改编码。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：input（raster）
- 输出：output（raster）
- 参数：map（json）、output（string）、recode（json）、recode_map（string）
- 前置条件：Input raster must be a single-band integer classification raster.
- 适用地物：任意地物
- 适用场景：类别体系归并、二值掩膜生成
- 失败模式：
  - `INVALID_PARAMETER` — 映射表中引用了不存在的源类别。处置：核对类别编码与映射表
- 教学概念：重编码、类别映射
- 适用课程：GIS 原理
- 典型练习：把 10 类土地覆盖归并为 5 大类并生成映射对照表。

## rs:resample

栅格重采样：最近邻/双线性/立方卷积改变栅格分辨率，分类图用最近邻保持类别编码。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：height（integer）、output（raster）、resolutionX（numeric）、resolutionY（numeric）、width（integer）
- 参数：categorical（boolean）、output（string）、resampling（enum）、resolution（numeric）、warpMemoryLimitBytes（numeric）
- 局限：Keeps the input CRS and derived extent; use rs:align to hit an exact reference grid.
- 适用地物：任意地物
- 适用场景：分辨率统一、金字塔生产
- 适用性备注：类别图必须用最近邻，避免类别值被插值污染。
- 失败模式：
  - `INVALID_PARAMETER` — 不支持的 resample 方法。处置：method 从 nearest/bilinear/cubic 中选择
- 教学概念：重采样方法、分辨率变换
- 适用课程：GIS 原理
- 典型练习：把 30m 土地覆盖图最近邻重采样到 250m 并检查类别占比漂移。

## rs:sieve

筛除（sieve）：移除小于指定像元数的孤立斑块，压制椒盐噪声，保持大类斑块。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：output（raster）
- 参数：band（integer）、connectivity（enum）、min_area_pixels（integer）、output（string）
- 适用地物：任意地物
- 适用场景：分类图去噪、细碎斑块清理
- 失败模式：
  - `INVALID_PARAMETER` — 阈值像元数设置过大导致真实小图斑被删。处置：按最小制图图斑标准设置
- 教学概念：斑块筛除、最小图斑
- 适用课程：遥感数字图像处理
- 典型练习：对 K-Means 分类结果执行 8 像元筛除并对比斑块数变化。

## rs:threshold_raster

栅格阈值分割：按单/双阈值把连续栅格二值化或分级，是指数图到类别图的最短路径。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）
- 输出：maskedPercent（numeric）、maskedPixels（integer）、output（raster）、thresholdUsed（numeric）、totalPixels（integer）
- 参数：cleanup（enum）、cleanupIterations（integer）、minAreaPixels（integer）、output（string）、percentile（numeric）、statisticalK（numeric）、threshold（numeric）、thresholdMethod（enum）
- 前置条件：Input must be a single-band raster (e.g. a change magnitude from rs:change_difference / rs:change_cva).
- 局限：Multi-band input uses band 1 only.
- 适用地物：植被、水体、温度场
- 适用场景：NDVI 阈值提取植被、热异常分级
- 失败模式：
  - `INVALID_PARAMETER` — 阈值区间配置错误（min>max）。处置：检查阈值上下限参数
- 教学概念：阈值分割、二值化、分级
- 适用课程：遥感数字图像处理
- 典型练习：以 NDWI>0.3 提取水面并评估与 MNDWI 结果的差异。

## rs:zonal_stats

分区统计：按矢量/栅格分区计算内部像元的均值/总和/分位数等，输出区域属性表，是报表与评价的核心算子。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、thermal、dem
- 输入：input（raster）、vector（vector）
- 输出：output（table）
- 参数：bands（integer）、layer（string）、median（boolean）、output（string）、zoneField（string）
- 前置条件：Value raster carries a CRS and a north-up geotransform.；Zone features carry geometry; a declared zoneField must exist on every feature.；分区与栅格需同一 CRS；必要时先 rs:align。
- 局限：Overlapping zones: last feature wins (input order), matching rs:rasterize.；Median is budgeted (shared 16M-value collection); zones beyond it are flagged while their other statistics stay exact.；stddev is the population statistic (divided by N).
- 适用地物：行政区、流域、地块
- 适用场景：行政区指标统计、地块产量评价
- 失败模式：
  - `CRS_MISMATCH` — 分区矢量与栅格坐标系不一致。处置：统一两者坐标系后重算
  - `INVALID_PARAMETER` — 统计量名称不在支持列表。处置：从 mean/sum/min/max/quantile 中选择
- 教学概念：分区统计、区域均值
- 适用课程：GIS 原理
- 典型练习：按乡镇统计 NDVI 均值并生成长势排名表。

