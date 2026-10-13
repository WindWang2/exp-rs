# R10-A / S1c — src/analysis 正确性合同缺陷狩猎（五子域）

- worktree: `exp-rs-r10-algorithms` @ master `8a0864276`（只读审查，未改任何源码）
- 排除范围：#1461 已修项（逐带哨兵、>255 dtype、几何变换 fail-closed、segment_map 原子写、
  scaler.fit 检查、MinDistance/Mahalanobis 维度守卫、include 修正、segment_features 错误通道）
  均已确认在位，不再重复报告。
- 每条发现均已对照 tests/ 确认无既有覆盖（覆盖情况逐条注明）。

严重度：P0 = 静默产出错误科学结果且默认路径可达；P1 = 默认/常见输入下静默错误结果；
P2 = 边界/罕见输入错误结果或合同违约；P3 = 防御性/一致性小缺口。

---

## 1. classification（审查 40 文件，深读 25）

### AN-1 [P1] `isIgnoreScalar` 只挡 NaN 不挡 Inf —— ±Inf 像素既进训练也被分类
- 位置：`src/analysis/classification/rs_pixel_ignore_options.h:50-62`
  ```cpp
  if ( ignoreNaN && std::isnan( v ) )   // 只查 isnan
  ```
- 触发条件：源栅格任一选定波段含 ±Inf（Float32 波段运算/除零产物常见；GDAL RasterIO
  原样读出 Inf）。`nearlyEqual(Inf, nodata)` 恒 false，Inf 永不被忽略。
- 为什么错：
  1. 训练路径：`rs_training_data_extraction.cpp:233-239` 用 `isIgnorePixel` 过滤样本，
     Inf 样本被保留进 trainX。`Config.fitScaler` 默认 `false`
     （rs_classification_pipeline.h:127），所以 scaler 的 isfinite 闸门默认不生效；
     Inf 直接进 backend fit —— MinDistance/Mahalanobis 的均值/协方差被污染
     （`NaN < best` 式比较使所有距离判据失效），OpenCV 后端则产出静默垃圾模型。
  2. 预测路径：`rs_classification_pipeline.cpp:1109-1120` 同一 `isIgnorePixel` 判定，
     含 Inf 的像素被当作有效像素分类（写出一个真实类号而非 unclassified）。
- 仓库内自证不一致：同仓 `rs_multires_segmenter.cpp:356`、`rs_otb_segmenter.cpp:94`、
  `rs_feature_schema.cpp:222`、`rs_feature_scaler.cpp:28` 全部用 `!std::isfinite`，
  唯独分类的像素忽略合同只查 NaN。
- 测试覆盖：`tests/test_pixel_ignore_options.cpp:30` 只测 NaN；无 Inf 用例。
- 最小复现思路：单测 `opt.isIgnoreScalar(+inf, false, 0.0)` 现返回 false；
  e2e：3 波段 Float32 GTiff 植入一个 +Inf 像素 → 默认参数分类 → 该像素获得类号
  （期望 unclassified/NoData）。
- 修复方向：`isIgnoreScalar` 增加 `!std::isfinite(v)`（ignoreNaN 语义升级为 ignoreNonFinite，
  或新增并列开关保持向后兼容并默认开启）。

### AN-2 [P2] sieve 相邻小图斑互换标签（A→B 且 B→A 同时发生）
- 位置：`src/analysis/classification/rs_post_process.cpp:260-304`（`sieve`）
- 触发条件：两个不同类、彼此相邻、均小于 threshold 的图斑互为对方的边界多数邻域
  （例如角落里相邻的两个 1 像素孤岛）。
- 为什么错：替换值全部基于冻结的 `orig` 计算、统一写入 `out` —— A 的多数邻域是 B
  （out[A]=B），同时 B 的多数邻域是 A（out[B]=A）。两个图斑的类标签静默互换：
  每类像素总数守恒，但具体像素的类别错了，polygonize 下游矢量属性错。
  参考实现（gdal_sieve）按处理顺序在工副本上更新，不会互换。
- 测试覆盖：`tests/test_classification_postprocess.cpp` 的 sieve 用例只验证保留阈值
  与退化输入，无相邻小图斑对。
- 最小复现：5x5 标签图，(2,2)=1、(2,3)=2，其余 0，threshold=4，connectedness=8 →
  输出 (2,2)=2、(2,3)=1。
