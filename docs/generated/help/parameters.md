# 参数知识参考（自动生成）

> 类型/范围/默认值由算子 Schema 推导；单位/推荐值/权衡为人工知识层。

### parameter.gdal.clip.cropToCutline


### parameter.gdal.clip.cutline


### parameter.gdal.clip.extent


### parameter.gdal.clip.input


### parameter.gdal.clip.nodata


### parameter.gdal.clip.output


### parameter.gdal.clip.resampling


### parameter.gdal.orthorectification.dem


### parameter.gdal.orthorectification.dstCrs


### parameter.gdal.orthorectification.height


### parameter.gdal.orthorectification.input


### parameter.gdal.orthorectification.nodata


### parameter.gdal.orthorectification.output


### parameter.gdal.orthorectification.resampling


### parameter.gdal.orthorectification.targetResolution


### parameter.gdal.pansharpen.ms


### parameter.gdal.pansharpen.output


### parameter.gdal.pansharpen.pan


### parameter.gdal.polygonize.band


### parameter.gdal.polygonize.connected8


### parameter.gdal.polygonize.field


### parameter.gdal.polygonize.input


### parameter.gdal.polygonize.output


### parameter.gdal.reproject.dstCrs


### parameter.gdal.reproject.input


### parameter.gdal.reproject.nodata


### parameter.gdal.reproject.output


### parameter.gdal.reproject.reference


### parameter.gdal.reproject.resampling


### parameter.gdal.reproject.srcCrs


### parameter.gdal.reproject.targetResolution


### parameter.io.build_overviews.input


### parameter.io.build_overviews.levels


### parameter.io.build_overviews.resampling


### parameter.io.clip.bounds


### parameter.io.clip.creationOptions


### parameter.io.clip.input


### parameter.io.clip.output


### parameter.io.clip.srcCrsOverride


### parameter.io.convert_format.creationOptions


### parameter.io.convert_format.driver


### parameter.io.convert_format.input


### parameter.io.convert_format.output


### parameter.io.doctor.includeStatistics


### parameter.io.doctor.input


### parameter.io.inspect.input


### parameter.io.make_cog.creationOptions


### parameter.io.make_cog.input


### parameter.io.make_cog.output


### parameter.io.make_cog.preset


### parameter.io.reproject.input


### parameter.io.reproject.output


### parameter.io.reproject.resampling


### parameter.io.reproject.srcCrsOverride


### parameter.io.reproject.targetCrs


### parameter.io.translate.bands


### parameter.io.translate.creationOptions


### parameter.io.translate.driver


### parameter.io.translate.height


### parameter.io.translate.input


### parameter.io.translate.output


### parameter.io.translate.resampling


### parameter.io.translate.targetCrs


### parameter.io.translate.width


### parameter.io.vector_convert.clipBounds


### parameter.io.vector_convert.driver


### parameter.io.vector_convert.input


### parameter.io.vector_convert.layer


### parameter.io.vector_convert.output


### parameter.io.vector_convert.targetCrs


### parameter.io.vector_convert.where


### parameter.io.warp.bounds


### parameter.io.warp.creationOptions


### parameter.io.warp.input


### parameter.io.warp.output


### parameter.io.warp.resampling


### parameter.io.warp.resolution


### parameter.io.warp.targetAlignedPixels


### parameter.io.warp.targetCrs


### parameter.opencv.canny.apertureSize


### parameter.opencv.canny.band


### parameter.opencv.canny.input


### parameter.opencv.canny.output


### parameter.opencv.canny.threshold1


### parameter.opencv.canny.threshold2


### parameter.opencv.gaussian_blur.band


### parameter.opencv.gaussian_blur.input


### parameter.opencv.gaussian_blur.kernelSize


### parameter.opencv.gaussian_blur.output


### parameter.opencv.gaussian_blur.sigma


### parameter.opencv.laplacian.band


### parameter.opencv.laplacian.input


### parameter.opencv.laplacian.kernelSize


### parameter.opencv.laplacian.output


### parameter.opencv.mean_blur.band


### parameter.opencv.mean_blur.input


### parameter.opencv.mean_blur.kernelSize


### parameter.opencv.mean_blur.output


### parameter.opencv.median_blur.band


### parameter.opencv.median_blur.input


### parameter.opencv.median_blur.kernelSize


### parameter.opencv.median_blur.output


### parameter.opencv.sobel.band


### parameter.opencv.sobel.dx


### parameter.opencv.sobel.dy


### parameter.opencv.sobel.input


### parameter.opencv.sobel.kernelSize


### parameter.opencv.sobel.output


### parameter.otb.compute_images_statistics.input


### parameter.otb.compute_images_statistics.inputs


### parameter.otb.compute_images_statistics.output


### parameter.otb.compute_images_statistics.ram


### parameter.otb.meanshift_segmentation.ccExpression


### parameter.otb.meanshift_segmentation.filter


### parameter.otb.meanshift_segmentation.input


### parameter.otb.meanshift_segmentation.maxIterations


### parameter.otb.meanshift_segmentation.minRegionSize


### parameter.otb.meanshift_segmentation.output


### parameter.otb.meanshift_segmentation.outputMode


### parameter.otb.meanshift_segmentation.profileSize


### parameter.otb.meanshift_segmentation.radiusStep


### parameter.otb.meanshift_segmentation.rangeRadius


### parameter.otb.meanshift_segmentation.sigma


### parameter.otb.meanshift_segmentation.spatialRadius


### parameter.otb.meanshift_segmentation.startRadius


### parameter.otb.meanshift_segmentation.threshold


### parameter.otb.svm_classification.C


### parameter.otb.svm_classification.input


### parameter.otb.svm_classification.kernel


### parameter.otb.svm_classification.labelField


### parameter.otb.svm_classification.output


### parameter.otb.svm_classification.stats


### parameter.otb.svm_classification.vector


### parameter.rs.ace.input


### parameter.rs.ace.output


### parameter.rs.ace.target


### parameter.rs.apply_mask.align_mask


### parameter.rs.apply_mask.input


### parameter.rs.apply_mask.mask


### parameter.rs.apply_mask.no_data


### parameter.rs.apply_mask.output


### parameter.rs.atmospheric_correction.airmass


### parameter.rs.atmospheric_correction.band


### parameter.rs.atmospheric_correction.bias


### parameter.rs.atmospheric_correction.gain


