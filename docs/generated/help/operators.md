# 遥感算子参考（自动生成）

> 本页由统一帮助系统 6.0 生成；参数类型/范围/默认值以算子 JSON Schema 为权威来源，此处仅呈现。

## GDAL Clip Raster（operator.gdal.clip）

Clip a raster by vector cutline and/or rectangular extent using GDALWarp.

## GDAL Orthorectification（operator.gdal.orthorectification）

Orthorectify a raster with RPC/GCP metadata using GDAL (optionally with a DEM).

## GDAL Pan-Sharpen（operator.gdal.pansharpen）

Pan-sharpen via the gdal_pansharpen.py utility (bilinear, LZW GTiff).

## GDAL Polygonize（operator.gdal.polygonize）

Convert a single-band label/class raster into polygons (GDALPolygonize).

## GDAL Reproject Raster（operator.gdal.reproject）

Reproject a raster dataset to a target CRS using GDALWarp.

## Build Overviews（operator.io.build_overviews）

Build overviews in place (gdaladdo semantics). Overviews are derived data; base pixels are never modified by this contract.

## Clip Raster（operator.io.clip）

Clip a raster to a declared extent in the source CRS (bounds mandatory). Refuses when the dataset carries no CRS — declare srcCrsOverride explicitly to take responsibility.

## Convert Format（operator.io.convert_format）

Convert a dataset to another format (raster and vector auto-detected). Rasters use the translate kernel; vectors stream through the foundation reader→writer contract with dataset-group atomic publish.

## Data Doctor（operator.io.doctor）

Read-only structured dataset diagnostics: readability, driver, CRS, geotransform, nodata, band metadata, scale/offset, overviews, geometry, encoding, sidecars and remote access.

## Inspect Dataset（operator.io.inspect）

Canonical metadata inspection (raster/vector/multidimensional). Read-only; never triggers a full pixel or feature scan.

## Make COG（operator.io.make_cog）

Produce a Cloud Optimized GeoTIFF through the COG driver with a safe preset (lossless_scientific / visualization / categorical / continuous_float / sar), then validate before publishing. Categorical scientific products are lossless by policy.

## Reproject Raster（operator.io.reproject）

Reproject a raster to a declared target CRS (io:warp with a minimal, explicit contract). The dataset CRS is read from the file; a CRS-less input is refused unless srcCrsOverride is declared by the caller.

## Translate Raster（operator.io.translate）

Convert a raster between formats/subsets through GDAL translate; output is staged, validated and published atomically. Metadata fidelity follows the certified profile.

## Convert Vector（operator.io.vector_convert）

Convert vector data (GPKG/GeoJSON/Shapefile/FlatGeobuf) with optional declared CRS transform, attribute filter and clip box; streams in bounded batches.

## Warp Raster（operator.io.warp）

Reproject/warp a raster through GDALWarp with an explicitly declared target CRS; resampling, resolution, alignment and extent are explicit parameters.

## Canny Edge Detector（operator.opencv.canny）

Apply Canny edge detection using OpenCV.

## Gaussian Blur（operator.opencv.gaussian_blur）

Apply Gaussian blur using OpenCV.

## Laplacian Edge Detector（operator.opencv.laplacian）

Apply Laplacian edge detection using OpenCV.

## Mean Filter（operator.opencv.mean_blur）

Apply a box (mean) filter using OpenCV cv::blur.

## Median Blur（operator.opencv.median_blur）

Apply median blur using OpenCV.

## Sobel Edge Detector（operator.opencv.sobel）

Apply Sobel edge detection using OpenCV.

## OTB Bundle To Perfect Sensor（operator.otb.bundle_to_perfect_sensor）

Pan-sharpen a multispectral image with a panchromatic image (OTB).

## OTB Compute Images Statistics（operator.otb.compute_images_statistics）

Compute mean/std-dev statistics for input images using OTB.

## OTB MeanShift Segmentation（operator.otb.meanshift_segmentation）

Run OTB Segmentation (MeanShift, connected components, watershed, etc.).

## OTB SVM Classification (Training)（operator.otb.svm_classification）

Train an OTB LibSVM classifier from raster and labelled vector data.

## ACE Detector（operator.rs.ace）

Adaptive coherence estimator: squared whitened cosine between a target spectrum and each pixel, in [0, 1].

**原理**：比较像元与目标光谱的夹角余弦（协方差白化后），对幅度不敏感。

**适用**：光照/幅度变化大但形状稳定的目标检测。

**局限**：幅度信息被舍弃

## Apply Mask（operator.rs.apply_mask）

Set masked pixels (cloud / shadow / snow) to NoData in every band of a product raster, yielding analysis-ready imagery.

**原理**：掩膜非零处像元置 NoData（或保留），输出同格网结果。

**适用**：去云、研究区裁剪、无效区剔除。

**局限**：NoData 语义需与下游工具一致

## Atmospheric Correction（operator.rs.atmospheric_correction）

Apply atmospheric correction (DOS1/DOS2/QUAC/radiance) to optical imagery.

**原理**：以辐射传输方程反演大气透过/程辐射，输出地表反射率。

**适用**：需要物理意义明确的地表反射率产品时。

**假设**：已知几何/大气参数（AOD、水汽）或其缺省近似

**局限**：参数不准时误差可能大于 DOS

深入阅读：docs/processing/grid-and-radiometric-policy.md

## Atmospheric Correction DOS1（operator.rs.atmospheric_dos1）

Dark Object Subtraction 1 (DOS1) atmospheric correction to estimate surface reflectance.

**原理**：假设影像存在零反射暗目标，以暗像元统计反演程辐射并扣除。

**适用**：无大气参数的快速校正；中低浑浊度大气效果可接受。

**假设**：影像内有真暗目标；忽略大气漫射多次散射

**局限**：浑浊/高湿大气误差大；不同景暗目标不同导致跨景不一致

## Atmospheric Correction DOS2（operator.rs.atmospheric_dos2）

Dark Object Subtraction 2 (DOS2) atmospheric correction incorporating solar zenith and transmittance.

**原理**：在程辐射扣除外再按 Rayleigh/气溶胶近似修正透过率项。

