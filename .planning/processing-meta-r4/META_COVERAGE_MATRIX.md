# META_COVERAGE_MATRIX — 157 注册 × 双口径 × 字段完备（WP-A）

生成：.planning/processing-meta-r4/gen_matrix.py（只读源树，可重跑复核）

## 汇总

- 注册行数：**157**（== REGISTER_RS_OPERATOR 计数 157）
- 唯一类：157；声明头文件：119；实现 .cpp：119
- sparse sidecar（== 代码中声明 taskFamily，设计不变量见 BASELINE P1）：**56/157**，缺口 101（注册口径）
- 文件口径缺口：实现 taskFamily 算子涉及的、无 sparse 覆盖的 .cpp 文件数 = 79（逐行归位见矩阵）
- capability 侧车覆盖：**157/157**（缺文件 0 个）
- authored 字段缺口：applicability 0 / teaching_use 0 / prerequisites 0 / limitations 0（summary/failure_modes 应为 0）
- determinism 声明缺失：0；memory_policy 缺失：0
- gpu 声明（true/false 显式）：51/157 显式；accuracy 实测值存在：0/157

## 矩阵（157 行，列：注册名 | 类 | 头文件 | 实现.cpp | sparse | task | family | det | mem | gpu | acc | in→out | applic | teach | prereq | limit）