### parameter.rs.atmospheric_correction.input


### parameter.rs.atmospheric_correction.metadata_path


### parameter.rs.atmospheric_correction.method


### parameter.rs.atmospheric_correction.output


### parameter.rs.atmospheric_dos1.band

- 含义：要校正的 1-based 波段序号。
- 单位：波段序号
- 推荐值：确认该波段的定标增益/偏置与元数据一致后再校正。

### parameter.rs.atmospheric_dos1.bias

- 含义：辐亮度偏置；缺省时从产品元数据解析。

### parameter.rs.atmospheric_dos1.gain

- 含义：辐亮度增益；缺省时从产品元数据解析。
- 单位：W·m⁻²·sr⁻¹·μm⁻¹ / DN

### parameter.rs.atmospheric_dos1.input


### parameter.rs.atmospheric_dos1.metadata_path


### parameter.rs.atmospheric_dos1.output


### parameter.rs.atmospheric_dos2.airmass


### parameter.rs.atmospheric_dos2.band


### parameter.rs.atmospheric_dos2.bias


### parameter.rs.atmospheric_dos2.gain


### parameter.rs.atmospheric_dos2.input


### parameter.rs.atmospheric_dos2.metadata_path


### parameter.rs.atmospheric_dos2.output


### parameter.rs.atmospheric_quac.input


### parameter.rs.atmospheric_quac.output


### parameter.rs.band_math.expression


### parameter.rs.band_math.input


### parameter.rs.band_math.output


### parameter.rs.band_ratio.blueBand


### parameter.rs.band_ratio.denominatorBand


### parameter.rs.band_ratio.greenBand


### parameter.rs.band_ratio.input


### parameter.rs.band_ratio.mode


### parameter.rs.band_ratio.numeratorBand


### parameter.rs.band_ratio.output


### parameter.rs.band_ratio.redBand


### parameter.rs.change_cva.after


### parameter.rs.change_cva.afterBand


### parameter.rs.change_cva.band


### parameter.rs.change_cva.before


### parameter.rs.change_cva.beforeBand


### parameter.rs.change_cva.output


### parameter.rs.change_cva_angle.after


### parameter.rs.change_cva_angle.afterBand1


### parameter.rs.change_cva_angle.afterBand2


### parameter.rs.change_cva_angle.before


### parameter.rs.change_cva_angle.beforeBand1


### parameter.rs.change_cva_angle.beforeBand2


### parameter.rs.change_cva_angle.mode


### parameter.rs.change_cva_angle.output


### parameter.rs.change_detection.after

- 含义：后时相影像路径，需与 before 同格网。

### parameter.rs.change_detection.afterBand


### parameter.rs.change_detection.band


### parameter.rs.change_detection.before

- 含义：前时相影像路径。

### parameter.rs.change_detection.beforeBand


### parameter.rs.change_detection.cleanup


### parameter.rs.change_detection.cleanupIterations


### parameter.rs.change_detection.makeMask

- 含义：是否输出二值变化掩膜。

### parameter.rs.change_detection.method

- 含义：变化度量：difference（差值，同量纲适用）、ratio（比值，抑制光照差异）、normalized_difference（归一化差值）、cva（多波段变化向量幅度）、mad、change_mask。
- 推荐值：反射率数据常用 difference；未一致性校正数据用 ratio；多波段综合用 cva。

### parameter.rs.change_detection.minAreaPixels

- 含义：形态清理时保留的最小图斑面积。
- 单位：像素
- 推荐值：按最小制图单元设定，过滤孤立像元噪声。

### parameter.rs.change_detection.output


### parameter.rs.change_detection.percentile

- 含义：取变化幅度前 N% 作为变化。
- 单位：百分比
- 推荐值：关注显著变化时 95–99。

### parameter.rs.change_detection.statisticalK

- 含义：统计阈值的 σ 倍数。
- 推荐值：k=2 保留较多变化，k=3 更严格。

### parameter.rs.change_detection.threshold

- 含义：固定阈值（fixed 模式）。

### parameter.rs.change_detection.thresholdMethod

- 含义：阈值策略：manual（手动阈值）、statistical（均值+k·σ）、percentile（百分位）、otsu（自动最大化类间方差）。
- 推荐值：无先验时用 otsu 或 statistical 起步，再按目视/样本微调。

### parameter.rs.change_difference.after


### parameter.rs.change_difference.afterBand


### parameter.rs.change_difference.band


### parameter.rs.change_difference.before


### parameter.rs.change_difference.beforeBand


### parameter.rs.change_difference.output


### parameter.rs.change_irmad.after


### parameter.rs.change_irmad.before


### parameter.rs.change_irmad.convThreshold


### parameter.rs.change_irmad.maxIterations


### parameter.rs.change_irmad.output


### parameter.rs.change_log_ratio.after


### parameter.rs.change_log_ratio.afterBand


### parameter.rs.change_log_ratio.band


### parameter.rs.change_log_ratio.before


### parameter.rs.change_log_ratio.beforeBand


### parameter.rs.change_log_ratio.epsilon


### parameter.rs.change_log_ratio.output


### parameter.rs.change_mad.after


### parameter.rs.change_mad.afterBand


### parameter.rs.change_mad.band


### parameter.rs.change_mad.before


### parameter.rs.change_mad.beforeBand


### parameter.rs.change_mad.output


### parameter.rs.change_normalized_difference.after


### parameter.rs.change_normalized_difference.afterBand


### parameter.rs.change_normalized_difference.band


### parameter.rs.change_normalized_difference.before


### parameter.rs.change_normalized_difference.beforeBand


### parameter.rs.change_normalized_difference.output


### parameter.rs.change_ratio.after


### parameter.rs.change_ratio.afterBand


### parameter.rs.change_ratio.band


### parameter.rs.change_ratio.before


### parameter.rs.change_ratio.beforeBand


### parameter.rs.change_ratio.output


### parameter.rs.change_sam.after


### parameter.rs.change_sam.before


### parameter.rs.change_sam.output


### parameter.rs.connected_components.band


### parameter.rs.connected_components.connectivity


### parameter.rs.connected_components.input


### parameter.rs.connected_components.output


### parameter.rs.continuum_removal.input


### parameter.rs.continuum_removal.output


### parameter.rs.contrast_stretch.clipPercent


### parameter.rs.contrast_stretch.input


### parameter.rs.contrast_stretch.method


