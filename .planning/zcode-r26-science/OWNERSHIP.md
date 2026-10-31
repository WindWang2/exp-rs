# OWNERSHIP — 逐文件批准清单（本轨写区）

## src/analysis/classification/（独占，无开放 PR 占用）
rs_training_data_extraction.cpp [WP-S2]、rs_probability_calibration.cpp [WP-S3]、
rs_cross_validation.cpp [WP-S5/S6]、rs_spatial_cross_validation.cpp [WP-S11]、
rs_feature_scaler.cpp [WP-S12]

## src/analysis/segmentation/（独占）
rs_majority_vote.h [WP-S7]、rs_class_raster.cpp [WP-S8]

## src/processing/algorithms/（未被 #1728 占用，逐文件批准）
image_enhancement.cpp [WP-S1]、trend_analysis.cpp [WP-S4]、change_detector.cpp [WP-S9]
（#1728 占用 sar_metadata/spectral_classification/terrain_flow —— 本轨不触碰）

## src/operators/rs/（不在任何开放 PR changed-files，逐文件批准）
rs_change_streaming.cpp [WP-S10]、rs_image_enhancement_operator.cpp [WP-S1 参数钳制]
（#1728 占用的 10 个算子文件不触碰）

## tests/
test_r26_science_oracles.cpp（新增）、tests/CMakeLists.txt（尾部唯一 append 块）

## 只读确认
src/geospatial/**、src/data/**、src/operators/runtime/inference_plan.* (#1726)、
tests/surface_diff_snapshot.json (#1727+#1728 双占)