| 注册名 | 类 | .h | .cpp | sparse | task | family | det | mem | gpu | acc | in→out | applic | teach | prereq | limit |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| rs:spectral_index | RsSpectralIndexOperator | rs_spectral_index_operator.h | rs_spectral_index_operator.cpp | Y | index-computation | spectral | bit_exact | streaming | nogpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:ndvi | RsNdviOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:evi | RsEviOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:ndwi | RsNdwiOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:savi | RsSaviOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:ndbi | RsNdbiOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:mndwi | RsMndwiOperator | rs_spectral_index_aliases.h | rs_spectral_index_aliases.cpp | × | ∅ | spectral | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:band_math | RsBandMathOperator | rs_band_math_operator.h | rs_band_math_operator.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:band_ratio | RsBandRatioOperator | rs_band_tools_operators.h | rs_band_tools_operators.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:extract_bands | RsExtractBandsOperator | rs_band_tools_operators.h | rs_band_tools_operators.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:contrast_stretch | RsContrastStretchOperator | rs_band_tools_operators.h | rs_band_tools_operators.cpp | × | ∅ | optical | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:image_enhancement | RsImageEnhancementOperator | rs_image_enhancement_operator.h | rs_image_enhancement_operator.cpp | × | ∅ | optical | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:sam_classify | RsSamClassifyOperator | rs_sam_classify_operator.h | rs_sam_classify_operator.cpp | × | ∅ | classification | bit_exact | streaming | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:spectral_unmixing | RsSpectralUnmixingOperator | rs_spectral_unmixing_operator.h | rs_spectral_unmixing_operator.cpp | × | ∅ | hyperspectral | bit_exact | streaming | - | - | raster→integer,numeric,raster | Y | Y | Y | Y |
| rs:rx_anomaly | RsRxAnomalyOperator | rs_rx_anomaly_operator.h | rs_rx_anomaly_operator.cpp | × | ∅ | hyperspectral | bit_exact | multipass_streaming | - | - | raster→numeric,raster | Y | Y | Y | Y |
| rs:local_rx_anomaly | RsLocalRxOperator | rs_local_rx_operator.h | rs_local_rx_operator.cpp | Y | anomaly-detection | spectral | bit_exact | streaming | - | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:sparse_unmixing | RsSparseUnmixingOperator | rs_sparse_unmixing_operator.h | rs_sparse_unmixing_operator.cpp | Y | unmixing | spectral | bit_exact | streaming | - | - | raster→integer,numeric,raster | Y | Y | Y | Y |
| rs:spectral_similarity | RsSpectralSimilarityOperator | rs_spectral_similarity_operator.h | rs_spectral_similarity_operator.cpp | Y | classification | spectral | bit_exact | streaming | - | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:endmember_analysis | RsEndmemberAnalysisOperator | rs_endmember_analysis_operator.h | rs_endmember_analysis_operator.cpp | Y | endmember-analysis | spectral | bit_exact | full_raster | - | - | ∅→integer,json | Y | Y | Y | Y |
| rs:continuum_removal | RsContinuumRemovalOperator | rs_continuum_removal_operator.h | rs_continuum_removal_operator.cpp | × | ∅ | hyperspectral | bit_exact | streaming | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:spectral_resample | RsSpectralResampleOperator | rs_spectral_resample_operator.h | rs_spectral_resample_operator.cpp | × | ∅ | hyperspectral | bit_exact | streaming | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:endmember_extraction | RsEndmemberExtractionOperator | rs_endmember_extraction_operator.h | rs_endmember_extraction_operator.cpp | × | ∅ | hyperspectral | bit_exact | multipass_streaming | - | - | raster→string | Y | Y | Y | Y |
| rs:atmospheric_correction | RsAtmosphericCorrectionOperator | rs_atmospheric_correction_operator.h | rs_atmospheric_correction_operator.cpp | × | ∅ | optical | bit_exact | multipass_streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:dn_to_radiance | RsDnToRadianceOperator | rs_atmospheric_aliases.h | rs_atmospheric_aliases.cpp | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:atmospheric_dos1 | RsAtmosphericDos1Operator | rs_atmospheric_aliases.h | rs_atmospheric_aliases.cpp | × | ∅ | optical | bit_exact | multipass_streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:atmospheric_dos2 | RsAtmosphericDos2Operator | rs_atmospheric_aliases.h | rs_atmospheric_aliases.cpp | × | ∅ | optical | bit_exact | multipass_streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:atmospheric_quac | RsAtmosphericQuacOperator | rs_atmospheric_aliases.h | rs_atmospheric_aliases.cpp | × | ∅ | optical | bit_exact | full_raster | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:radiometric_calibration | RsRadiometricCalibrationOperator | rs_radiometric_calibration_operator.h | rs_radiometric_calibration_operator.cpp | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:change_detection | RsChangeDetectionOperator | rs_change_detection_operator.h | rs_change_detection_operator.cpp | Y | change-detection | change | bit_exact | multipass_streaming | nogpu | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:change_difference | RsChangeDifferenceOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_normalized_difference | RsChangeNormalizedDifferenceOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_ratio | RsChangeRatioOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_cva | RsChangeCvaOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | multipass_streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_cva_angle | RsChangeCvaAngleOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:change_sam | RsChangeSamOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_log_ratio | RsChangeLogRatioOperator | rs_change_primitives.h | rs_change_primitives.cpp | Y | change-detection | change | bit_exact | streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_mad | RsChangeMadOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | multipass_streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:change_irmad | RsChangeIrMadOperator | rs_change_primitives.h | rs_change_primitives.cpp | × | ∅ | change | bit_exact | multipass_streaming | - | - | raster→numeric,raster,string | Y | Y | Y | Y |
| rs:threshold_raster | RsThresholdRasterOperator | rs_threshold_raster_operator.h | rs_threshold_raster_operator.cpp | × | ∅ | raster_spatial | bit_exact | streaming | - | - | raster→integer,numeric,raster | Y | Y | Y | Y |
| rs:sar_calibrate | RsSarCalibrateOperator | rs_sar_calibrate_operator.h | rs_sar_calibrate_operator.cpp | × | ∅ | sar | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_backscatter | RsSarBackscatterOperator | rs_sar_backscatter_operator.h | rs_sar_backscatter_operator.cpp | × | ∅ | sar | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_terrain_flatten | RsSarTerrainFlattenOperator | rs_sar_terrain_flatten_operator.h | rs_sar_terrain_flatten_operator.cpp | × | ∅ | sar | bit_exact | streaming | - | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:sar_terrain_correction | RsSarTerrainCorrectionOperator | rs_sar_terrain_correction_operator.h | rs_sar_terrain_correction_operator.cpp | × | ∅ | sar | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_speckle | RsSarSpeckleOperator | rs_sar_speckle_operator.h | rs_sar_speckle_operator.cpp | × | ∅ | sar | tolerance | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_ratio | RsSarRatioOperator | rs_sar_ratio_operator.h | rs_sar_ratio_operator.cpp | × | ∅ | sar | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_texture | RsSarTextureOperator | rs_sar_texture_operator.h | rs_sar_texture_operator.cpp | × | ∅ | sar | tolerance | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:sar_change | RsSarChangeOperator | rs_sar_change_operator.h | rs_sar_change_operator.cpp | × | ∅ | sar | tolerance | multipass_streaming | - | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:post_classification_change | RsPostClassificationChangeOperator | rs_post_classification_change_operator.h | rs_post_classification_change_operator.cpp | × | ∅ | change | bit_exact | multipass_streaming | - | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:qa_mask | RsQaMaskOperator | rs_qa_mask_operator.h | rs_qa_mask_operator.cpp | Y | quality-masking | optical | bit_exact | streaming | nogpu | - | raster→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:apply_mask | RsApplyMaskOperator | rs_apply_mask_operator.h | rs_apply_mask_operator.cpp | × | ∅ | optical | bit_exact | streaming | - | - | raster→boolean,integer,numeric,raster | Y | Y | Y | Y |
| rs:image_fusion | RsImageFusionOperator | rs_image_fusion_operator.h | rs_image_fusion_operator.cpp | × | ∅ | optical | bit_exact | streaming | - | - | raster→boolean,integer,raster,string | Y | Y | Y | Y |
| rs:fusion_linear | RsFusionLinearOperator | rs_fusion_aliases.h | ? | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:fusion_brovey | RsFusionBroveyOperator | rs_fusion_aliases.h | ? | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:fusion_pca | RsFusionPcaOperator | rs_fusion_aliases.h | ? | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:fusion_ihs | RsFusionIhsOperator | rs_fusion_aliases.h | ? | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:fusion_gram_schmidt | RsFusionGramSchmidtOperator | rs_fusion_aliases.h | ? | × | ∅ | optical | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:feature_stack | RsFeatureStackOperator | rs_feature_stack_operator.h | rs_feature_stack_operator.cpp | × | ∅ | classification | bit_exact | multipass_streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:feature_normalize | RsFeatureNormalizeOperator | rs_feature_normalize_operator.h | rs_feature_normalize_operator.cpp | × | ∅ | classification | tolerance | multipass_streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:feature_select | RsFeatureSelectOperator | rs_feature_select_operator.h | rs_feature_select_operator.cpp | × | ∅ | classification | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:terrain_analysis | RsTerrainAnalysisOperator | rs_terrain_analysis_operator.h | rs_terrain_analysis_operator.cpp | × | ∅ | terrain | bit_exact | streaming | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:topographic_correction | RsTopographicCorrectionOperator | rs_topographic_correction_operator.h | rs_topographic_correction_operator.cpp | Y | radiometric-normalization | optical | bit_exact | streaming | nogpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:solar_geometry | RsSolarGeometryOperator | rs_solar_geometry_operator.h | rs_solar_geometry_operator.cpp | Y | radiometric-normalization | optical | bit_exact | streaming | nogpu | - | raster→numeric | Y | Y | Y | Y |
| rs:brdf_normalization | RsBrdfNormalizationOperator | rs_brdf_normalization_operator.h | rs_brdf_normalization_operator.cpp | Y | radiometric-normalization | optical | bit_exact | streaming | nogpu | - | raster→integer,raster | Y | Y | Y | Y |
| rs:radiometric_qa | RsRadiometricQaOperator | rs_radiometric_qa_operator.h | rs_radiometric_qa_operator.cpp | Y | radiometric-normalization | optical | bit_exact | streaming | nogpu | - | raster→integer,raster | Y | Y | Y | Y |
| rs:spectral_derivative | RsSpectralDerivativeOperator | rs_spectral_derivative_operator.h | rs_spectral_derivative_operator.cpp | Y | feature-engineering | spectral | bit_exact | streaming | nogpu | - | raster→integer,raster | Y | Y | Y | Y |
| rs:matched_filter | RsMatchedFilterOperator | rs_spectral_detection_operators.h | rs_spectral_detection_operators.cpp | Y | target-detection | hyperspectral | bit_exact | multipass_streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:ace | RsAceOperator | rs_spectral_detection_operators.h | rs_spectral_detection_operators.cpp | Y | target-detection | hyperspectral | bit_exact | multipass_streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:cem_detection | RsCemOperator | rs_spectral_detection_operators.h | rs_spectral_detection_operators.cpp | Y | target-detection | hyperspectral | bit_exact | multipass_streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:tcimf_detection | RsTcimfOperator | rs_spectral_detection_operators.h | rs_spectral_detection_operators.cpp | Y | target-detection | hyperspectral | bit_exact | multipass_streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:osp_detection | RsOspOperator | rs_spectral_detection_operators.h | rs_spectral_detection_operators.cpp | Y | target-detection | hyperspectral | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:spectral_spatial_fuse | RsSpectralSpatialFuseOperator | rs_spectral_spatial_fuse_operator.h | rs_spectral_spatial_fuse_operator.cpp | Y | target-detection | hyperspectral | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:sar_dualpol_features | RsSarDualPolOperator | rs_sar_dualpol_operator.h | rs_sar_dualpol_operator.cpp | Y | sar-feature-engineering | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_terrain_masks | RsSarTerrainMasksOperator | rs_sar_terrain_masks_operator.h | rs_sar_terrain_masks_operator.cpp | Y | sar-geometry | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_geocode | RsSarGeocodeOperator | rs_sar_geocode_operator.h | rs_sar_geocode_operator.cpp | Y | sar-geometry | sar | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:rasterize | RsRasterizeOperator | rs_rasterize_operator.h | rs_rasterize_operator.cpp | Y | raster-vector | raster_spatial | bit_exact | streaming | nogpu | - | raster,vector→raster | Y | Y | Y | Y |
| rs:sar_temporal_stats | RsSarTemporalStatsOperator | rs_sar_temporal_stats_operator.h | rs_sar_temporal_stats_operator.cpp | Y | sar-temporal | sar | bit_exact | streaming | nogpu | - | ∅→raster | Y | Y | Y | Y |
| rs:sar_polsar_decompose | RsSarPolsarDecomposeOperator | rs_sar_polsar_decompose_operator.h | rs_sar_polsar_decompose_operator.cpp | Y | sar-polarimetry | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_interferogram | RsSarInterferogramOperator | rs_sar_interferogram_operator.h | rs_sar_interferogram_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_phase_filter | RsSarPhaseFilterOperator | rs_sar_phase_filter_operator.h | rs_sar_phase_filter_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:sar_unwrap | RsSarUnwrapOperator | rs_sar_unwrap_operator.h | rs_sar_unwrap_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_displacement | RsSarDisplacementOperator | rs_sar_displacement_operator.h | rs_sar_displacement_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_coregister | RsSarCoregisterOperator | rs_sar_coregister_operator.h | rs_sar_coregister_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:sar_temporal_events | RsSarTemporalEventsOperator | rs_sar_temporal_events_operator.h | rs_sar_temporal_events_operator.cpp | Y | sar-temporal | sar | bit_exact | streaming | nogpu | - | ∅→raster,string | Y | Y | Y | Y |
| rs:sar_remove_topographic_phase | RsSarRemoveTopographicPhaseOperator | rs_sar_remove_topographic_phase_operator.h | rs_sar_remove_topographic_phase_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:sar_coregister_local | RsSarCoregisterLocalOperator | rs_sar_coregister_local_operator.h | rs_sar_coregister_local_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:sar_pair_network | RsSarPairNetworkOperator | rs_sar_pair_network_operator.h | rs_sar_pair_network_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | ∅→json | Y | Y | Y | Y |
| rs:sar_network_inversion | RsSarNetworkInversionOperator | rs_sar_network_inversion_operator.h | rs_sar_network_inversion_operator.cpp | Y | sar-insar | sar | bit_exact | streaming | nogpu | - | ∅→raster | Y | Y | Y | Y |
| rs:zonal_stats | RsZonalStatsOperator | rs_zonal_stats_operator.h | rs_zonal_stats_operator.cpp | Y | raster-vector | raster_spatial | bit_exact | streaming | nogpu | - | raster,vector→table | Y | Y | Y | Y |
| rs:temporal_monitor | RsTemporalMonitorOperator | rs_temporal_monitor_operator.h | rs_temporal_monitor_operator.cpp | Y | temporal-monitoring | temporal | bit_exact | streaming | nogpu | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:terrain_flow | RsTerrainFlowOperator | rs_terrain_flow_operator.h | rs_terrain_flow_operator.cpp | Y | hydrology | terrain | bit_exact | full_raster | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:terrain_viewshed | RsTerrainViewshedOperator | rs_terrain_viewshed_operator.h | rs_terrain_viewshed_operator.cpp | Y | visibility | terrain | bit_exact | full_raster | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:terrain_solar | RsTerrainSolarOperator | rs_terrain_solar_operator.h | rs_terrain_solar_operator.cpp | Y | solar_terrain | terrain | bit_exact | full_raster | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:terrain_landform | RsTerrainLandformOperator | rs_terrain_landform_operator.h | rs_terrain_landform_operator.cpp | Y | landform_classification | terrain | bit_exact | full_raster | nogpu | - | raster→raster,string | Y | Y | Y | Y |
| rs:resample | RsResampleOperator | rs_grid_operators.h | rs_grid_operators.cpp | × | ∅ | raster_spatial | bit_exact | streaming | - | - | raster→integer,numeric,raster | Y | Y | Y | Y |
| rs:align | RsAlignOperator | rs_grid_operators.h | rs_grid_operators.cpp | × | ∅ | raster_spatial | bit_exact | streaming | - | - | raster→boolean,integer,raster | Y | Y | Y | Y |
| rs:morphology | RsMorphologyOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | full_raster | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:connected_components | RsConnectedComponentsOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | full_raster | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:fill_holes | RsFillHolesOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | full_raster | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:sieve | RsSieveOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | full_raster | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:proximity | RsProximityOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | full_raster | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:local_extrema | RsLocalExtremaOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:focal_stats | RsFocalStatsOperator | ? | rs_raster_spatial_operators.cpp | Y | raster_cleanup | raster_spatial | bit_exact | streaming | nogpu | - | raster→raster | Y | Y | Y | Y |
| rs:pca | RsPcaOperator | rs_pca_operator.h | rs_pca_operator.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:mnf | RsMnfOperator | rs_mnf_operator.h | rs_mnf_operator.cpp | × | ∅ | hyperspectral | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:mnf_inverse | RsMnfInverseOperator | rs_mnf_inverse_operator.h | rs_mnf_inverse_operator.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→json,raster | Y | Y | Y | Y |
| rs:spectral_band_select | RsSpectralBandSelectOperator | rs_spectral_band_select_operator.h | rs_spectral_band_select_operator.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:library_select | RsLibrarySelectOperator | rs_library_select_operator.h | rs_library_select_operator.cpp | × | ∅ | spectral | bit_exact | full_raster | - | - | ∅→integer,json | Y | Y | Y | Y |
| rs:mosaic | RsMosaicOperator | rs_mosaic_operator.h | rs_mosaic_operator.cpp | × | ∅ | raster_spatial | bit_exact | full_raster | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:quality_mosaic | RsQualityMosaicOperator | rs_quality_mosaic_operator.h | rs_quality_mosaic_operator.cpp | × | ∅ | raster_spatial | bit_exact | full_raster | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:temporal_summary | RsTemporalSummaryOperator | rs_temporal_summary_operator.h | rs_temporal_summary_operator.cpp | × | ∅ | temporal | bit_exact | multipass_streaming | - | - | ∅→integer,numeric,raster | Y | Y | Y | Y |
| rs:temporal_composite | RsTemporalCompositeOperator | rs_temporal_composite_operator.h | rs_temporal_composite_operator.cpp | × | ∅ | temporal | bit_exact | multipass_streaming | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:temporal_index_series | RsTemporalIndexSeriesOperator | rs_temporal_index_operator.h | rs_temporal_index_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:temporal_trend | RsTemporalTrendOperator | rs_temporal_trend_operator.h | rs_temporal_trend_operator.cpp | × | ∅ | temporal | bit_exact | multipass_streaming | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:temporal_smooth | RsTemporalSmoothOperator | rs_temporal_smooth_operator.h | rs_temporal_smooth_operator.cpp | × | ∅ | temporal | tolerance | multipass_streaming | - | - | ∅→integer,json,raster,string | Y | Y | Y | Y |
| rs:temporal_gap_fill | RsTemporalGapFillOperator | rs_temporal_gap_fill_operator.h | rs_temporal_gap_fill_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_harmonic_fit | RsTemporalHarmonicFitOperator | rs_temporal_harmonic_fit_operator.h | rs_temporal_harmonic_fit_operator.cpp | × | ∅ | temporal | bit_exact | multipass_streaming | - | - | ∅→boolean,integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_phenology | RsTemporalPhenologyOperator | rs_temporal_phenology_operator.h | rs_temporal_phenology_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_breakpoints | RsTemporalBreakpointsOperator | rs_temporal_breakpoints_operator.h | rs_temporal_breakpoints_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_sen_trend | RsTemporalSenTrendOperator | rs_temporal_sen_trend_operator.h | rs_temporal_sen_trend_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_sar_fusion | RsTemporalSarFusionOperator | rs_temporal_sar_fusion_operator.h | rs_temporal_sar_fusion_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | raster→integer,json,raster | Y | Y | Y | Y |
| rs:temporal_decompose | RsTemporalDecomposeOperator | rs_temporal_decompose_operator.h | rs_temporal_decompose_operator.cpp | × | ∅ | temporal | tolerance | multipass_streaming | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:temporal_anomaly | RsTemporalAnomalyOperator | rs_temporal_anomaly_operator.h | rs_temporal_anomaly_operator.cpp | × | ∅ | temporal | bit_exact | multipass_streaming | - | - | ∅→integer,raster | Y | Y | Y | Y |
| rs:temporal_extract_series | RsTemporalExtractSeriesOperator | rs_temporal_extract_series_operator.h | rs_temporal_extract_series_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→string,table | Y | Y | Y | Y |
| rs:temporal_extract_regions | RsTemporalExtractRegionsOperator | rs_temporal_extract_regions_operator.h | rs_temporal_extract_regions_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→boolean,integer,string,table | Y | Y | Y | Y |
| rs:temporal_regularize | RsTemporalRegularizeOperator | rs_temporal_regularize_operator.h | rs_temporal_regularize_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_harmonic_breaks | RsTemporalHarmonicBreaksOperator | rs_temporal_harmonic_breaks_operator.h | rs_temporal_harmonic_breaks_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster,string | Y | Y | Y | Y |
| rs:temporal_seasonal_breaks | RsTemporalSeasonalBreaksOperator | rs_temporal_seasonal_breaks_operator.h | rs_temporal_seasonal_breaks_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,raster,string | Y | Y | Y | Y |
| rs:temporal_model_select | RsTemporalModelSelectOperator | rs_temporal_model_select_operator.h | rs_temporal_model_select_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,raster | Y | Y | Y | Y |
| rs:temporal_phenology_multi | RsTemporalPhenologyMultiOperator | rs_temporal_phenology_multi_operator.h | rs_temporal_phenology_multi_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,json,numeric,raster | Y | Y | Y | Y |
| rs:temporal_region_features | RsTemporalRegionFeaturesOperator | rs_temporal_region_features_operator.h | rs_temporal_region_features_operator.cpp | × | ∅ | temporal | bit_exact | streaming | - | - | ∅→integer,string,table | Y | Y | Y | Y |
| rs:landsat_import | RsLandsatImportOperator | rs_landsat_import_operator.h | rs_landsat_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:sentinel2_import | RsSentinel2ImportOperator | rs_sentinel2_import_operator.h | rs_sentinel2_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:modis_import | RsModisImportOperator | rs_modis_import_operator.h | rs_modis_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:cn_product_import | RsCnProductImportOperator | rs_cn_product_import_operator.h | rs_cn_product_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:gaofen_import | RsGaofenImportOperator | rs_gaofen_import_operator.h | rs_gaofen_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:zy3_import | RsZy3ImportOperator | rs_zy3_import_operator.h | rs_zy3_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:hj_import | RsHjImportOperator | rs_hj_import_operator.h | rs_hj_import_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:modis_georeference | RsModisGeoreferenceOperator | rs_modis_georeference_operator.h | rs_modis_georeference_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,raster,string | Y | Y | Y | Y |
| rs:register_images | RsRegisterImagesOperator | rs_register_images_operator.h | rs_register_images_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:stack_register | RsStackRegisterOperator | rs_stack_register_operator.h | rs_stack_register_operator.cpp | × | ∅ | io | bit_exact | full_raster | - | - | ∅→integer,numeric,string | Y | Y | Y | Y |
| rs:kmeans_classification | RsKmeansOperator | rs_kmeans_operator.h | rs_kmeans_operator.cpp | × | ∅ | classification | bit_exact | full_raster | - | - | raster→integer,raster | Y | Y | Y | Y |
| rs:supervised_classification | RsSupervisedClassificationOperator | rs_supervised_classification_operator.h | rs_supervised_classification_operator.cpp | Y | classification | classification | bit_exact | full_raster | nogpu | - | raster,vector→integer,numeric,raster,string | Y | Y | Y | Y |
| rs:obia_segment | RsObiaSegmentOperator | rs_obia_segment_operator.h | rs_obia_segment_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:obia_classify | RsObiaClassifyOperator | rs_obia_classify_operator.h | rs_obia_classify_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster,vector→integer,raster,string | Y | Y | Y | Y |
| rs:obia_hierarchy | RsObiaHierarchyOperator | rs_obia_hierarchy_operator.h | rs_obia_hierarchy_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster,vector→integer | Y | Y | Y | Y |
| rs:obia_features | RsObiaFeaturesOperator | rs_obia_features_operator.h | rs_obia_features_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster→integer,table | Y | Y | Y | Y |
| rs:obia_label | RsObiaLabelOperator | rs_obia_label_operator.h | rs_obia_label_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster,vector→integer,table | Y | Y | Y | Y |
| rs:segment_stats | RsSegmentStatsOperator | rs_segment_stats_operator.h | rs_segment_stats_operator.cpp | × | ∅ | obia | bit_exact | full_raster | - | - | raster→integer,table | Y | Y | Y | Y |
| rs:majority_filter | RsMajorityFilterOperator | rs_majority_filter_operator.h | rs_majority_filter_operator.cpp | × | ∅ | raster_spatial | bit_exact | streaming | - | - | raster→raster | Y | Y | Y | Y |
| rs:recode | RsRecodeOperator | rs_recode_operator.h | rs_recode_operator.cpp | × | ∅ | raster_spatial | bit_exact | multipass_streaming | - | - | raster→raster | Y | Y | Y | Y |
| rs:infer | RsInferenceOperator | rs_inference_operator.h | rs_inference_operator.cpp | Y | inference | classification | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:segment | RsSegmentOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | segmentation | obia | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:detect | RsDetectOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | detection | classification | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:embedding | RsEmbeddingOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | embedding | classification | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:classify | RsClassifyOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | classification | classification | bit_exact | streaming | gpu | - | raster→integer,string | Y | Y | Y | Y |
| rs:change | RsChangeOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | change_detection | classification | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |
| rs:regress | RsRegressOperator | rs_model_task_operators.h | rs_model_task_operators.cpp | Y | regression | classification | bit_exact | streaming | gpu | - | raster→integer,raster,string | Y | Y | Y | Y |