### parameter.rs.contrast_stretch.output


### parameter.rs.contrast_stretch.piecewisePoints


### parameter.rs.contrast_stretch.stddevK


### parameter.rs.detect.bands


### parameter.rs.detect.batchCap


### parameter.rs.detect.conf


### parameter.rs.detect.device


### parameter.rs.detect.input


### parameter.rs.detect.model


### parameter.rs.detect.nms_iou


### parameter.rs.detect.output


### parameter.rs.detect.tta


### parameter.rs.dn_to_radiance.band


### parameter.rs.dn_to_radiance.bias


### parameter.rs.dn_to_radiance.gain


### parameter.rs.dn_to_radiance.input


### parameter.rs.dn_to_radiance.metadata_path


### parameter.rs.dn_to_radiance.output


### parameter.rs.embedding.aggregate


### parameter.rs.embedding.bands


### parameter.rs.embedding.batchCap


### parameter.rs.embedding.device


### parameter.rs.embedding.input


### parameter.rs.embedding.model


### parameter.rs.embedding.output


### parameter.rs.embedding.tta


### parameter.rs.endmember_extraction.input


### parameter.rs.endmember_extraction.nEndmembers


### parameter.rs.endmember_extraction.projections


### parameter.rs.evi.blue


### parameter.rs.evi.input


### parameter.rs.evi.nir


### parameter.rs.evi.output


### parameter.rs.evi.red


### parameter.rs.extract_bands.bands


### parameter.rs.extract_bands.input


### parameter.rs.extract_bands.output


### parameter.rs.feature_normalize.input


### parameter.rs.feature_normalize.inverse


### parameter.rs.feature_normalize.method


### parameter.rs.feature_normalize.output


### parameter.rs.feature_select.complement


### parameter.rs.feature_select.ids


### parameter.rs.feature_select.indices


### parameter.rs.feature_select.input


### parameter.rs.feature_select.output


### parameter.rs.feature_select.roles


### parameter.rs.feature_stack.feature_id


### parameter.rs.feature_stack.features


### parameter.rs.feature_stack.generator


### parameter.rs.feature_stack.output


### parameter.rs.feature_stack.reference


### parameter.rs.fill_holes.band


### parameter.rs.fill_holes.connectivity


### parameter.rs.fill_holes.input


### parameter.rs.fill_holes.output


### parameter.rs.focal_stats.band


### parameter.rs.focal_stats.input


### parameter.rs.focal_stats.output


### parameter.rs.focal_stats.stat


### parameter.rs.focal_stats.window


### parameter.rs.fusion_brovey.blueIdx


### parameter.rs.fusion_brovey.greenIdx


### parameter.rs.fusion_brovey.ms


### parameter.rs.fusion_brovey.output


### parameter.rs.fusion_brovey.pan


### parameter.rs.fusion_brovey.panWeight


### parameter.rs.fusion_brovey.redIdx


### parameter.rs.fusion_gram_schmidt.blueIdx


### parameter.rs.fusion_gram_schmidt.greenIdx


### parameter.rs.fusion_gram_schmidt.ms


### parameter.rs.fusion_gram_schmidt.output


### parameter.rs.fusion_gram_schmidt.pan


### parameter.rs.fusion_gram_schmidt.panWeight


### parameter.rs.fusion_gram_schmidt.redIdx


### parameter.rs.fusion_ihs.blueIdx


### parameter.rs.fusion_ihs.greenIdx


### parameter.rs.fusion_ihs.ms


### parameter.rs.fusion_ihs.output


### parameter.rs.fusion_ihs.pan


### parameter.rs.fusion_ihs.panWeight


### parameter.rs.fusion_ihs.redIdx


### parameter.rs.fusion_linear.blueIdx


### parameter.rs.fusion_linear.greenIdx


### parameter.rs.fusion_linear.ms


### parameter.rs.fusion_linear.output


### parameter.rs.fusion_linear.pan


### parameter.rs.fusion_linear.panWeight


### parameter.rs.fusion_linear.redIdx


### parameter.rs.fusion_pca.blueIdx


### parameter.rs.fusion_pca.greenIdx


### parameter.rs.fusion_pca.ms


### parameter.rs.fusion_pca.output


### parameter.rs.fusion_pca.pan


### parameter.rs.fusion_pca.panWeight


### parameter.rs.fusion_pca.redIdx


### parameter.rs.image_enhancement.band1


### parameter.rs.image_enhancement.band2


### parameter.rs.image_enhancement.band3


### parameter.rs.image_enhancement.clipPercent


### parameter.rs.image_enhancement.damping


### parameter.rs.image_enhancement.filterType


### parameter.rs.image_enhancement.input


### parameter.rs.image_enhancement.kernelSize


### parameter.rs.image_enhancement.method


### parameter.rs.image_enhancement.noiseVariance


### parameter.rs.image_enhancement.output


### parameter.rs.image_enhancement.sigma


### parameter.rs.image_enhancement.speckleType


### parameter.rs.image_enhancement.stddevK


### parameter.rs.image_enhancement.stretchType


### parameter.rs.image_enhancement.transform


### parameter.rs.image_fusion.blueIdx


### parameter.rs.image_fusion.greenIdx


### parameter.rs.image_fusion.method

- 含义：融合算法分支（Brovey/IHS/PCA/Gram-Schmidt/线性注入等）。
- 推荐值：制图展示 brovey/ihs；需要光谱一致性选 gram_schmidt。

### parameter.rs.image_fusion.ms


### parameter.rs.image_fusion.msWeights


### parameter.rs.image_fusion.output


### parameter.rs.image_fusion.pan


### parameter.rs.image_fusion.panWeight

- 含义：全色细节的注入强度权重（加权类融合算法）。
- 单位：权重
- 推荐值：从 1.0 起步；出现光晕/过锐时降低。
- 权衡：权重越高细节越强，光谱失真与振铃风险越大。

### parameter.rs.image_fusion.redIdx


### parameter.rs.infer.bands


### parameter.rs.infer.batchCap


### parameter.rs.infer.input


### parameter.rs.infer.model


### parameter.rs.infer.output


### parameter.rs.infer.tta


### parameter.rs.kmeans_classification.bands


### parameter.rs.kmeans_classification.input


### parameter.rs.kmeans_classification.k