**适用**：需要比 DOS1 略精确且无外部参数时。

**局限**：仍是经验近似

## Atmospheric Correction QUAC（operator.rs.atmospheric_quac）

Quick Atmospheric Correction (QUAC) multi-band scene-statistics surface reflectance retrieval.

**原理**：假设影像包含若干已知反射率背景材质，按端元统计估计平均反射率。

**适用**：快速、免参数的地表反射率估计。

**假设**：影像包含足够材质多样性

**局限**：均质影像失效；绝对精度低于辐射传输模型

## Band Math（operator.rs.band_math）

Evaluate an arithmetic expression over raster bands.

**原理**：对每个像元按表达式计算结果，可引用各波段与常量。

**适用**：任意自定义指数、条件掩膜、单位换算。

**假设**：表达式语法正确且波段引用存在

**局限**：除零/开方负数需自行保护（NoData 语义按实现）

## Band Ratio / IHS（operator.rs.band_ratio）

Band ratio (numerator/denominator) or RGB-to-IHS color transform.

**原理**：逐像元计算 band1/band2 或归一化形式，抑制共同的光照/地形乘性因子。

**适用**：矿物/植被比值特征、光照差异抑制。

**局限**：分母为零需要保护

## Change Vector Analysis（operator.rs.change_cva）

Change Vector Analysis magnitude across all bands (sqrt of summed squared band deltas).

**原理**：逐像元计算两期多波段向量的欧氏距离作为变化强度。

**适用**：综合利用多波段信息检测总变化。

**局限**：只有幅度没有方向（变化类型需再分类）

## Change Vector Analysis Angle & Quadrant（operator.rs.change_cva_angle）

Change Vector Analysis 2-band directional angle (radians) and semantic quadrant classification.

**原理**：在幅度之外输出变化向量方向，不同方向对应不同的波段增减组合（如植被减少 vs 增加）。

**适用**：对变化进行粗分类解释。

**局限**：方向解释依赖波段顺序约定

## Change Detection（operator.rs.change_detection）

Detect changes between two co-registered raster images.

**原理**：对前后时相逐像元计算差异/比值/CVA 幅度，再以固定阈值或统计阈值（均值+k·标准差、百分位）二值化，可做形态清理并输出掩膜。

**适用**：土地覆盖变化、灾害提取、城市扩张监测。

**假设**：两期几何配准（建议优于 1 像元）；辐射基准可比（建议都做大气校正）

**局限**：物候差异产生伪变化；阈值依赖场景统计

深入阅读：docs/processing/foundation-5.md

## Change Difference（operator.rs.change_difference）

Pixel-wise difference between two co-registered rasters (after - before).

**原理**：after − before，正值/负值指示增/减方向。

**适用**：同量纲、已辐射一致性校正的数据（如 NDVI 两期）。

**局限**：整体辐射漂移直接进入结果

## Iteratively Reweighted MAD（operator.rs.change_irmad）

Iteratively Reweighted Multivariate Alteration Detection (IR-MAD) with Chi-Square sample weights.

**原理**：以 no-change 像元权重迭代重估 CCA，收敛后 MAD 分量同时可用于相对辐射归一化 (IR-MAD)。

**适用**：两期辐射差异明显时的变化检测与辐射归一化一体方案。

**假设**：足够的不变像元占多数

**局限**：迭代收敛需要计算量；变化比例过高时迭代失稳

## Change Log Ratio（operator.rs.change_log_ratio）

Log-Ratio change metric ln(after + eps) - ln(before + eps).

**原理**：对数域差值使乘性斑点/光照因子近似加性高斯，便于统计阈值。

**适用**：SAR 变化检测的标准度量。

**局限**：零值必须先处理

## Multivariate Alteration Detection（operator.rs.change_mad）

Multivariate Alteration Detection change magnitude (canonical correlation analysis, multi-pass streaming).

**原理**：对两期做 CCA 得到典型变量对，差值 (MAD) 按方差升序排列，低方差分量携带变化信息。

**适用**：多波段两期比较，需要自动分离变化与不变成分。

**假设**：两期已配准；足够样本估计统计

**局限**：分量选择需要经验或迭代

## Change Normalized Difference（operator.rs.change_normalized_difference）

Normalized difference change (after - before) / (after + before).

**原理**：(after−before)/(after+before)，类似逐像元归一化比值。

**适用**：幅度漂移较大但相对变化关心的场景。

**局限**：值接近零的分母需要保护

## Change Ratio（operator.rs.change_ratio）

Ratio change after / before (NaN where before is <= 0).

**原理**：after/before，未变化区接近 1，对乘性光照差异稳健。

**适用**：未做辐射归一化的数据。

**局限**：零值需保护

## Spectral Angle Mapper Change（operator.rs.change_sam）

Spectral Angle Mapper (SAM) change angle (radians) across multi-spectral bands.

**原理**：计算两期光谱向量的夹角，对幅度差异不敏感、只看形状。

**适用**：光照/幅度变化大、形状变化更有意义的多/高光谱数据。

**局限**：纯幅度变化（亮度）不敏感是双刃剑

## Connected Components（operator.rs.connected_components）

Deterministic connected-component labeling of a 0/1 mask (raster-order compact labels).

**原理**：4/8 邻接连通域识别并编号，用于图斑统计。

**适用**：变化图斑计数、对象化预处理。

## Continuum Removal（operator.rs.continuum_removal）

Normalize each pixel's reflectance spectrum to its convex-hull continuum.

**原理**：拟合光谱包络线并把反射率除以包络，使吸收深度/位置可比。

**适用**：矿物填图、叶绿素/氮素吸收特征分析。

**假设**：光谱分辨率足以刻画吸收特征

**局限**：端点选择影响包络形状

## Contrast Stretch（operator.rs.contrast_stretch）

Stretch each band (linear / percent-clip / stddev / histogram / piecewise).

**原理**：把原始值映射到显示范围：线性按 min/max，百分比裁剪截除尾部极值，均衡按累计直方图重分布。

**适用**：显示与制图；不影响后续定量分析（保留原始数据）。

