<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 变化检测（change）

共 11 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:change_cva

变化向量分析（CVA）：在多波段空间计算两期变化向量的模长，全面刻画多波段变化强度。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).；两期影像同网格、同波段集。
- 适用地物：城市、农田、森林
- 适用场景：多波段变化强度制图、城市扩张多维度检测
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐
  - `INVALID_PARAMETER` — 两期波段数不一致。处置：保证两期使用相同波段集
- 教学概念：变化向量、欧氏距离
- 适用课程：遥感数字图像处理
- 典型练习：对 6 波段两期影像做 CVA 并分析变化强度空间格局。

## rs:change_cva_angle

CVA 角度变体：在变化强度之外输出变化方向角，区分变化类型（增/减、类型转换方向）。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：height（integer）、method（string）、mode（string）、output（raster）、width（integer）
- 参数：afterBand1（integer）、afterBand2（integer）、beforeBand1（integer）、beforeBand2（integer）、mode（enum）、output（string）
- 前置条件：Before and after rasters must be co-registered with identical dimensions.
- 适用地物：农田、森林
- 适用场景：变化方向判读、植被退化 vs 恢复区分
- 失败模式：
  - `INVALID_PARAMETER` — 两期波段数不一致。处置：保证相同波段集与顺序
- 教学概念：变化方向、CVA 角度
- 适用课程：遥感数字图像处理
- 典型练习：用 CVA 角度把变化区分为绿化与退化两类。

## rs:change_detection

通用双时相变化检测入口：按 method 选择差值/比值/CVA 等策略生成变化图，需两期配准影像。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：changedArea（numeric）、changedAreaUnit（string）、changedPercent（numeric）、changedPixels（integer）、mean（numeric）、method（string）、output（raster）、stddev（numeric）、thresholdUsed（numeric）、totalPixels（integer）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、cleanup（enum）、cleanupIterations（integer）、makeMask（boolean）、method（enum）、minAreaPixels（integer）、output（string）、percentile（numeric）、statisticalK（numeric）、threshold（numeric）、thresholdMethod（enum）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).；两期影像须同模态、同网格、可比辐射量纲。
- 局限：ratio outputs after/before (NaN where before is <= 0); cva and mad stream over 256x256 tiles in O(tile*bands + bands^2) memory (mad is multi-pass); makeMask writes a UInt8 0/1 mask with manual/Otsu/percentile/statistical thresholds and optional morphological cleanup.
- 适用地物：城市、农田、森林、水体
- 适用场景：土地覆盖变化检测、灾害应急变化速报
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格/配准不一致。处置：先用 rs:align 配准到同一网格
  - `TIME_ORDER_INVALID` — before/after 顺序颠倒。处置：确认 before 为早期影像
  - `MODALITY_MISMATCH` — 两期数据模态不一致（光学 vs SAR）。处置：同模态两期数据才能直接比较
- 教学概念：双时相变化检测、变化阈值
- 适用课程：遥感数字图像处理
- 典型练习：对两期城市影像执行变化检测并按阈值提取新建区。

## rs:change_difference

差值法变化检测：两期同波段影像直接相减，|差值| 超阈值判为变化，简单稳健，要求输入可比。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).；两期影像须配准到同一网格；辐射量纲需一致。
- 适用地物：植被、水体、城市
- 适用场景：NDVI 差值植被变化、水体涨落检测
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐（同一网格契约）
  - `INVALID_RADIOMETRY` — 两期辐射量纲不可比（DN vs 反射率）。处置：统一完成定标/大气校正后再做差值
- 教学概念：影像差值、变化阈值
- 适用课程：遥感数字图像处理
- 典型练习：计算两期 NDVI 差值并取 ±0.2 阈值提取退化/恢复区。

## rs:change_irmad

IR-MAD 迭代加权 MAD：在 MAD 基础上迭代降权不变像元，收敛出更纯净的变化概率图。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：convThreshold（numeric）、maxIterations（integer）、output（string）
- 前置条件：Before and after rasters must have equal band count and dimensions.
- 适用地物：城市、森林
- 适用场景：高精度变化检测、变化概率制图
- 失败模式：
  - `NOT_SUPPORTED` — 迭代不收敛（数据方差结构病态）。处置：检查输入波段相关性或减少波段数
- 教学概念：IR-MAD、迭代加权、变化概率
- 适用课程：统计方法、遥感数字图像处理
- 典型练习：运行 IR-MAD 得到变化概率图并与固定阈值法对比。

## rs:change_log_ratio