- 修复方向：逐类处理时使用"已更新的工作图"做邻域判定，或对成对小图斑特判保持不动。

### AN-3 [P2/UNCERTAIN] sieve 允许 NoData(0) 当选多数邻域，吞噬有效类孤岛 —— 与本文件 majorityFilter 的 #700 政策矛盾
- 位置：`src/analysis/classification/rs_post_process.cpp:107-143`（`borderNeighborMajority`
  无 0 票排除；对比同文件 `modeOfWindow:159-204` 明确"Label 0 不投票、0 中心保持 0"）
- 触发条件：一个小于 threshold 的有效类图斑四周主要是 NoData 像素。
- 为什么存疑：GDAL 自家 sieve 确实把背景值当合法邻居（行为兼容 GDAL）；
  但同文件 majorityFilter 有显式 #700 修复防"NoData 长入有效区"，两个算子语义相反，
  属于合同不一致。若以 GDAL 奇偶为准应注释声明；否则应让 0 不投票。
- 测试覆盖：无（sieve 测试无 NoData 邻域用例）。

### AN-4 [P2] K-Means 训练用 OpenCV 全局未播种 RNG —— 同进程二次拟合不可复现，Config.seed 不达 backend
- 位置：`src/analysis/classification/rs_classifier_kmeans.cpp:55`
  （`cv::kmeans(..., cv::KMEANS_PP_CENTERS, m_centers)`；`src/analysis/` 全树 grep 无
  `setRNGSeed/theRNG` 命中）
- 触发条件：同一进程内对同一数据第二次训练（GUI 重训、CV 工厂每 fold 重建 backend）。
  OpenCV kmeans 用线程局部全局 theRNG() 生成初始中心，连续调用状态推进 →
  两次结果不同；pipeline 的 `seed=42` 只传给抽样/划分，不进 KMeans。
- 为什么错：仓库的确定性合同（RsClassificationPipeline::Config.seed、
  rs_classification_utils.h "Deterministic … policy"）被聚类后端旁路；
  tests/test_classifier_kmeans.cpp:5 自己注释承认 "Centre ordering is non-deterministic"。
- 测试覆盖：无两次拟合一致性用例。
- 修复方向：fit 前调用 `cv::theRNG().state = seed`（seed 从 backend 构造参数传入）。
- 备注：ISODATA 是无 RNG 的确定性实现，不受影响。

（其余：accuracy/kappa、CV 分层与种子、spatial CV 缓冲区、Hungarian、JM、MLP/RF/NB/SVM
后端、calibration/isotonic/Platt、flood fill 均复查无新缺陷；多数已被既有 hardening 注释
与测试覆盖。）

---

## 2. segmentation（审查 15 文件，深读 11）

### AN-5 [P1] RsSimpleSegmenter 不把 ±Inf 当无效 → 单个 Inf 像素把整幅图坍缩成一个段（含 int 转换 UB）
- 位置：`src/analysis/segmentation/rs_simple_segmenter.cpp`
  - `quantize` 308-343：min/max 扫描只跳过 `== nodata || isnan`（316）→ vmax=+Inf 时
    `scale = (bins-1)/(vmax-vmin) = 0` → 所有权值落 bin 1 → 全图单连通分量 →
    `segmentCount()==1`；
  - 仅 -Inf 时 `v - vmin = +Inf`，`Inf * 0 = NaN`，`static_cast<int>(NaN)`（341）是 UB
    （x86 上得 INT_MIN）→ `1 + INT_MIN` 有符号溢出；
  - `gaussianSmooth` 的 `isNoData`（240-242）同样只查 `== nodata || isnan`，Inf 参与
    卷积并把 Inf 写回平滑缓冲（275、302 处 val/weightSum 仍为 Inf），先污染再量化。
  - 多波段路径 `segmentMultiBand`（150、164）逐带同样判定，同样坍缩。
- 为什么错：一个坏像素使整幅分割静默失效（输出是"合法"的 1 个段，无任何错误信号）。
  同仓 FNEA 主力 `rs_multires_segmenter.cpp:356` 用 `!std::isfinite( val )` 正确排除，
  简单分割器是唯一漏网点。
- 测试覆盖：`tests/test_simple_segmenter.cpp` 有 "nodata handling" 与 "#336 nodata boundary"
  用例，但无 Inf 用例；`tests/test_e2e_phase2.cpp:241,275` 亦无。