**局限**：输出拉伸后数据不宜再用于定量反演

## Object Detection (Model)（operator.rs.detect）

Run a detection model (output.detection contract) over a raster; publishes georeferenced detection boxes as vector output with NMS tile dedup.

**原理**：检测网络输出目标框（可转矢量），含置信度与类别。

**适用**：船只/车辆/树冠等目标级提取。

**局限**：小目标与密集目标性能受限

## DN to Radiance（operator.rs.dn_to_radiance）

Calibrate raw Digital Numbers (DN) to spectral radiance using sensor gain and bias.

**原理**：L = gain·DN + offset，逐波段应用定标系数。

**适用**：辐射处理链第一步。

**假设**：产品提供定标参数

## Feature Embedding (Model)（operator.rs.embedding）

Run an embedding model on a raster; writes the feature stack (aggregate=mean adds a per-scene mean feature vector to the result).

**原理**：以预训练网络把影像编码为低维特征堆栈。

**适用**：小样本分类（特征复用）、相似性检索。

**局限**：嵌入域偏移影响下游任务

## Endmember Extraction (PPI)（operator.rs.endmember_extraction）

Extract spectral endmembers by Pixel Purity Index.

**原理**：在高维特征空间寻找构成数据单形体的顶点光谱。

**适用**：解混前的端元获取；高光谱数据效果最佳。

**局限**：纯像元不存在时端元为虚拟混合

## Enhanced Vegetation Index (EVI)（operator.rs.evi）

Compute Enhanced Vegetation Index: 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1).

**原理**：EVI = G·(NIR−Red)/(NIR+C1·Red−C2·Blue+L)，引入蓝光与增益项。

**适用**：茂密植被区、跨期比较需要低饱和时。

**假设**：需要蓝光波段

**局限**：传感器缺蓝波段时不可用

## Extract Bands（operator.rs.extract_bands）

Extract one or more bands into a new multi-band raster.

**原理**：复制所选波段到新栅格，无损保留地理信息。

**适用**：精简数据、单独处理某一波段。

## Feature Normalization（operator.rs.feature_normalize）

Standardize feature cube bands (zscore, minmax or robust) and store the fit statistics inside the cube contract so training and inference share the exact same normalization; supports inverting stored stats.

**原理**：逐波段线性缩放到 [0,1] 或零均值单位方差。

**适用**：距离/核度量模型（SVM/KMeans）前的尺度统一。

**局限**：极值会影响 min-max

## Feature Band Selection（operator.rs.feature_select）

Keep (or with complement, drop) feature cube bands by id, semantic role or 1-based index, renumbering the bands and keeping the feature contract intact for model-input matching.

**原理**：以模型重要性、相关性阈值或包装法挑选低冗余高判别特征。

**适用**：特征爆炸、需要提速或解释时。

**局限**：选择偏差：应在验证集外评估

## Feature Cube Builder（operator.rs.feature_stack）

Stack multiple co-registered raster bands into a self-describing multimodal feature cube (optical indices, SAR backscatter/texture, DEM derivatives, temporal metrics) with per-band feature identity metadata for model-input matching.

**原理**：把多源特征（光谱、指数、纹理、SAR、地形）按统一格网堆叠。

**适用**：分类/回归前统一特征入口。

**假设**：所有输入已配准同格网

**局限**：格网不一致时需先重采样

## Fill Holes（operator.rs.fill_holes）

Fill background regions that do not touch the raster border (interior holes become foreground).

**原理**：识别被同类包围的孔洞并填充。

**适用**：水体/云掩膜后处理。

**局限**：大面积真实孔洞会被误填

## Focal Statistics（operator.rs.focal_stats）

Window statistics (mean, sum, min, max, stddev, range) with the replicate edge policy.

**原理**：窗口内按所选统计量聚合输出，等价于低通/纹理度量族。

**适用**：背景场估计、局部对比特征。

## Fusion Brovey（operator.rs.fusion_brovey）

Brovey transform pan-sharpening fusion.

**原理**：各波段乘以 (PAN/ΣMS) 的归一化注入。

**局限**：高反射区易饱和

## Fusion Gram-Schmidt（operator.rs.fusion_gram_schmidt）

Gram-Schmidt pan-sharpening fusion.

**原理**：以模拟低分辨率全色为第一向量做 GS 正交化，替换后逆变换。

**适用**：对光谱保真要求较高的融合。

**局限**：计算量高于 Brovey/IHS

## Fusion IHS（operator.rs.fusion_ihs）

Intensity-Hue-Saturation pan-sharpening fusion.

**原理**：RGB→IHS，用 PAN 替换 I 后逆变换。

**局限**：仅适合 3 波段

## Fusion Linear（operator.rs.fusion_linear）

Linear pan-sharpening fusion.

**原理**：多光谱上采样后与全色的线性加权组合。

**局限**：光谱失真较大

## Fusion PCA（operator.rs.fusion_pca）

Principal Component Analysis pan-sharpening fusion.

**原理**：MS 做 PCA，PC1 与 PAN 直方图匹配后替换，逆变换输出。

**局限**：统计依赖整幅场景

## Image Enhancement（operator.rs.image_enhancement）

Contrast stretch, spatial filter, band ratio/IHS, or SAR speckle filter.

**原理**：卷积核滑动计算：低通（均值/高斯）去噪，中值保边去噪，拉普拉斯锐化边缘。

**适用**：去噪、细节增强。

**局限**：核大小与边缘损失之间的权衡

## Image Fusion（operator.rs.image_fusion）

Fuse panchromatic and multispectral imagery (pan-sharpening).

**原理**：把多光谱的低空间分辨率色彩信息与全色高分辨率细节合成高分辨率多光谱影像。

**适用**：高分辨率目视解译、对象分割前的细节增强。

**假设**：全色与多光谱已配准（亚像元）；光谱范围重叠合理

**局限**：融合改变光谱保真度，定量分析慎用融合产品

## On-Device Inference (ONNX)（operator.rs.infer）

Run an ONNX model on a raster via the model runtime (tiled, bounded memory, manifest-driven pre/post-processing).

