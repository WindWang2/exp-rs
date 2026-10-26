# R22-01 science 文件所有权

## 本轨写入范围
- `src/processing/algorithms/primitives/distance_transform.cpp`（#1707）
- `src/processing/algorithms/math_utils.cpp`（#1684，注意与 #1676 bothNormalizable 重构的合并协调）
- `src/processing/algorithms/resampler.cpp`（#1686）
- `src/processing/algorithms/portable_random.h`（**新增**，#1685 Qt-free 原语）
- `src/analysis/classification/rs_classification_split.cpp`（#1685）
- `src/analysis/classification/rs_classification_utils.h`（#1685）
- `src/analysis/classification/rs_training_data_extraction.cpp`（#1685）
- `src/analysis/segmentation/rs_multires_segmenter.cpp`（#1685）
- `src/operators/rs/rs_kmeans_operator.cpp`（#1685 reservoir，L210-285 区域）
- `src/processing/algorithms/endmember_extraction.cpp`（#1685 PPI）
- `src/operators/rs/rs_endmember_extraction_operator.cpp`（#1685 PPI，须与 kernel 同序列）
- `src/processing/algorithms/feature_matcher.cpp`（#1685 RANSAC）
- `tests/test_r22_science_*.cpp`（新增）+ `tests/CMakeLists.txt`（append-only 注册）
- `.planning/r22-science/**`

## 明确排除（其他轨）
- `src/operators/runtime/**`（03 轨）、`src/geospatial/remote/**`（02 轨）、Qt 工作台（05 轨）
- `src/faultlab/**`、`src/dataset/**`（只读引用其算法纪律，不编辑）
- 其他 R21/R22 worktree

## 跨轨建议出口
- `CROSS_TRACK_FINDINGS.md`