- 含义：聚类中心数量 K。
- 单位：簇数
- 推荐值：目标类别数的 1.5–2 倍起步，聚类后再归并到语义类别。
- 权衡：K 越大对光谱细节刻画越细，但噪声簇与归并工作量增加。

### parameter.rs.kmeans_classification.maxSamples

- 含义：参与中心估计的采样像元上限。
- 单位：样本数
- 推荐值：大数据量时保持默认采样即可，统计意义足够。

### parameter.rs.kmeans_classification.output


### parameter.rs.kmeans_classification.scale

- 含义：聚类前特征缩放开关，消除波段量纲差异。
- 推荐值：波段动态范围差异大时开启。

### parameter.rs.landsat_import.bands


### parameter.rs.landsat_import.input


### parameter.rs.landsat_import.output


### parameter.rs.local_extrema.band


### parameter.rs.local_extrema.input


### parameter.rs.local_extrema.output


### parameter.rs.local_extrema.window


### parameter.rs.majority_filter.input


### parameter.rs.majority_filter.kernel


### parameter.rs.majority_filter.output


### parameter.rs.matched_filter.input


### parameter.rs.matched_filter.output


### parameter.rs.matched_filter.target


### parameter.rs.mndwi.green


### parameter.rs.mndwi.input


### parameter.rs.mndwi.output


### parameter.rs.mndwi.swir


### parameter.rs.mnf.input


### parameter.rs.mnf.numComponents


### parameter.rs.mnf.output


### parameter.rs.modis_georeference.dstCrs


### parameter.rs.modis_georeference.input


### parameter.rs.modis_georeference.output


### parameter.rs.modis_georeference.resampling


### parameter.rs.modis_georeference.tileH


### parameter.rs.modis_georeference.tileV


### parameter.rs.modis_import.bands


### parameter.rs.modis_import.input


### parameter.rs.modis_import.output


### parameter.rs.morphology.band


### parameter.rs.morphology.connectivity


### parameter.rs.morphology.input


### parameter.rs.morphology.iterations


### parameter.rs.morphology.op


### parameter.rs.morphology.output


### parameter.rs.mosaic.inputs

- 含义：参与镶嵌的栅格列表，输入次序决定重叠区的取值优先级。
- 推荐值：把希望优先保留的景放在列表前部；时相一致的影像先做辐射归一化再镶嵌以弱化接缝。

### parameter.rs.mosaic.output


### parameter.rs.ndbi.input


### parameter.rs.ndbi.nir


### parameter.rs.ndbi.output


### parameter.rs.ndbi.swir


### parameter.rs.ndvi.input


### parameter.rs.ndvi.nir


### parameter.rs.ndvi.output


### parameter.rs.ndvi.red


### parameter.rs.ndwi.green


### parameter.rs.ndwi.input


### parameter.rs.ndwi.nir


### parameter.rs.ndwi.output


### parameter.rs.obia_classify.bands


### parameter.rs.obia_classify.cellSize


### parameter.rs.obia_classify.classColors


### parameter.rs.obia_classify.classField


### parameter.rs.obia_classify.featureSelection


### parameter.rs.obia_classify.features


### parameter.rs.obia_classify.input


### parameter.rs.obia_classify.labels


### parameter.rs.obia_classify.method


### parameter.rs.obia_classify.minLabelPixels


### parameter.rs.obia_classify.minRegionSize


### parameter.rs.obia_classify.mlpHiddenLayerSize


### parameter.rs.obia_classify.mlpMaxIter


### parameter.rs.obia_classify.output


### parameter.rs.obia_classify.outputUncertainty


### parameter.rs.obia_classify.quantizeBins


### parameter.rs.obia_classify.rfMaxDepth


### parameter.rs.obia_classify.rfMinSampleCount


### parameter.rs.obia_classify.rfNumTrees


### parameter.rs.obia_classify.scale


### parameter.rs.obia_classify.segmentClasses


### parameter.rs.obia_classify.segmentMethod


### parameter.rs.obia_classify.smoothKernel


### parameter.rs.obia_classify.training


### parameter.rs.obia_features.bands


### parameter.rs.obia_features.input


### parameter.rs.obia_features.labels


### parameter.rs.obia_features.output


### parameter.rs.obia_hierarchy.classColors


### parameter.rs.obia_hierarchy.classField


### parameter.rs.obia_hierarchy.classifyLevel


### parameter.rs.obia_hierarchy.input


### parameter.rs.obia_hierarchy.labelsCoarse


### parameter.rs.obia_hierarchy.labelsFine


### parameter.rs.obia_hierarchy.maxIterations


### parameter.rs.obia_hierarchy.method


### parameter.rs.obia_hierarchy.minLabelPixels


### parameter.rs.obia_hierarchy.minRegionSize


### parameter.rs.obia_hierarchy.mlpHiddenLayerSize


### parameter.rs.obia_hierarchy.mlpMaxIter


### parameter.rs.obia_hierarchy.outputClass


### parameter.rs.obia_hierarchy.outputCoarse


### parameter.rs.obia_hierarchy.outputFine


### parameter.rs.obia_hierarchy.outputParents


### parameter.rs.obia_hierarchy.outputUncertainty


### parameter.rs.obia_hierarchy.parents


### parameter.rs.obia_hierarchy.rangeRadius


### parameter.rs.obia_hierarchy.rfMaxDepth


### parameter.rs.obia_hierarchy.rfMinSampleCount


### parameter.rs.obia_hierarchy.rfNumTrees


### parameter.rs.obia_hierarchy.segmentClasses


### parameter.rs.obia_hierarchy.spatialRadius


### parameter.rs.obia_hierarchy.threshold


### parameter.rs.obia_hierarchy.training


### parameter.rs.obia_hierarchy.watershedThreshold


### parameter.rs.obia_label.classField


### parameter.rs.obia_label.input


### parameter.rs.obia_label.labels


### parameter.rs.obia_label.minLabelPixels


### parameter.rs.obia_label.output


### parameter.rs.obia_label.training


### parameter.rs.obia_segment.bands


### parameter.rs.obia_segment.engine


### parameter.rs.obia_segment.input


### parameter.rs.obia_segment.maxIterations


### parameter.rs.obia_segment.minRegionSize

- 含义：分割后保留的最小对象像元数，小于该值的对象并入邻域。
- 单位：像素
- 推荐值：设为最小制图单元对应的像元数。

### parameter.rs.obia_segment.output


### parameter.rs.obia_segment.quantizeBins