- 最小复现：32x32 阶梯数据植入单个 `+inf` → `RsSimpleSegmenter::segment(...)` →
  断言 `segmentCount() == 1`（错误现状）。
- 修复方向：`isNoData`/quantize 排除条件统一为 `!std::isfinite(v)`（与 multires 对齐）。

### AN-6 [P2] 连通分量 8 连通、小图斑合并邻接 4 连通 —— 对角接触的小区域永远低于 minRegionSize
- 位置：`src/analysis/segmentation/rs_simple_segmenter.cpp:354-356`（CC 用 8 邻域）vs
  `469-471`（mergeSmallRegions 边界偏移 dr4/dc4 仅 4 邻域）
- 触发条件：某小区域的全部邻居只与其对角接触（8 连通形成的区域形态），
  或四周全是 NoData（neighbor==0 不计入邻接，499）。
- 为什么错：(a) minRegionSize 合同违约——输出仍含 < minSize 的段；
  (b) fixpoint 循环（441-600）每轮全图重扫 32 轮后才放弃，白烧 32 次 O(n)。
- 测试覆盖：`tests/test_simple_segmenter.cpp:54` 的 minRegionSize 用例是正交邻接场景，
  无对角-only 邻接用例。
- 修复方向：合并邻接改 8 连通与 CC 对齐；对彻底无邻居的小区域记日志/文档化保留语义。

### AN-7 [P2] RsRoiLabeler::labelByMajority 丢弃 rasterize 的 ok 失败位 —— 基础设施失败静默丢票，违反自家头文件合同
- 位置：`src/analysis/segmentation/rs_roi_labeler.cpp:192`
  `const QSet<quint64> px = RsPixelRasterizer::rasterize( qg, gt, w, h );`
- 合同：`rs_pixel_rasterizer.h:40-46` 明文 "callers that train on the coverage MUST pass
  \a ok and fail closed when it is false"；MEM driver 缺失、GDALRasterizeGeometries 失败、
  扫描行 RasterIO 失败时返回空集且无 ok → labeler 把"覆盖为空"与"覆盖计算失败"混同，
  该多边形一票不投，段无标签或标签由剩余多边形决定——正是 #1056 修掉的静默丢失模式，
  在训练提取路径（collectPixels 用 4 参 + RasterizeFailed fail-closed）修了、ROI 标注路径没修。
- 测试覆盖：`tests/test_object_hierarchy.cpp:865-933` 只测正常路径/缺栅格路径，
  未注入 rasterize 失败（sForceRasterizeGeometriesFailure 钩子存在但此调用点不消费）。
- 修复方向：传 `bool ok`，false 时 fail（与 labelByMajority 现有 fail 风格一致）。

### AN-8 [P2] rs_segment_features 的 validMask/GLCM 同样不排除 Inf → Inf 进均值/方差，GLCM 量化处 `static_cast<int>(Inf)` UB
- 位置：`src/analysis/segmentation/rs_segment_features.cpp:119`
  （validMask 只查 `isnan || == sentinel`）与 `370/384`
  （`static_cast<int>( (val1 - minV) / rangeV * (nLevels-1) )` 先转换后 clamp —— val=+Inf
  时 (Inf-min)/range*15 = Inf，cast 发生在 std::clamp 之前，UB）。
  Inf 还直接进入 `acc->sum/sumSq/min/max`（277-280），mean/stddev 变 Inf/NaN。
- 测试覆盖：`tests/test_segment_features.cpp` 无 Inf 用例（有 NaN/nodata 用例）。
- 修复方向：validMask 判定加 `!std::isfinite`；GLCM 量化先 clamp 浮点再 cast。
- 说明：与 AN-1/AN-5 同族（Inf 治理缺口），但分属不同文件与修复点，故单列。

（其余：FNEA 主循环 DSU/邻接重定向/GLCM 分块重叠 fuzz 注释均可信；multires 已有 isfinite；
object_hierarchy/parent_link/otb/class_raster/consolidator 复查无新缺陷。）

---

## 3. hyperspectral（审查 8 文件：analysis/hyperspectral/continuum_removal +
processing/algorithms 的 spectral_classification/detection/resampling、mnf、unmixing、endmember seam）

**结论：未发现站得住的新缺陷。** 已逐项核对审查维度：
- SAM/SID（spectral_classification.cpp:11-67, 173-264）：零向量/近零范数守卫、
  isnan/isfinite 逐带拒绝、acos 前 clamp [-1,1]、SID 负反射率拒绝 —— 均在位。
