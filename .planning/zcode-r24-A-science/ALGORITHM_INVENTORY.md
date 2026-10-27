# R24-A Algorithm Inventory (WP-A0)

- 基线：origin/master@5fe642ec6339807823aa56612ba94668592a738d
- rs: 算子注册总数：157（src/operators/rs/rs_operators_init.cpp，注册即唯一权威清单）
- 内核实现：src/processing/algorithms/（127 个 .cpp，含 sar/ temporal/ detail/ primitives/ registration/ 子目录）
- 分析库：src/analysis/{atmospheric,classification,georeferencing,hyperspectral,segmentation}
- GUI/CLI/TaskCenter/MCP 同一算子路径：rs: 算子经 RsOperatorRegistry；atomic_algorithm_registry 暴露给 Processing 工具箱与 TaskCenter；GDAL/OTB/Native 提供者在 src/processing/providers/*。

## 家族 → 注册 id 清单

### spectral_family (7)
- ids: rs:spectral_index, rs:spectral_unmixing, rs:spectral_similarity, rs:spectral_resample, rs:spectral_derivative, rs:spectral_spatial_fuse, rs:spectral_band_select

### core_index_mask (71)
- ids: rs:ndvi, rs:evi, rs:ndwi, rs:savi, rs:ndbi, rs:mndwi, rs:band_math, rs:band_ratio, rs:extract_bands, rs:contrast_stretch, rs:image_enhancement, rs:sam_classify, rs:rx_anomaly, rs:local_rx_anomaly, rs:sparse_unmixing, rs:endmember_analysis, rs:continuum_removal, rs:endmember_extraction, rs:dn_to_radiance, rs:radiometric_calibration, rs:threshold_raster, rs:post_classification_change, rs:qa_mask, rs:apply_mask, rs:image_fusion, rs:topographic_correction, rs:solar_geometry, rs:brdf_normalization, rs:radiometric_qa, rs:matched_filter, rs:ace, rs:cem_detection, rs:tcimf_detection, rs:osp_detection, rs:rasterize, rs:zonal_stats, rs:resample, rs:align, rs:morphology, rs:connected_components, rs:fill_holes, rs:sieve, rs:proximity, rs:local_extrema, rs:focal_stats, rs:pca, rs:mnf, rs:mnf_inverse, rs:library_select, rs:mosaic, rs:quality_mosaic, rs:hj_import, rs:register_images, rs:stack_register, rs:kmeans_classification, rs:supervised_classification, rs:obia_segment, rs:obia_classify, rs:obia_hierarchy, rs:obia_features, rs:obia_label, rs:segment_stats, rs:majority_filter, rs:recode, rs:infer, rs:segment, rs:detect, rs:embedding, rs:classify, rs:change, rs:regress

### atmospheric_family (4)
- ids: rs:atmospheric_correction, rs:atmospheric_dos1, rs:atmospheric_dos2, rs:atmospheric_quac

### change_family (10)
- ids: rs:change_detection, rs:change_difference, rs:change_normalized_difference, rs:change_ratio, rs:change_cva, rs:change_cva_angle, rs:change_sam, rs:change_log_ratio, rs:change_mad, rs:change_irmad

### sar_family (23)
- ids: rs:sar_calibrate, rs:sar_backscatter, rs:sar_terrain_flatten, rs:sar_terrain_correction, rs:sar_speckle, rs:sar_ratio, rs:sar_texture, rs:sar_change, rs:sar_dualpol_features, rs:sar_terrain_masks, rs:sar_geocode, rs:sar_temporal_stats, rs:sar_polsar_decompose, rs:sar_interferogram, rs:sar_phase_filter, rs:sar_unwrap, rs:sar_displacement, rs:sar_coregister, rs:sar_temporal_events, rs:sar_remove_topographic_phase, rs:sar_coregister_local, rs:sar_pair_network, rs:sar_network_inversion

### fusion_family (5)
- ids: rs:fusion_linear, rs:fusion_brovey, rs:fusion_pca, rs:fusion_ihs, rs:fusion_gram_schmidt

### feature_family (3)
- ids: rs:feature_stack, rs:feature_normalize, rs:feature_select

### terrain_family (5)
- ids: rs:terrain_analysis, rs:terrain_flow, rs:terrain_viewshed, rs:terrain_solar, rs:terrain_landform

### temporal_family (22)
- ids: rs:temporal_monitor, rs:temporal_summary, rs:temporal_composite, rs:temporal_index_series, rs:temporal_trend, rs:temporal_smooth, rs:temporal_gap_fill, rs:temporal_harmonic_fit, rs:temporal_phenology, rs:temporal_breakpoints, rs:temporal_sen_trend, rs:temporal_decompose, rs:temporal_anomaly, rs:temporal_extract_series, rs:temporal_sar_fusion, rs:temporal_extract_regions, rs:temporal_regularize, rs:temporal_harmonic_breaks, rs:temporal_seasonal_breaks, rs:temporal_model_select, rs:temporal_phenology_multi, rs:temporal_region_features

### landsat_family (1)
- ids: rs:landsat_import

### sentinel2_family (1)
- ids: rs:sentinel2_import

### modis_family (2)
- ids: rs:modis_import, rs:modis_georeference

### cn_family (1)
- ids: rs:cn_product_import

### gaofen_family (1)
- ids: rs:gaofen_import

### zy3_family (1)
- ids: rs:zy3_import

## 测试覆盖粗粒度映射（关键词 → 提及它的测试文件数）

- change: 388
- sar: 288
- classif: 260
- ndvi: 223
- spectral: 216
- nodata: 199
- temporal: 144
- cem: 133
- radiometric: 109
- pca: 87
- terrain: 55
- fusion: 50
- phenology: 37
- mosaic: 37
- band_math: 29
- atmospheric: 27
- trend: 25
- texture: 23
- svm: 20
- confusion: 19
- kmeans: 18
- insar: 17
- endmember: 15
- glcm: 14
- unmix: 13
- breakpoint: 10
- sieve: 10
- rx_anomaly: 9
- random_forest: 8
- brdf: 7
- hydrology: 7
- viewshed: 6
- isodata: 6
- proximity: 4
- resampler: 4
- mnf: 4
- spatiotemporal: 3

## 本次侦察实读确认的缺口

- #1687 OPEN_LIVE：math_utils.cpp:184 魔数 + 等值负对 NaN 化（详见 ISSUE_TRIAGE.md）
- #1646-5 键名已修但无 drift 测试：algorithm_help_catalog.cpp（93 键）vs 注册算法名
- 已修家族防重做锚点：r22-science(#1712)/r21-A(#1676)/r20-01(#1671) 修复清单见 BASELINE.md