### parameter.rs.obia_segment.rangeRadius

- 含义：mean-shift 光谱带宽，控制视为同质的光谱差异容限。
- 推荐值：按影像量化位数与目标对比度调整；过小导致过分割。

### parameter.rs.obia_segment.smoothKernel


### parameter.rs.obia_segment.spatialRadius

- 含义：mean-shift 空间带宽，控制邻域参与聚类的空间范围。
- 单位：像素
- 推荐值：与目标对象半径相当；高分辨率影像常用 5–15。
- 权衡：半径越大对象越大越平滑，小图斑被吞并。

### parameter.rs.obia_segment.threshold


### parameter.rs.pca.input


### parameter.rs.pca.numComponents


### parameter.rs.pca.output


### parameter.rs.post_classification_change.after


### parameter.rs.post_classification_change.afterBand


### parameter.rs.post_classification_change.band


### parameter.rs.post_classification_change.before


### parameter.rs.post_classification_change.beforeBand


### parameter.rs.post_classification_change.class_count


### parameter.rs.post_classification_change.class_labels


### parameter.rs.post_classification_change.output


### parameter.rs.proximity.band


### parameter.rs.proximity.connectivity


### parameter.rs.proximity.input


### parameter.rs.proximity.output


### parameter.rs.qa_mask.bits


### parameter.rs.qa_mask.input


### parameter.rs.qa_mask.mask


### parameter.rs.qa_mask.output


### parameter.rs.qa_mask.qa_band


### parameter.rs.qa_mask.source


### parameter.rs.radiometric_calibration.bands


### parameter.rs.radiometric_calibration.input


### parameter.rs.radiometric_calibration.metadata_path


### parameter.rs.radiometric_calibration.output


### parameter.rs.radiometric_calibration.unit


### parameter.rs.recode.input


### parameter.rs.recode.output


### parameter.rs.recode.recode_map


### parameter.rs.rx_anomaly.input


### parameter.rs.rx_anomaly.output


### parameter.rs.sam_classify.angleOut


### parameter.rs.sam_classify.bands


### parameter.rs.sam_classify.input


### parameter.rs.sam_classify.metric


### parameter.rs.sam_classify.output


### parameter.rs.sam_classify.refs


### parameter.rs.sar_backscatter.band


### parameter.rs.sar_backscatter.fromCalibration


### parameter.rs.sar_backscatter.incidenceDeg


### parameter.rs.sar_backscatter.incidenceRaster


### parameter.rs.sar_backscatter.input


### parameter.rs.sar_backscatter.inputDomain


### parameter.rs.sar_backscatter.output


### parameter.rs.sar_backscatter.outputDomain


### parameter.rs.sar_backscatter.polarizations


### parameter.rs.sar_backscatter.sensor


### parameter.rs.sar_backscatter.toCalibration


### parameter.rs.sar_calibrate.band


### parameter.rs.sar_calibrate.calibrationA


### parameter.rs.sar_calibrate.incidenceDeg


### parameter.rs.sar_calibrate.input


### parameter.rs.sar_calibrate.noiseLinear


### parameter.rs.sar_calibrate.output


### parameter.rs.sar_calibrate.outputDomain


### parameter.rs.sar_calibrate.polarizations


### parameter.rs.sar_calibrate.sensor


### parameter.rs.sar_change.bandA


### parameter.rs.sar_change.bandB


### parameter.rs.sar_change.cleanup


### parameter.rs.sar_change.cleanupIterations


### parameter.rs.sar_change.inputA


### parameter.rs.sar_change.inputB


### parameter.rs.sar_change.inputDomain


### parameter.rs.sar_change.minAreaPixels


### parameter.rs.sar_change.output


### parameter.rs.sar_change.percentile


### parameter.rs.sar_change.polarizations


### parameter.rs.sar_change.sensor


### parameter.rs.sar_change.statisticalK


### parameter.rs.sar_change.threshold


### parameter.rs.sar_change.thresholdMethod


### parameter.rs.sar_dualpol_features.domain


### parameter.rs.sar_dualpol_features.feature


### parameter.rs.sar_dualpol_features.input


### parameter.rs.sar_dualpol_features.output


### parameter.rs.sar_dualpol_features.vh_band


### parameter.rs.sar_dualpol_features.vv_band


### parameter.rs.sar_ratio.bandA


### parameter.rs.sar_ratio.bandB


### parameter.rs.sar_ratio.inputA


### parameter.rs.sar_ratio.inputB


### parameter.rs.sar_ratio.inputDomain


### parameter.rs.sar_ratio.output


### parameter.rs.sar_ratio.outputType


### parameter.rs.sar_ratio.polarizations


### parameter.rs.sar_ratio.sensor


### parameter.rs.sar_speckle.band

- 含义：要滤波的 1-based 波段序号，0 表示所有波段。
- 推荐值：单极化影像保持默认 1；多波段仅处理目标波段可省时。

### parameter.rs.sar_speckle.companionScenes

- 含义：多时相滤波的辅景路径列表，要求与主景同格网配准。
- 推荐值：3–10 景同期影像效果较稳。
- ⚠ 未配准的辅景会直接造成滤波错误。

### parameter.rs.sar_speckle.dampingFactor

- 含义：Frost 滤波的指数衰减系数，控制中心像元权重随距离衰减。
- 推荐值：默认 1.0；值越大平滑越弱、边缘保留越多。

### parameter.rs.sar_speckle.deviationK

- 含义：多时相门限：拒绝偏离参考景超过 k·局部标准差的配影像元。
- 推荐值：k=1 适合保留真实变化；增大 k 更宽容。

### parameter.rs.sar_speckle.input

- 含义：输入 SAR 强度影像。
- 推荐值：先运行 rs:sar_calibrate 得到 sigma0 再滤波。

### parameter.rs.sar_speckle.kernelSize

- 含义：定义估计斑点统计的局部邻域窗口。
- 单位：像素
- 推荐值：Sentinel-1 10 m 产品常用 5 或 7。
- 权衡：窗口越大抑斑越强，但小目标与边缘更模糊。
- 性能：耗时随窗口面积近似线性增长。

### parameter.rs.sar_speckle.looks

- 含义：等效视数 (ENL)，决定斑点统计强度假设。
- 单位：视数
- 推荐值：与数据的真实 ENL 一致；Sentinel-1 GRD IW 一般 4–5。