- MNF（mnf_transform.cpp）：Jacobi 双精度特征分解、SNR 降序排序（380-382，方向正确）、
  噪声协方差奇异 fail-closed（323-331）、模型 %.17g 摘要防篡改。
- unmixing（spectral_unmixing.cpp）：`nEndmembers > bands` 拒绝（27、366）、
  零端元/共线端元 fail-closed、FCLS 和一约束惩罚 + 实测违约度量。
- 重采样（spectral_resampling.cpp）：FWHM→sigma 常量两处一致且注释了历史乘法错误、
  支持域内 NaN 洞传播 (#1186)、覆盖度 erf 闭式。
- continuum_removal（analysis 侧 + spectral_classification 侧双实现）：
  上凸包/段推进/半深插值复核正确。唯一小瑕疵（P3 级，记录不立案）：
  `spectral_classification.cpp:369-371` 对 |continuum|≤1e-12 的波段写 1.0f
  （"在连续统上"）而非无效标记，零反射率端点谱会静默丢失该波段吸收信息；
  且两套实现的 NoData 合同不同（一套跳过无效波段、一套整谱拒绝），属语义重复非错误。

---

## 4. atmospheric（审查 fast_6s_lookup.cpp 334 行 + 头文件）

### AN-9 [P3/UNCERTAIN] `locateTable` 缺少 `locateUniform` 同款非有限守卫
- 位置：`src/analysis/atmospheric/fast_6s_lookup.cpp:194-204`（对比 `175-192` 的
  locateUniform 显式 `if ( !std::isfinite( value ) ) return loc;`）
- 触发条件：`Atmosphere6sParams` 任一角度/气溶胶/水汽参数为 NaN/Inf。
- 后果：frac 变 NaN → 6D 权重 NaN → 插值条目全 NaN → `invertBoaReflectance:262`
  因 `!(T>0)` 拒绝（下游 fail-closed，整体纠正被拒而非产出错误反射率）。
  危害有限（无错误科学值），但错误形态是"莫名其妙的全 NaN 拒绝"而非参数校验错误。
- 测试覆盖：tests/test_fast_6s_atmospheric.cpp 无非有限参数用例。
- 修复方向：locateTable 开头补 `!std::isfinite` 早退（与 locateUniform 对齐）。

（LUT 越界为文档化 clamp 端点行为；DOS2 有太阳高度角/ESUN 守卫；暗像元路径
`denominator = esun*sin²` 有 esun>0 与 sinElev>0 守卫 —— 无除零缺口。）

---

## 5. georeferencing（审查 4 文件：qgsgcppoint/qgsgcptransformer/qgsleastsquares/qgsrpcgcptransformer）

**结论：未发现新缺陷。** 已核对审查维度：
- GCP 残差/病态：qgsleastsquares.cpp 有共线/重复点 SingularException（绝对 + 相对双守卫，
  74-83、143-146）、projective 的次小奇异值秩亏检查（369-379）。
- 多项式阶数：QgsGDALGeorefTransform 构造钳 ≤3（354），minimumGcpCount 公式正确
  （order n → (n+1)(n+2)/2），GDAL 创建失败 fail-closed。
- 逆变换：线性/Helmert 零尺度守卫、projective det==0 → hasInverse=false 且
  投影到无穷远逐点 panSuccess=false（599-602）。
- RPC：偏差不写 OFF（#286 教训）、中位数残差只在改善时应用、CRS 转换失败剔除样本。

---

## 汇总

| 子域 | 审查文件 | 立案 |
|------|---------|------|
| classification | 40（深读 25） | AN-1 P1, AN-2 P2, AN-3 P2/UNC, AN-4 P2 |
| segmentation | 15（深读 11） | AN-5 P1, AN-6 P2, AN-7 P2, AN-8 P2 |
| hyperspectral | 8 | 无（1 条 P3 级记录不立案） |
| atmospheric | 1 | AN-9 P3/UNC |
| georeferencing | 4 | 无 |

P1×2（AN-1 Inf 进分类训练/预测；AN-5 Inf 坍缩简单分割）、P2×6、P3/UNC×2。
共同主题：**NaN/Inf 治理只做了 NaN 一半** —— 仓库里 !isfinite 纪律已在 multires/otb/
scaler/schema 落地，但分类像素合同、简单分割器、段特征掩膜三处漏网，属同一系统性缺口的
三个修复点。