对数比值变化检测：log(a/b) 把乘性差异化为加性并压缩动态范围，统计解释性更好。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、epsilon（numeric）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size.
- 适用地物：植被、土壤
- 适用场景：SAR 变化检测常用策略、对数域阈值实验
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐
  - `INVALID_PARAMETER` — 出现零值/负值导致对数无定义。处置：先保证输入为正值（定标后数据）
- 教学概念：对数比值、对数正态
- 适用课程：微波遥感、遥感数字图像处理
- 典型练习：对两期 Sentinel-1 做 log-ratio 变化检测并与差值法比较噪声水平。

## rs:change_mad

MAD 变化检测（多元变化检测）：对两期多波段做典型相关变换后取方差不变分量，自动突出真实变化。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).
- 适用地物：城市、农田
- 适用场景：辐射差异较大时的稳健变化检测、多波段统计变化检测
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐
  - `NOT_SUPPORTED` — 样本像元过少导致协方差估计不稳定。处置：保证足够重叠区域样本
- 教学概念：典型相关、MAD 变量、方差不变
- 适用课程：遥感数字图像处理、统计方法
- 典型练习：对光照差异明显的两期影像执行 MAD 并与差值法比较虚警率。

## rs:change_normalized_difference

归一化差值变化检测：(a-b)/(a+b) 形式压制公共乘性因子，兼顾差值与比值的优点。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 波段角色要求：nir×1、swir×1
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).
- 适用地物：植被、水体
- 适用场景：指数时序变化（如 dNDVI）、归一化变化制图
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐
- 教学概念：归一化差值、相对变化
- 适用课程：遥感数字图像处理
- 典型练习：对两期 NDVI 做归一化差值并比较与简单差值的检测结果。
- 可接上游：rs:spectral_index

## rs:change_ratio

比值法变化检测：两期影像相比值突出比例型变化，对乘性光照差异比差值法更稳健。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、output（string）
- 前置条件：Before and after rasters must be co-registered and same size (grid compatibility is preflighted).
- 适用地物：植被、裸土
- 适用场景：光照差异较大的两期比较、矿物蚀变粗查
- 失败模式：
  - `GRID_MISMATCH` — 两期影像网格不一致。处置：先 rs:align 对齐
- 教学概念：影像比值、乘性噪声
- 适用课程：遥感数字图像处理
- 典型练习：对两期影像做比值检测并讨论光照差异下差值法的误报。

## rs:change_sam

光谱角变化检测：以两期光谱向量的夹角度量光谱形态变化，对亮度差异不敏感。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 网格要求：输入必须位于同一网格（先用 rs:align 对齐）
- 输入：after（raster）、before（raster）
- 输出：mean（numeric）、method（string）、output（raster）、stddev（numeric）
- 参数：output（string）
- 前置条件：Before and after rasters must have equal band count and dimensions.
- 适用地物：矿物、植被
- 适用场景：光谱形态变化识别、光照不一致场景的变化检测
- 失败模式：
  - `INVALID_PARAMETER` — 两期波段配置不一致。处置：统一波段集后重试
- 教学概念：光谱角、形态变化
- 适用课程：高光谱遥感
- 典型练习：用 SAM 角度区分两期高光谱影像的光谱形态变化与亮度变化。

## rs:post_classification_change

分类后变化检测：对两期分类图做类别转移矩阵与变化/保留图输出，变化类型可解释，精度受两期分类质量约束。

- 确定性：逐位一致（bit_exact）
- 模态：optical、sar、multimodal
- 输入：after（raster）、before（raster）
- 输出：changedPercent（numeric）、changedPixels（integer）、classCount（integer）、fromTotals（string）、netChange（string）、output（raster）、toTotals（string）、totalPixels（integer）、transitionMatrix（string）、unchangedPixels（integer）
- 参数：afterBand（integer）、band（integer）、beforeBand（integer）、class_count（integer）、class_labels（string）、output（string）
- 前置条件：Both inputs are thematic rasters with a shared class coding; before/after grids must be aligned (grid compatibility is preflighted).；两期分类图须同一分类体系与网格。
- 局限：Change-type codes are UInt16: class_count must be <= 255 so before * classCount + after fits; auto class_count derives from the maximum observed class + 1.
- 适用地物：城市、农田、森林
- 适用场景：土地覆盖转移矩阵、用途变更统计
- 失败模式：
  - `GRID_MISMATCH` — 两期分类图网格不一致。处置：先 rs:align 最近邻对齐
  - `INVALID_PARAMETER` — 两期分类体系（类别数/编码）不一致。处置：统一类别编码或提供映射表
- 教学概念：转移矩阵、分类后比较
- 适用课程：遥感应用分析
- 典型练习：对 2010/2020 两期土地覆盖图生成转移矩阵并统计耕地流失去向。