**原理**：加载模型资产，按模型输入约定准备影像并推理，输出类别/回归结果。

**适用**：已训练模型（语义分割/目标检测/回归）的业务化批量应用。

**假设**：输入分辨率/波段/归一化与训练一致；模型与运行时兼容

**局限**：跨域（传感器/区域）迁移需评估

## K-Means Classification（operator.rs.kmeans_classification）

Unsupervised K-Means clustering of multi-band raster pixels.

**原理**：随机/采样初始化 K 个中心，迭代“分配-更新”直到收敛，输出簇号图。

**适用**：无样本时的光谱分区、样本预选辅助。

**假设**：簇在特征空间近似球形；特征已归一化更稳

**局限**：簇号无语义，需要人工归并到类别；K 与初始化影响结果

## Landsat Product Import（operator.rs.landsat_import）

Import a Landsat scene (MTL + bands) into a multi-band GeoTIFF.

**原理**：读取 MTL 元数据，按产品组装分析就绪堆栈（含 QA 波段），可选直接输出定标反射率。

**适用**：Landsat 8/9 数据进入处理链的第一步。

**假设**：完整产品包（含 MTL）

**局限**：Pre-Collection 产品不支持

## Local Extrema（operator.rs.local_extrema）

Flag window maxima or minima as 1/0.

**原理**：窗口比较中心像元与邻域，标记极值点。

**适用**：特征点/树峰/热异常定位。

## 3x3 Majority Filter（operator.rs.majority_filter）

Apply a majority sliding-window filter to a classification raster to reduce noise.

**原理**：窗口内取出现最多的类别替换中心，仅用于类别图。

**适用**：分类结果后处理。

**局限**：会抹掉细小真实图斑，谨慎设置窗口

## Matched Filter（operator.rs.matched_filter）

Matched filter detection of a target spectrum against the scene background: signed whitened projection per pixel.

**原理**：把像元投影到目标方向并按背景协方差白化，输出目标存在性得分。

**适用**：已知目标光谱（矿物/材料）的探测。

**假设**：背景高斯、目标亚像元

**局限**：目标光谱失配会漏检

## Modified Normalized Difference Water Index (MNDWI)（operator.rs.mndwi）

Compute Modified Normalized Difference Water Index: (Green - SWIR) / (Green + SWIR).

**原理**：用 SWIR 替代 NIR，拉大水体与阴影/建筑的反差。

**适用**：城镇周边或阴影干扰区的水体提取。

**假设**：需要 SWIR 波段

**局限**：积雪与水体易混

## MNF (Minimum Noise Fraction)（operator.rs.mnf）

Minimum Noise Fraction transform for hyperspectral dimensionality reduction.

**原理**：估计噪声协方差做白化后执行 PCA，成分按信噪比而非方差排序。

**适用**：高光谱降噪：保留高 SNR 成分做后续分析。

**局限**：噪声估计质量决定效果

## MODIS Georeference（operator.rs.modis_georeference）

Assign MODIS sinusoidal tile georeference and optionally reproject (e.g. to EPSG:4326).

**原理**：按正弦网格参数生成地理变换并转换到目标 CRS。

**局限**：插值会平滑像元

## MODIS Product Import（operator.rs.modis_import）

Import a MODIS HDF/GeoTIFF product into a multi-band GeoTIFF (with sinusoidal tile georeference when h/v known).

**原理**：选择子数据集、应用 scale factor 并重投影到目标 CRS。

**适用**：MODIS 指数产品（NDVI/LST）进入分析。

**局限**：低分辨率决定其用于大尺度分析

## Morphology（operator.rs.morphology）

Binary morphology over a 0/1 mask: erode, dilate, open, close (4/8-connectivity, iterations).

**原理**：结构元素在二值图上的极值运算：开运算去小斑点，闭运算补小孔。

**适用**：掩膜清理、水体/建筑图斑整形。

**局限**：改变图斑边界精度

## Raster Mosaic（operator.rs.mosaic）

Mosaic multiple rasters (band 1) into a single GeoTIFF covering their union extent.

**原理**：把多景重投影到统一格网后逐像元合并，重叠区按次序/羽化/统计决定取值。

**适用**：大区域底图生产；时序/光谱合成前的基础数据准备。

**假设**：各景 CRS/分辨率可对齐；重叠区辐射差异已控制（先做归一化更佳）

**局限**：接缝线处理简化时重叠区可见色差

## Normalized Difference Built-up Index (NDBI)（operator.rs.ndbi）

Compute Normalized Difference Built-up Index: (SWIR - NIR) / (SWIR + NIR).

**原理**：建筑/不透水面 SWIR 高于 NIR 的特征比值。

**适用**：建成区粗提取，常与 NDVI/NDWI 组合使用。

**局限**：裸土易混入

## Normalized Difference Vegetation Index (NDVI)（operator.rs.ndvi）

Compute Normalized Difference Vegetation Index: (NIR - Red) / (NIR + Red).

**原理**：(NIR−Red)/(NIR+Red)，对绿色植被活性敏感。

**适用**：植被覆盖与长势快速监测。

**假设**：地表反射率输入最佳

**局限**：高生物量饱和；土壤背景影响低覆盖区

## Normalized Difference Water Index (NDWI)（operator.rs.ndwi）

Compute Normalized Difference Water Index: (Green - NIR) / (Green + NIR).

**原理**：利用水体绿光高反射/近红外低反射的反差识别水面。

**适用**：开阔水体提取。

**局限**：建筑/阴影易混；浑浊水体阈值需调整

## OBIA Classification（operator.rs.obia_classify）

Classify objects from polygon training or pre-labeled segments.

**原理**：与监督分类相同模型族，但样本/预测单元为对象特征向量。

**适用**：高分辨率地物制图。

**局限**：分割质量成为精度上限

## OBIA Object Features（operator.rs.obia_features）

Extract per-object spectral, GLCM texture and shape features to CSV.

**原理**：对每个对象聚合均值/标准差、面积/周长/形状指数与 GLCM 纹理等。

**适用**：对象分类前的特征工程。

**局限**：特征维数高时需要筛选