### parameter.rs.sar_speckle.method

- 含义：滤波核类型：lee（局部统计最小均方）、enhanced_lee（改进 Lee）、frost（指数加权）、kuan（改进 Kuan）、gamma_map（Gamma 先验最大后验）、refined_lee（边缘方向感知）、multitemporal（跨景栈滤波）。
- 推荐值：常规制图用 lee 或 refined_lee；保留纹理选 kuan；有同区多景选 multitemporal。
- 权衡：强抑斑滤波（gamma_map）更平滑但更易抹平细小地物；refined_lee 保边最好但计算最慢。

### parameter.rs.sar_speckle.noiseVariance

- 含义：Lee/Kuan/Gamma-MAP 的噪声方差模型参数，控制对斑点强度的假设。
- 推荐值：1 视强度数据约 1.0；已多视数据按 1/ENL 估计。默认 0.25 适合中等多视数据。
- ⚠ 设置过小会欠滤波，过大会过度平滑真实结构。

### parameter.rs.sar_speckle.output


### parameter.rs.sar_speckle.polarizations


### parameter.rs.sar_speckle.sensor


### parameter.rs.sar_terrain_correction.band


### parameter.rs.sar_terrain_correction.dem


### parameter.rs.sar_terrain_correction.demUnit


### parameter.rs.sar_terrain_correction.flagIncidence


### parameter.rs.sar_terrain_correction.flagMask


### parameter.rs.sar_terrain_correction.headingDeg


### parameter.rs.sar_terrain_correction.incidenceDeg


### parameter.rs.sar_terrain_correction.input


### parameter.rs.sar_terrain_correction.output


### parameter.rs.sar_terrain_correction.polarizations


### parameter.rs.sar_terrain_correction.sensor


### parameter.rs.sar_terrain_flatten.band


### parameter.rs.sar_terrain_flatten.dem


### parameter.rs.sar_terrain_flatten.demUnit


### parameter.rs.sar_terrain_flatten.headingDeg


### parameter.rs.sar_terrain_flatten.incidenceDeg


### parameter.rs.sar_terrain_flatten.input


### parameter.rs.sar_terrain_flatten.output


### parameter.rs.sar_terrain_flatten.polarizations


### parameter.rs.sar_terrain_flatten.sensor


### parameter.rs.sar_terrain_masks.dem


### parameter.rs.sar_terrain_masks.heading


### parameter.rs.sar_terrain_masks.incidence


### parameter.rs.sar_terrain_masks.output


### parameter.rs.sar_terrain_masks.product


### parameter.rs.sar_texture.band


### parameter.rs.sar_texture.directionDeg


### parameter.rs.sar_texture.displacement


### parameter.rs.sar_texture.input


### parameter.rs.sar_texture.measures


### parameter.rs.sar_texture.output


### parameter.rs.sar_texture.polarizations


### parameter.rs.sar_texture.quantLevels


### parameter.rs.sar_texture.sensor


### parameter.rs.sar_texture.windowSize


### parameter.rs.savi.input


### parameter.rs.savi.nir


### parameter.rs.savi.output


### parameter.rs.savi.red


### parameter.rs.segment.bands


### parameter.rs.segment.batchCap


### parameter.rs.segment.device


### parameter.rs.segment.format


### parameter.rs.segment.input


### parameter.rs.segment.model


### parameter.rs.segment.output


### parameter.rs.segment.tta


### parameter.rs.segment_stats.bands


### parameter.rs.segment_stats.input


### parameter.rs.segment_stats.labels


### parameter.rs.segment_stats.output


### parameter.rs.sentinel2_import.bands


### parameter.rs.sentinel2_import.input


### parameter.rs.sentinel2_import.output


### parameter.rs.sentinel2_import.resolution


### parameter.rs.sieve.band


### parameter.rs.sieve.connectivity


### parameter.rs.sieve.input


### parameter.rs.sieve.min_area_pixels


### parameter.rs.sieve.output


### parameter.rs.spectral_derivative.input


### parameter.rs.spectral_derivative.order


### parameter.rs.spectral_derivative.output


### parameter.rs.spectral_derivative.wavelengths


### parameter.rs.spectral_index.blue

- 含义：蓝光波段（EVI 需要，如 Sentinel-2 B2）。

### parameter.rs.spectral_index.green


### parameter.rs.spectral_index.index

- 含义：指数类型。常用：NDVI（植被）、EVI（增强型植被、抗饱和）、NDWI（水体, McFeeters）、MNDWI（改进水体, 加 SWIR）、SAVI（低植被区土壤调整）、NDBI（建筑指数）。
- 推荐值：植被监测 NDVI；水体用 MNDWI（比 NDWI 更稳）；建成区用 NDBI 并配合其他特征。

### parameter.rs.spectral_index.input


### parameter.rs.spectral_index.nir

- 含义：近红外波段（如 Sentinel-2 B8）。

### parameter.rs.spectral_index.output


### parameter.rs.spectral_index.postfire


### parameter.rs.spectral_index.red

- 含义：红光波段索引或名称（如 Sentinel-2 B4）。
- 推荐值：务必与影像实际波段顺序核对。

### parameter.rs.spectral_index.rededge


### parameter.rs.spectral_index.swir

- 含义：短波红外波段（MNDWI/NDBI 需要，如 Sentinel-2 B11）。

### parameter.rs.spectral_index.swir2


### parameter.rs.spectral_resample.input


### parameter.rs.spectral_resample.output


### parameter.rs.spectral_resample.sourceWavelengths


### parameter.rs.spectral_resample.wavelengths


### parameter.rs.spectral_unmixing.bands


### parameter.rs.spectral_unmixing.endmembers


### parameter.rs.spectral_unmixing.errorOut


### parameter.rs.spectral_unmixing.input


### parameter.rs.spectral_unmixing.output


### parameter.rs.supervised_classification.bands

- 含义：参与分类的波段/特征列表。
- 推荐值：加入 NDVI/纹理/SAR 特征常显著提升精度。

### parameter.rs.supervised_classification.classField

- 含义：类别标签字段名。
- ⚠ 字段不存在或类型非整数/文本时报错。

### parameter.rs.supervised_classification.input

- 含义：待分类影像或特征堆栈。

### parameter.rs.supervised_classification.maxSamplesPerClass

- 含义：每类训练样本上限（类别平衡）。
- 单位：样本数
- 推荐值：样本极不均衡时设置，防止大类主导。