## 缺口清单

### sparse（taskFamily）缺口（注册口径，设计上 opt-in，处置=backlog）

- rs:ndvi（RsNdviOperator，rs_spectral_index_aliases.cpp）
- rs:evi（RsEviOperator，rs_spectral_index_aliases.cpp）
- rs:ndwi（RsNdwiOperator，rs_spectral_index_aliases.cpp）
- rs:savi（RsSaviOperator，rs_spectral_index_aliases.cpp）
- rs:ndbi（RsNdbiOperator，rs_spectral_index_aliases.cpp）
- rs:mndwi（RsMndwiOperator，rs_spectral_index_aliases.cpp）
- rs:band_math（RsBandMathOperator，rs_band_math_operator.cpp）
- rs:band_ratio（RsBandRatioOperator，rs_band_tools_operators.cpp）
- rs:extract_bands（RsExtractBandsOperator，rs_band_tools_operators.cpp）
- rs:contrast_stretch（RsContrastStretchOperator，rs_band_tools_operators.cpp）
- rs:image_enhancement（RsImageEnhancementOperator，rs_image_enhancement_operator.cpp）
- rs:sam_classify（RsSamClassifyOperator，rs_sam_classify_operator.cpp）
- rs:spectral_unmixing（RsSpectralUnmixingOperator，rs_spectral_unmixing_operator.cpp）
- rs:rx_anomaly（RsRxAnomalyOperator，rs_rx_anomaly_operator.cpp）
- rs:continuum_removal（RsContinuumRemovalOperator，rs_continuum_removal_operator.cpp）
- rs:spectral_resample（RsSpectralResampleOperator，rs_spectral_resample_operator.cpp）
- rs:endmember_extraction（RsEndmemberExtractionOperator，rs_endmember_extraction_operator.cpp）
- rs:atmospheric_correction（RsAtmosphericCorrectionOperator，rs_atmospheric_correction_operator.cpp）
- rs:dn_to_radiance（RsDnToRadianceOperator，rs_atmospheric_aliases.cpp）
- rs:atmospheric_dos1（RsAtmosphericDos1Operator，rs_atmospheric_aliases.cpp）
- rs:atmospheric_dos2（RsAtmosphericDos2Operator，rs_atmospheric_aliases.cpp）
- rs:atmospheric_quac（RsAtmosphericQuacOperator，rs_atmospheric_aliases.cpp）
- rs:radiometric_calibration（RsRadiometricCalibrationOperator，rs_radiometric_calibration_operator.cpp）
- rs:change_difference（RsChangeDifferenceOperator，rs_change_primitives.cpp）
- rs:change_normalized_difference（RsChangeNormalizedDifferenceOperator，rs_change_primitives.cpp）
- rs:change_ratio（RsChangeRatioOperator，rs_change_primitives.cpp）
- rs:change_cva（RsChangeCvaOperator，rs_change_primitives.cpp）
- rs:change_cva_angle（RsChangeCvaAngleOperator，rs_change_primitives.cpp）
- rs:change_sam（RsChangeSamOperator，rs_change_primitives.cpp）
- rs:change_mad（RsChangeMadOperator，rs_change_primitives.cpp）
- rs:change_irmad（RsChangeIrMadOperator，rs_change_primitives.cpp）
- rs:threshold_raster（RsThresholdRasterOperator，rs_threshold_raster_operator.cpp）
- rs:sar_calibrate（RsSarCalibrateOperator，rs_sar_calibrate_operator.cpp）
- rs:sar_backscatter（RsSarBackscatterOperator，rs_sar_backscatter_operator.cpp）
- rs:sar_terrain_flatten（RsSarTerrainFlattenOperator，rs_sar_terrain_flatten_operator.cpp）
- rs:sar_terrain_correction（RsSarTerrainCorrectionOperator，rs_sar_terrain_correction_operator.cpp）
- rs:sar_speckle（RsSarSpeckleOperator，rs_sar_speckle_operator.cpp）
- rs:sar_ratio（RsSarRatioOperator，rs_sar_ratio_operator.cpp）
- rs:sar_texture（RsSarTextureOperator，rs_sar_texture_operator.cpp）
- rs:sar_change（RsSarChangeOperator，rs_sar_change_operator.cpp）
- rs:post_classification_change（RsPostClassificationChangeOperator，rs_post_classification_change_operator.cpp）
- rs:apply_mask（RsApplyMaskOperator，rs_apply_mask_operator.cpp）
- rs:image_fusion（RsImageFusionOperator，rs_image_fusion_operator.cpp）
- rs:fusion_linear（RsFusionLinearOperator，?）
- rs:fusion_brovey（RsFusionBroveyOperator，?）
- rs:fusion_pca（RsFusionPcaOperator，?）
- rs:fusion_ihs（RsFusionIhsOperator，?）
- rs:fusion_gram_schmidt（RsFusionGramSchmidtOperator，?）
- rs:feature_stack（RsFeatureStackOperator，rs_feature_stack_operator.cpp）
- rs:feature_normalize（RsFeatureNormalizeOperator，rs_feature_normalize_operator.cpp）
- rs:feature_select（RsFeatureSelectOperator，rs_feature_select_operator.cpp）
- rs:terrain_analysis（RsTerrainAnalysisOperator，rs_terrain_analysis_operator.cpp）
- rs:resample（RsResampleOperator，rs_grid_operators.cpp）
- rs:align（RsAlignOperator，rs_grid_operators.cpp）
- rs:pca（RsPcaOperator，rs_pca_operator.cpp）
- rs:mnf（RsMnfOperator，rs_mnf_operator.cpp）
- rs:mnf_inverse（RsMnfInverseOperator，rs_mnf_inverse_operator.cpp）
- rs:spectral_band_select（RsSpectralBandSelectOperator，rs_spectral_band_select_operator.cpp）
- rs:library_select（RsLibrarySelectOperator，rs_library_select_operator.cpp）
- rs:mosaic（RsMosaicOperator，rs_mosaic_operator.cpp）
- rs:quality_mosaic（RsQualityMosaicOperator，rs_quality_mosaic_operator.cpp）
- rs:temporal_summary（RsTemporalSummaryOperator，rs_temporal_summary_operator.cpp）
- rs:temporal_composite（RsTemporalCompositeOperator，rs_temporal_composite_operator.cpp）
- rs:temporal_index_series（RsTemporalIndexSeriesOperator，rs_temporal_index_operator.cpp）
- rs:temporal_trend（RsTemporalTrendOperator，rs_temporal_trend_operator.cpp）
- rs:temporal_smooth（RsTemporalSmoothOperator，rs_temporal_smooth_operator.cpp）
- rs:temporal_gap_fill（RsTemporalGapFillOperator，rs_temporal_gap_fill_operator.cpp）
- rs:temporal_harmonic_fit（RsTemporalHarmonicFitOperator，rs_temporal_harmonic_fit_operator.cpp）
- rs:temporal_phenology（RsTemporalPhenologyOperator，rs_temporal_phenology_operator.cpp）
- rs:temporal_breakpoints（RsTemporalBreakpointsOperator，rs_temporal_breakpoints_operator.cpp）
- rs:temporal_sen_trend（RsTemporalSenTrendOperator，rs_temporal_sen_trend_operator.cpp）
- rs:temporal_sar_fusion（RsTemporalSarFusionOperator，rs_temporal_sar_fusion_operator.cpp）
- rs:temporal_decompose（RsTemporalDecomposeOperator，rs_temporal_decompose_operator.cpp）
- rs:temporal_anomaly（RsTemporalAnomalyOperator，rs_temporal_anomaly_operator.cpp）
- rs:temporal_extract_series（RsTemporalExtractSeriesOperator，rs_temporal_extract_series_operator.cpp）
- rs:temporal_extract_regions（RsTemporalExtractRegionsOperator，rs_temporal_extract_regions_operator.cpp）
- rs:temporal_regularize（RsTemporalRegularizeOperator，rs_temporal_regularize_operator.cpp）
- rs:temporal_harmonic_breaks（RsTemporalHarmonicBreaksOperator，rs_temporal_harmonic_breaks_operator.cpp）
- rs:temporal_seasonal_breaks（RsTemporalSeasonalBreaksOperator，rs_temporal_seasonal_breaks_operator.cpp）
- rs:temporal_model_select（RsTemporalModelSelectOperator，rs_temporal_model_select_operator.cpp）
- rs:temporal_phenology_multi（RsTemporalPhenologyMultiOperator，rs_temporal_phenology_multi_operator.cpp）
- rs:temporal_region_features（RsTemporalRegionFeaturesOperator，rs_temporal_region_features_operator.cpp）
- rs:landsat_import（RsLandsatImportOperator，rs_landsat_import_operator.cpp）
- rs:sentinel2_import（RsSentinel2ImportOperator，rs_sentinel2_import_operator.cpp）
- rs:modis_import（RsModisImportOperator，rs_modis_import_operator.cpp）
- rs:cn_product_import（RsCnProductImportOperator，rs_cn_product_import_operator.cpp）
- rs:gaofen_import（RsGaofenImportOperator，rs_gaofen_import_operator.cpp）
- rs:zy3_import（RsZy3ImportOperator，rs_zy3_import_operator.cpp）
- rs:hj_import（RsHjImportOperator，rs_hj_import_operator.cpp）
- rs:modis_georeference（RsModisGeoreferenceOperator，rs_modis_georeference_operator.cpp）
- rs:register_images（RsRegisterImagesOperator，rs_register_images_operator.cpp）
- rs:stack_register（RsStackRegisterOperator，rs_stack_register_operator.cpp）
- rs:kmeans_classification（RsKmeansOperator，rs_kmeans_operator.cpp）
- rs:obia_segment（RsObiaSegmentOperator，rs_obia_segment_operator.cpp）
- rs:obia_classify（RsObiaClassifyOperator，rs_obia_classify_operator.cpp）
- rs:obia_hierarchy（RsObiaHierarchyOperator，rs_obia_hierarchy_operator.cpp）
- rs:obia_features（RsObiaFeaturesOperator，rs_obia_features_operator.cpp）
- rs:obia_label（RsObiaLabelOperator，rs_obia_label_operator.cpp）
- rs:segment_stats（RsSegmentStatsOperator，rs_segment_stats_operator.cpp）
- rs:majority_filter（RsMajorityFilterOperator，rs_majority_filter_operator.cpp）
- rs:recode（RsRecodeOperator，rs_recode_operator.cpp）

### authored summary 缺口（0）


### authored failure_modes 缺口（0）


### authored applicability 缺口（0）


### authored teaching_use 缺口（0）


### authored prerequisites 缺口（0）


### authored limitations 缺口（0）