## OBIA Hierarchy (OTB)（operator.rs.obia_hierarchy）

Build or reuse a two-level object hierarchy; optionally classify a level.

**原理**：由细到粗逐级合并对象，记录层级拓扑供跨尺度分析。

**适用**：需要多尺度对象（树冠/林分）的复杂场景。

**局限**：层级数增加内存与管理成本

## OBIA ROI Labeling（operator.rs.obia_label）

Label objects by pixel majority of training polygons (CSV output).

**原理**：按空间叠加把样本多边形的类别赋给对象；重叠度阈值控制纯度。

**适用**：对象分类的训练准备。

**局限**：跨界对象归属需规则处理

## OBIA Segmentation（operator.rs.obia_segment）

Segment imagery into objects (teaching segmenter or OTB MeanShift).

**原理**：把像元聚合为同质对象（engine 可选 mean-shift 等分割引擎）：mean-shift 在空间-光谱联合域迭代寻找众数形成对象。

**适用**：高分辨率影像分类前；避免椒盐现象。

**假设**：分割参数与目标地物尺度匹配

**局限**：单一尺度难以兼顾大小地物；过分割/欠分割都损害精度

## Principal Component Analysis（operator.rs.pca）

Compute PCA components of a multi-band raster and write them as a GeoTIFF.

**原理**：对波段协方差矩阵特征分解，前几个主成分集中大部分方差；支持逆变换。

**适用**：压缩波段、噪声分离（后几个成分）、变化信息集中。

**假设**：整幅统计（内存策略通常 full raster）

**局限**：成分物理意义不直观；全局统计对流式场景代价高

## Post-Classification Change（operator.rs.post_classification_change）

Compare two thematic rasters and report the per-class transition matrix, gains/losses, and a change-type map.

**原理**：逐像元对比两期类别标签，输出“前类→后类”编码与混淆式转移矩阵。

**适用**：需要变化类型（不只变化与否）的成果；规避两期辐射差异问题。

**假设**：两期分类体系一致；分类精度已评估

**局限**：分类误差会在变化图中加倍；建议只报告显著转移类别

## Proximity（operator.rs.proximity）

Exact Euclidean distance (pixels) to the nearest foreground cell; unreachable cells are NaN.

**原理**：精确欧氏距离变换输出距离栅格（单位：像素或地图单位）。

**适用**：缓冲区分析、距离特征构造。

## QA Mask（operator.rs.qa_mask）

Derive a cloud / cloud-shadow / snow mask from Landsat QA_PIXEL or Sentinel-2 SCL quality bands.

**原理**：按位（Landsat QA_PIXEL）或类别值（S2 SCL）解析质量波段，输出 0/1 掩膜。

**适用**：时序合成、镶嵌、指数计算前的去云步骤。

**假设**：QA 波段语义与产品版本匹配

**局限**：云影/薄云漏检是常态，可叠加阈值辅助

## Radiometric Calibration（operator.rs.radiometric_calibration）

Convert DN to radiance, TOA reflectance, or brightness temperature from Landsat MTL / Sentinel-2 MTD / generic GDAL scale-offset metadata.

**原理**：按产品定标参数输出辐亮度，或进一步换算为太阳辐照度归一的表观反射率 (TOA)。

**适用**：所有定量处理前；跨期比较的基础。

**假设**：产品元数据完整

## Class Recode（operator.rs.recode）

Remap integer class labels according to a recode mapping table.

**原理**：按 provided 映射替换类别值，可合并类别。

**适用**：分类体系转换、聚类簇→语义类别归并。

## RX Anomaly Detection（operator.rs.rx_anomaly）

Reed-Xiaoli anomaly detection (Mahalanobis distance to scene background).

**原理**：计算像元到背景统计（均值+协方差）的马氏距离，距离越大越异常。

**适用**：无目标先验的小目标/异常探测。

**假设**：背景近似高斯分布

**局限**：背景非均匀时虚警率高

## Spectral Angle Mapper (SAM) Classification（operator.rs.sam_classify）

Classify multi-band imagery by spectral angle to reference spectra.

**原理**：计算像元光谱到每个端元光谱的夹角，取最小角对应类别，超阈值判为未知。

**适用**：高光谱矿物/目标分类；光照梯度明显场景。

**假设**：端元光谱已知；光谱形状稳定

**局限**：幅度（亮度）信息被忽略；端元代表性决定上限

## SAR Backscatter Conversion（operator.rs.sar_backscatter）

Convert SAR backscatter between sigma0/gamma0/beta0 states and linear power or dB using a constant or per-pixel incidence angle.

**原理**：输出定标后的后向散射强度，可选分贝变换便于显示与统计。

**适用**：水面/植被/建成区的散射对比分析，或作为分类特征。

**假设**：输入已辐射定标

**局限**：dB 变换便于人眼判读但不改变信息量

## SAR Radiometric Calibration（operator.rs.sar_calibrate）

Calibrate SAR digital numbers (DN) to sigma0 backscatter (linear power or dB) with optional noise subtraction.

**原理**：用定标常数 LUT 把像元值换算为物理量纲的后向散射系数，使不同景/传感器数据可比。

**适用**：任何 SAR 定量处理的第一步：滤波、地形辐射校正、变化检测、时序分析之前。

**假设**：产品元数据含定标参数

**局限**：热噪声未去除时低后向散射区（水面）可能偏高

深入阅读：docs/processing/sar-domain.md

## SAR Change Detection（operator.rs.sar_change）

Detect change between two co-registered SAR scenes: log-ratio magnitude (dB) thresholded into a change mask (manual, Otsu, percentile or statistical).

**原理**：对数比值把乘性斑点噪声转为加性，统计阈值（如 ki/两参数 CFAR 思路）分离变化与未变化。

**适用**：洪水、建筑变化、作物收割等 SAR 时相对比。

**假设**：两期已配准且定标一致；建议已抑斑

**局限**：几何失配直接映射为伪变化

## SAR Dual-Pol Features（operator.rs.sar_dualpol_features）

Dual-polarization feature rasters (ratio, normalized difference, log ratio, dual-pol RVI, span) from VV/VH calibrated backscatter.