### parameter.rs.supervised_classification.method

- 含义：分类器：rf（随机森林，稳健默认）、svm（小样本高维）、等。
- 推荐值：不确定时选 rf 并留默认参数。

### parameter.rs.supervised_classification.modelIn


### parameter.rs.supervised_classification.modelOut


### parameter.rs.supervised_classification.output


### parameter.rs.supervised_classification.probabilityOutput

- 含义：输出各类别概率图。

### parameter.rs.supervised_classification.scale

- 含义：特征缩放开关（SVM 建议）。

### parameter.rs.supervised_classification.seed

- 含义：随机种子（样本划分/森林初始化）。
- 推荐值：记录种子保证可复现。

### parameter.rs.supervised_classification.testSplit

- 含义：从样本中划出的验证比例。
- 单位：比例
- 推荐值：0.2–0.3；注意随机划分存在空间泄漏风险，严格评估用空间分块样本。

### parameter.rs.supervised_classification.training

- 含义：训练样本矢量图层（含类别字段）。
- 推荐值：每类样本量 ≥ 特征维数的 10 倍；空间分布分散。

### parameter.rs.temporal_anomaly.apply_qa_masking


### parameter.rs.temporal_anomaly.band


### parameter.rs.temporal_anomaly.band_role


### parameter.rs.temporal_anomaly.baseline_end


### parameter.rs.temporal_anomaly.baseline_start


### parameter.rs.temporal_anomaly.collection


### parameter.rs.temporal_anomaly.duplicate_policy


### parameter.rs.temporal_anomaly.method


### parameter.rs.temporal_anomaly.min_observations


### parameter.rs.temporal_anomaly.output


### parameter.rs.temporal_anomaly.scenes


### parameter.rs.temporal_anomaly.target_time


### parameter.rs.temporal_anomaly.tile_size


### parameter.rs.temporal_breakpoints.apply_qa_masking


### parameter.rs.temporal_breakpoints.band


### parameter.rs.temporal_breakpoints.band_role


### parameter.rs.temporal_breakpoints.collection


### parameter.rs.temporal_breakpoints.duplicate_policy


### parameter.rs.temporal_breakpoints.maxBreaks


### parameter.rs.temporal_breakpoints.minImprovement


### parameter.rs.temporal_breakpoints.minSegmentDays


### parameter.rs.temporal_breakpoints.output


### parameter.rs.temporal_breakpoints.outputBreakDates


### parameter.rs.temporal_breakpoints.scenes


### parameter.rs.temporal_breakpoints.tile_size


### parameter.rs.temporal_composite.apply_qa_masking

- 含义：合成前按 QA/质量波段剔除低质量像元。

### parameter.rs.temporal_composite.band


### parameter.rs.temporal_composite.band_role


### parameter.rs.temporal_composite.collection

- 含义：时间集合 ID（治理目录引用），与 scenes 二选一。

### parameter.rs.temporal_composite.duplicate_policy

- 含义：同一时刻重复景的处理策略。

### parameter.rs.temporal_composite.method

- 含义：合成方式：best_pixel（按质量分选最优观测，默认）、mean、median（抗云抗离群）。
- 推荐值：有质量波段时用 best_pixel；云污染重的光学数据用 median。

### parameter.rs.temporal_composite.output


### parameter.rs.temporal_composite.period

- 含义：聚合周期命名（month/season/year）或自定义天数。
- 推荐值：云雨区月度合成 + 中值最常用。

### parameter.rs.temporal_composite.period_days


### parameter.rs.temporal_composite.quality_band

- 含义：质量分 1-based 波段号（值越大越好），供 best_pixel 选取。
- ⚠ 是波段序号而非 QA 波段名；QA 语义掩膜请先用 rs:qa_mask / rs:apply_mask。

### parameter.rs.temporal_composite.scenes

- 含义：显式列出影像路径（逗号分隔）。

### parameter.rs.temporal_composite.target_date

- 含义：best_pixel 的时间接近度 tie-break：多景质量相同时选最接近该日期的观测。

### parameter.rs.temporal_composite.tile_size

- 含义：流式分块大小，控制内存。
- 单位：像素
- 推荐值：保持默认 256/512 即可。

### parameter.rs.temporal_decompose.apply_qa_masking


### parameter.rs.temporal_decompose.band


### parameter.rs.temporal_decompose.band_role


### parameter.rs.temporal_decompose.collection


### parameter.rs.temporal_decompose.components


### parameter.rs.temporal_decompose.duplicate_policy


### parameter.rs.temporal_decompose.output


### parameter.rs.temporal_decompose.scenes


### parameter.rs.temporal_decompose.seasonal_window_days


### parameter.rs.temporal_decompose.tile_size


### parameter.rs.temporal_decompose.trend_lambda


### parameter.rs.temporal_extract_series.apply_qa_masking


### parameter.rs.temporal_extract_series.band


### parameter.rs.temporal_extract_series.band_role


### parameter.rs.temporal_extract_series.collection


### parameter.rs.temporal_extract_series.duplicate_policy


### parameter.rs.temporal_extract_series.output


### parameter.rs.temporal_extract_series.point


### parameter.rs.temporal_extract_series.polygon


### parameter.rs.temporal_extract_series.scenes


### parameter.rs.temporal_gap_fill.apply_qa_masking


### parameter.rs.temporal_gap_fill.band


### parameter.rs.temporal_gap_fill.band_role


### parameter.rs.temporal_gap_fill.collection


### parameter.rs.temporal_gap_fill.duplicate_policy


### parameter.rs.temporal_gap_fill.max_gap_days


### parameter.rs.temporal_gap_fill.method


### parameter.rs.temporal_gap_fill.output


### parameter.rs.temporal_gap_fill.scenes


### parameter.rs.temporal_gap_fill.tile_size


### parameter.rs.temporal_harmonic_fit.apply_qa_masking


### parameter.rs.temporal_harmonic_fit.band


### parameter.rs.temporal_harmonic_fit.band_role


### parameter.rs.temporal_harmonic_fit.collection


### parameter.rs.temporal_harmonic_fit.duplicate_policy


### parameter.rs.temporal_harmonic_fit.harmonics


### parameter.rs.temporal_harmonic_fit.minObservations


### parameter.rs.temporal_harmonic_fit.output


### parameter.rs.temporal_harmonic_fit.robust


