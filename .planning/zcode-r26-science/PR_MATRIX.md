# PR 防撞矩阵（实时核实于 2026-10-30）

| PR | 轨道 | 与本轨交集 | 处置 |
|----|------|-----------|------|
| #1724 | R25-D UI/制图 | tests/CMakeLists.txt（append-only） | 尾部追加唯一块 |
| #1725 | R25-E 教学 | tests/CMakeLists.txt | 同上 |
| #1726 | R25-B 数据/推理 | tests/CMakeLists.txt；inference_plan 只读 | 不碰其源文件 |
| #1727 | R25-C Pi/MCP | tests/CMakeLists.txt；surface_diff_snapshot.json | 快照不改 |
| #1728 | R25-A 科学算法 | tests/CMakeLists.txt；sar_metadata/spectral_classification/terrain_flow/gdal_cell_geometry + 10 个 rs 算子 | 全部只读 |

## #1728 占用清单（逐文件，本轨禁改）
src/operators/rs/: rs_apply_mask_operator, rs_post_classification_change_operator, rs_qa_mask_operator,
rs_sar_terrain_correction_operator, rs_sar_terrain_flatten_operator, rs_spectral_derivative_operator,
rs_spectral_resample_operator, rs_spectral_similarity_operator, rs_terrain_flow_operator, rs_terrain_solar_operator
src/processing/algorithms/: sar/sar_metadata.*, spectral_classification.cpp, terrain_flow.*
src/processing/gdal/: gdal_cell_geometry.*