**原理**：组合 VV、VH 生成比值 (VH/VV)、总功率 (VV+VH)、雷达植被指数等特征。

**适用**：SAR 单独分类或与光学融合前的特征工程。

**假设**：输入为已定标的 VV/VH 强度对

**局限**：通道缺失时特征不可算

## SAR Ratio / Log-Ratio（operator.rs.sar_ratio）

Ratio, log-ratio or absolute log-difference magnitude between two co-registered SAR scenes (incoherent change pair metric).

**原理**：同极化/交叉极化强度相除，VH/VV 低值常指示水面或光滑地表，高值指示体散射（植被）。

**适用**：作物/植被监测、水体识别的特征构造。

**假设**：两极化通道已配准且定标一致

**局限**：零值分母需要 NoData 保护

## SAR Speckle Filter（operator.rs.sar_speckle）

Despeckle a SAR intensity raster with Lee, enhanced Lee, Frost, Kuan, Gamma-MAP, refined Lee or multitemporal filtering.

**原理**：经典自适应滤波用局部统计区分斑点与真实结构； refined Lee 结合边缘方向；多时相模式跨配准影像联合估计降低噪声同时保留时间变化。

**适用**：SAR 解译/分类/变化检测前的降噪；多时相模式适合同区域多景数据。

**假设**：输入为强度（功率）数据而非幅度/dB；多时相模式需要同格网配准影像

**局限**：多时相门限拒绝偏离参考超过 k·局部标准差的像元，被拒像元回退为时间均值；所有滤波假设强度域数据

深入阅读：docs/processing/sar-domain.md

## SAR Terrain Correction（operator.rs.sar_terrain_correction）

DEM terrain correction product: terrain-flattened gamma0 with a layover/shadow validity mask and the local incidence angle band.

**原理**：用轨道参数 + DEM 做 RD 正射纠正，消除斜距投影与地形引起的几何位移。

**适用**：SAR 数据进入栅格分析前的标准几何步骤。

**假设**：有轨道元数据与 DEM

**局限**：阴影/叠掩区输出 NoData

## SAR Terrain Flattening（operator.rs.sar_terrain_flatten）

Radiometric terrain flattening: sigma0 to gamma0 (sigma0·cosθ0/cosθi) using a co-registered DEM in radar geometry.

**原理**：按真实散射面积把 sigma0 归一化为参考椭球/局部入射几何下的 Γ⁰，消除坡面辐射畸变。

**适用**：山地 SAR 时序、比较与分类之前；与辐射定标配合使用。

**假设**：有配准的 DEM；已知几何（轨道/视线）

**局限**：叠掩/阴影区仍不可恢复；DEM 质量直接影响结果

深入阅读：docs/processing/sar-domain.md

## SAR Terrain Masks（operator.rs.sar_terrain_masks）

Local incidence angle and geometric layover/shadow masks from a DEM under declared constant SAR geometry (not full range-Doppler).

**原理**：以局部入射角与视线几何标记几何畸变区，输出掩膜供质量标记与统计剔除。

**适用**：山地 SAR 质量控制、无效区标记。

**假设**：需要 DEM 与影像几何元数据

**局限**：掩膜精度受 DEM 分辨率限制

## SAR GLCM Texture（operator.rs.sar_texture）

Compute GLCM (Haralick) texture measures over a sliding window of a SAR intensity raster; one Float32 output band per measure.

**原理**：在滑动窗口内计算灰度共生矩阵统计量，把空间结构信息量化为特征波段。

**适用**：城市/植被/作物的分类特征增强；斑点滤波后使用更稳。

**假设**：建议先抑斑再提纹理

**局限**：窗口大小决定纹理尺度，需要与地物匹配

## Soil-Adjusted Vegetation Index (SAVI)（operator.rs.savi）

Compute Soil-Adjusted Vegetation Index: ((NIR - Red) / (NIR + Red + 0.5)) * 1.5.

**原理**：((NIR−Red)/(NIR+Red+L))·(1+L)，L≈0.5 时土壤影响最小。

**适用**：干旱半干旱稀疏植被区替代 NDVI。

**局限**：L 需按覆盖度调整

## Semantic Segmentation (Model)（operator.rs.segment）

Run a segmentation model on a raster; output format defaults to the manifest (probability stack, argmax labels, binary mask or confidence band).

**原理**：滑窗/整图推理分割网络，输出类别概率与标签图。

**适用**：建筑/道路/水体等高精度逐像元提取。

**假设**：影像域与训练数据相近

**局限**：大影像内存策略与分块拼接接缝

## Segment Statistics（operator.rs.segment_stats）

Compute per-segment mean spectra and area from a label raster.

**原理**：对每个对象计算所选统计并输出属性表/矢量。

**适用**：对象质检、特征核查。

## Sentinel-2 Product Import（operator.rs.sentinel2_import）

Import a Sentinel-2 SAFE product into a multi-band GeoTIFF.

**原理**：组装 10/20/60 m 波段到统一格网（按目标分辨率重采样），保留 SCL。

**适用**：S2 分析就绪数据准备。

**假设**：完整 SAFE 包

## Sieve（operator.rs.sieve）

Remove foreground components smaller than a minimum area (pixels).

**原理**：按连通域面积阈值过滤，小于阈值的图斑并入相邻大类。

**适用**：分类后处理的最小制图单元控制。

## Spectral Derivative（operator.rs.spectral_derivative）

First or second spectral derivative along the wavelength axis (finite differences; requires a wavelength axis).

**原理**：差分近似光谱导数，抑制缓变背景、突出变化率特征。

**适用**：植被红边分析、矿物吸收边定位。

**局限**：放大噪声，常先平滑

## Spectral Index（operator.rs.spectral_index）