### parameter.rs.temporal_harmonic_fit.scenes


### parameter.rs.temporal_harmonic_fit.tile_size


### parameter.rs.temporal_harmonic_fit.writeCoefficients


### parameter.rs.temporal_index_series.apply_qa_masking


### parameter.rs.temporal_index_series.bands


### parameter.rs.temporal_index_series.collection


### parameter.rs.temporal_index_series.duplicate_policy


### parameter.rs.temporal_index_series.index


### parameter.rs.temporal_index_series.output


### parameter.rs.temporal_index_series.scenes


### parameter.rs.temporal_index_series.tile_size


### parameter.rs.temporal_monitor.apply_qa_masking


### parameter.rs.temporal_monitor.band


### parameter.rs.temporal_monitor.band_role


### parameter.rs.temporal_monitor.collection


### parameter.rs.temporal_monitor.drift


### parameter.rs.temporal_monitor.lambda


### parameter.rs.temporal_monitor.max_pairwork


### parameter.rs.temporal_monitor.method


### parameter.rs.temporal_monitor.min_observations


### parameter.rs.temporal_monitor.output


### parameter.rs.temporal_monitor.tile_size


### parameter.rs.temporal_phenology.apply_qa_masking


### parameter.rs.temporal_phenology.band


### parameter.rs.temporal_phenology.band_role


### parameter.rs.temporal_phenology.collection


### parameter.rs.temporal_phenology.crossingFraction


### parameter.rs.temporal_phenology.duplicate_policy


### parameter.rs.temporal_phenology.minValidPerSeason


### parameter.rs.temporal_phenology.output


### parameter.rs.temporal_phenology.scenes


### parameter.rs.temporal_phenology.seasonEndDoy


### parameter.rs.temporal_phenology.seasonStartDoy


### parameter.rs.temporal_phenology.tile_size


### parameter.rs.temporal_sen_trend.alpha


### parameter.rs.temporal_sen_trend.apply_qa_masking


### parameter.rs.temporal_sen_trend.band


### parameter.rs.temporal_sen_trend.band_role


### parameter.rs.temporal_sen_trend.collection


### parameter.rs.temporal_sen_trend.duplicate_policy


### parameter.rs.temporal_sen_trend.output


### parameter.rs.temporal_sen_trend.scenes


### parameter.rs.temporal_sen_trend.tile_size


### parameter.rs.temporal_smooth.apply_qa_masking


### parameter.rs.temporal_smooth.band


### parameter.rs.temporal_smooth.band_role


### parameter.rs.temporal_smooth.collection


### parameter.rs.temporal_smooth.degree


### parameter.rs.temporal_smooth.duplicate_policy


### parameter.rs.temporal_smooth.lambda


### parameter.rs.temporal_smooth.method


### parameter.rs.temporal_smooth.moving_average_window


### parameter.rs.temporal_smooth.output


### parameter.rs.temporal_smooth.scenes


### parameter.rs.temporal_smooth.tile_size


### parameter.rs.temporal_smooth.window


### parameter.rs.temporal_summary.apply_qa_masking


### parameter.rs.temporal_summary.band


### parameter.rs.temporal_summary.band_role


### parameter.rs.temporal_summary.collection


### parameter.rs.temporal_summary.duplicate_policy


### parameter.rs.temporal_summary.include_median


### parameter.rs.temporal_summary.output


### parameter.rs.temporal_summary.scenes


### parameter.rs.temporal_summary.tile_size


### parameter.rs.temporal_trend.apply_qa_masking


### parameter.rs.temporal_trend.band


### parameter.rs.temporal_trend.band_role


### parameter.rs.temporal_trend.collection


### parameter.rs.temporal_trend.duplicate_policy


### parameter.rs.temporal_trend.output


### parameter.rs.temporal_trend.scenes


### parameter.rs.temporal_trend.tile_size


### parameter.rs.terrain_analysis.cellSize


### parameter.rs.terrain_analysis.input


### parameter.rs.terrain_analysis.nodata


### parameter.rs.terrain_analysis.output


### parameter.rs.terrain_analysis.product

- 含义：要生成的地形产品（slope/aspect/hillshade 等）。
- 推荐值：制图晕渲选 hillshade；水文准备先出 slope。

### parameter.rs.terrain_analysis.sunAzimuth

- 含义：山影计算的光源方位角（0–360，北 0 顺时针）。
- 单位：度
- 推荐值：西北 315° 为常用制图惯例。

### parameter.rs.terrain_analysis.sunElevation

- 含义：山影计算的光源高度角（0–90）。
- 单位：度
- 推荐值：45° 起步，按地形起伏调整阴影对比。

### parameter.rs.terrain_analysis.zFactor

- 含义：垂直夸张系数，水平垂直单位不一致时必设。
- 推荐值：经纬度 DEM 与米高程混用时按纬度换算设置。

### parameter.rs.terrain_flow.input


### parameter.rs.terrain_flow.nodata


### parameter.rs.terrain_flow.output


### parameter.rs.terrain_flow.product


### parameter.rs.threshold_raster.cleanup


### parameter.rs.threshold_raster.cleanupIterations


### parameter.rs.threshold_raster.input


### parameter.rs.threshold_raster.minAreaPixels


### parameter.rs.threshold_raster.output


### parameter.rs.threshold_raster.percentile


### parameter.rs.threshold_raster.statisticalK


### parameter.rs.threshold_raster.threshold


### parameter.rs.threshold_raster.thresholdMethod


### parameter.rs.topographic_correction.dem

- 含义：与影像配准的 DEM。

### parameter.rs.topographic_correction.input

- 含义：待校正影像（建议地表反射率）。

### parameter.rs.topographic_correction.method

- 含义：correction 模型：cosine（简单余弦）、c（经验 C）、scs+c（太阳-冠层-传感器）、minnaert（非朗伯参数）。
- 推荐值：植被区常用 scs+c；minnaert 参数按波段回归。

### parameter.rs.topographic_correction.output


### parameter.rs.topographic_correction.solar_azimuth

- 含义：太阳方位角（0–360，北 0 顺时针）。
- 单位：度
- 推荐值：从影像元数据读取，勿凭经验填写。

### parameter.rs.topographic_correction.solar_zenith

- 含义：太阳天顶角（0–90）。
- 单位：度
- ⚠ 天顶角/高度角混用是常见错误，确认元数据定义。