Compute a spectral index (NDVI, EVI, SAVI, NDWI, NDBI, MNDWI, NBR, dNBR, BSI, NDRE, CI, NDSI, NDTI) from raster bands. Scale rule (#680): EVI/SAVI constants assume unit reflectance [0,1]; when the input carries SICNU_NUMERIC_SCALE (stamped at Level-2 import), the participating bands are divided by it for the computation, while ratio indices are scale-invariant and inputs are never rescaled on disk.

**原理**：按所选指数公式组合指定波段像元值，输出单波段指数影像。

**适用**：植被/水体/建筑等信息提取的第一步特征。

**假设**：波段对应正确（红/近红外/蓝/短波红外）；定量分析建议用地表反射率输入

**局限**：指数饱和（如高生物量 NDVI）；未校正数据阈值需按传感器调整

深入阅读：docs/processing/foundation-5.md

## Spectral Resampling（operator.rs.spectral_resample）

Resample spectra onto a target wavelength grid by linear interpolation.

**原理**：按目标波段的光谱响应函数加权积分源光谱。

**适用**：跨传感器比较、以高光谱模拟多光谱。

**假设**：已知响应函数或等效波长

**局限**：窄特征可能被平滑掉

## Linear Spectral Unmixing（operator.rs.spectral_unmixing）

Estimate per-pixel endmember abundances by linear spectral unmixing.

**原理**：假设像元光谱为端元光谱的线性组合，反演各端元比例（FCLS 等约束）。

**适用**：亚像元覆盖度估计（植被/土壤/不透水比例）。

**假设**：端元光谱已知或已提取；线性混合假设成立

**局限**：多次散射/非线性混合时误差大

## Supervised Classification（operator.rs.supervised_classification）

Train or apply SVM/NormalBayes classification on multi-band rasters.

**原理**：以 training 矢量的类别字段 + 影像特征训练分类器，输出类别图与可选概率图；testSplit 划分验证样本估计精度。

**适用**：有可靠样本的标准土地覆盖分类任务。

**假设**：样本代表性覆盖各类别光谱变异；验证样本与训练样本空间独立（防泄漏）

**局限**：样本偏差直接进入模型；类别体系需互斥完备

深入阅读：docs/processing/foundation-5.md

## Temporal Anomaly（operator.rs.temporal_anomaly）

Per-pixel anomaly of a target date against a baseline date range of the same collection: z-score or difference from the baseline mean. Two streaming passes (baseline accumulation, target scoring); degenerate baselines (stddev 0, too few observations) yield NoData instead of silent numbers.

**原理**：以多年同期的均值/标准差为基线，输出 (x−μ)/σ 标准异常。

**适用**：干旱/洪涝/热 stress 的年度对比。

**假设**：基线期稳定且足够长

**局限**：基线受极端年份影响

## Temporal Breakpoints（operator.rs.temporal_breakpoints）

Per-pixel piecewise-linear trend segmentation with break detection (BSFAST-lite): the series (real acquisition day offsets) is split greedily where an additional OLS segment reduces the residual sum of squares the most, while the relative RSS reduction exceeds minImprovement and both sides keep the minimum segment sample count (derived from minSegmentDays and the collection time span). Outputs the break count, break dates (day offsets from the first acquisition), one per-day slope per segment and the overall RMSE.

**原理**：以分段回归/统计检验定位趋势或季节成分的突变时间。

**适用**：采伐、火灾、建设等扰动事件检测。

**假设**：突变前后各有稳定时段

**局限**：渐变过程不易定位；短序列误报率升高

## Best Pixel Composite（operator.rs.temporal_composite）

Temporal composites (best-pixel, mean or median) over a multi-date collection, optionally grouped by month / quarter / season / year / custom period. Every output carries a valid-observation-count band and the chosen observation's quality score.

**原理**：把集合内的景按 period 聚合，对每像元取 min/median/mean 等统计量；可按 QA 波段先剔除云污染。

**适用**：去云无缝底图生产、季度/年度植被基线。

**假设**：景已配准同格网；云掩膜已就绪（或用 QA 波段）

**局限**：周期内有效观测过少时仍含云斑

深入阅读：docs/temporal/

## Temporal Decomposition（operator.rs.temporal_decompose）

Additive seasonal-trend decomposition of a per-pixel time series: a Whittaker-smoothed trend, a day-of-year climatology seasonal component (circularly smoothed over seasonal_window_days) and a remainder, computed on real acquisition day offsets. Bands are grouped per requested component, one band per scene date.

**原理**：STL/移动平均分解输出趋势项、季节项与残差项堆栈。

**适用**：分别分析长期变化与周期动态。

**局限**：周期参数需与数据频率匹配

## Extract Temporal Series（operator.rs.temporal_extract_series）

Extract a time series at a point or inside a polygon ROI from a multi-date collection. Points return (time, value, valid); ROI pixels are bounded by the polygon bounding box and summarized per date (mean/median/min/max/stddev/valid_count). Output: CSV plus the JSON series; missing observations stay missing (no interpolation).

**原理**：在指定点/面内逐期取样，输出 id/日期/值表格，供曲线绘图与统计。

**适用**：样本点检查、地物光谱曲线时间对比。

**局限**：面提取的聚合方式（均值）影响结果

## Temporal Gap Fill（operator.rs.temporal_gap_fill）

Time-aware interpolation of missing (masked/NaN) samples in a per-pixel time series: linear in acquisition days or nearest valid neighbour, never bridging gaps wider than max_gap_days and never extrapolating past the ends of the series. Valid samples copy through unchanged; a final filled_count band documents how many samples were synthesised per pixel.

**原理**：时间线性/样条插值或邻近均值填充缺失观测，并标记插补掩膜。

**适用**：谐波/趋势分析前的规则化采样。

**局限**：长缺口插补会引入人工形态

## Temporal Harmonic Fit（operator.rs.temporal_harmonic_fit）

Per-pixel harmonic regression (annual + sub-annual Fourier terms) across a multi-date collection: y(t) = a0 + Σ_k [ a_k·sin(2πkt/365.25) + b_k·cos(2πkt/365.25) ] with t in real acquisition days since the collection reference epoch. Solved in closed form (weighted least squares) with optional IRLS outlier damping. Outputs one fitted band per acquisition date plus RMSE and R²; optionally the raw coefficients. Pixels with fewer than minObservations valid samples stay NoData.

**原理**：以正弦/余弦基拟合年内周期，输出幅度/相位（物候时间）与残差。

**适用**：稳定季节动态的建模、去云重构。

**局限**：非周期过程（砍伐）不适用；阶数过高拟合噪声

## Temporal Index Series（operator.rs.temporal_index_series）

Spectral index time series: computes NDVI/EVI/SAVI/NDWI/NDBI/MNDWI/NBR/NDRE/NDSI/NDTI for every date of a temporal collection using the single-scene spectral-index kernels, output as one stacked raster (one band per date) with per-band acquisition metadata.

**原理**：对每期影像计算所选指数后堆叠为时间序列栅格。

**适用**：物候/趋势分析的特征准备。

## Temporal Monitor（operator.rs.temporal_monitor）

Per-pixel temporal monitoring: CUSUM and EWMA of standardized anomalies, or seasonal Mann-Kendall trend (calendar-month seasons).

**原理**：对每个新观测与历史基线/模型比较，超限输出告警与置信度。

**适用**：森林扰动、作物长势滚动监测。

**假设**：历史基线可用

**局限**：告警阈值需按目标调校

## Temporal Phenology Metrics（operator.rs.temporal_phenology）

Seasonal phenology metrics per pixel from a vegetation-index time series: start/peak/end of season (SOS/POS/EOS, day-of-year), season length (LOS, days), amplitude, base level and the small integral of the index over the season. Threshold method: SOS/EOS are the first/last crossings of base + crossingFraction·amplitude inside the season window [seasonStartDoy, seasonEndDoy] (a window that wraps the year end is supported). Metrics are computed once per pixel over the whole series for the requested season window; pixels with fewer than minValidPerSeason valid in-season samples stay NoData.

**原理**：以阈值/曲率法从平滑曲线判定 SOS/EOS/POS 与季节积分。

**适用**：农业物候监测、生态系统研究。

**假设**：单峰或可分离多峰的季节形态

**局限**：双季作物需要多峰处理

## Temporal Sen Trend（operator.rs.temporal_sen_trend）

Per-pixel non-parametric monotonic trend across a multi-date collection: Sen's median slope (median of all pairwise day slopes — robust to outliers) with the tie-corrected Mann-Kendall test (continuity-corrected z, two-sided p-value). Uses the real acquisition day offsets. Outputs slope, intercept, z, p_value and the valid observation count; undefined results are NaN, never zero.

**原理**：Theil–Sen 以成对斜率中位数估计趋势，抗离群；MK 检验给出显著性。

**适用**：含云残余/离群值的长期植被趋势。

**局限**：计算量高于普通回归；数据点少时功效不足

## Temporal Smoothing（operator.rs.temporal_smooth）

Quality-aware smoothing (Savitzky–Golay, Whittaker or moving average) of a per-pixel time series across a multi-date collection. NaN samples are treated as absent; one output band per scene date, same order, so the result stays stackable with acquisition metadata.

**原理**：时间维低通滤波抑制云残余抖动，保留季节形态。

**适用**：物候参数提取前的去噪。

**局限**：过度平滑抹平真实突变

## Temporal Summary（operator.rs.temporal_summary）

Per-pixel temporal statistics over a multi-date collection: count, valid_count, mean, min, max, stddev (Welford) and optional exact median. Values follow the shared temporal validity contract (NoData/NaN/QA-cloud masked samples are excluded).

**原理**：对每像元时间维计算基本统计量，count 可作为质量层。

**适用**：快速了解时间动态幅度与有效观测数。

**局限**：极值对云残余敏感

## Temporal Linear Trend（operator.rs.temporal_trend）

Per-pixel linear trend (ordinary least squares) across a multi-date collection using real acquisition time intervals (days since the collection reference epoch). Outputs slope, intercept, R², valid observation count and RMSE; the regression accumulates online (numerically stable, O(tile) memory independent of date count).

**原理**：以时间为自变量逐像元最小二乘拟合，输出斜率（变化速率）与显著性。

**适用**：植被退化/恢复、城市化的长期方向性判断。

**假设**：观测数足够；趋势近似线性

**局限**：对离群值敏感；非线性过程误拟合

## Terrain Analysis（operator.rs.terrain_analysis）

Compute slope, aspect, hillshade, roughness, TRI, or TPI from a DEM.

**原理**：以邻域差分估计表面导数：坡度为梯度幅值，坡向为梯度方向，山影按光照角度合成。

**适用**：地貌判读、水文prep、地形校正输入、制图晕渲。

**假设**：DEM 已配准；垂直基准与水平分辨率匹配（z 因子）

**局限**：分辨率决定可提取的地形尺度；边缘像元受窗口截断影响

## Terrain Flow（operator.rs.terrain_flow）

Depression filling (priority-flood), D8 flow directions, and drainage accumulation over a DEM.

**原理**：先填平洼地保证 D8/MFD 流向连续，再计算流量累积并按阈值提取河网与流域。

**适用**：河网制图、流域划分、水土保持分析。

**假设**：DEM 质量良好；平坦区流向可能不唯一

**局限**：大型平坦/城市区 D8 结果呈平行条纹；填洼会改变原始高程

## Threshold Raster（operator.rs.threshold_raster）

Threshold a raster into a binary mask (manual/Otsu/percentile/statistical).

**原理**：单波段按阈值离散化；OTSU 自动最大化类间方差。

**适用**：指数图→水体/变化掩膜等二值产品。

**局限**：多峰直方图场景单一阈值不足

## Topographic Correction（operator.rs.topographic_correction）

Topographic correction of reflectance over a co-registered DEM (cosine, C/SCS+C, or Minnaert illumination models).

**原理**：以 DEM 计算局部入射角余弦，按所选模型把倾斜面反射率归一到等效水平面光照。

**适用**：山区植被/覆盖分类与镶嵌前，消除阴阳坡光谱差异。

**假设**：DEM 与影像配准（建议优于 1 像元）；朗伯（C/SCS+C）或参数化非朗伯（Minnaert）假设

**局限**：陡坡低光照角可能过补偿；模型选择影响植被坡面一致性

深入阅读：docs/processing/grid-and-radiometric-policy.md

