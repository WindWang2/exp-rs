# R25-A PR_COLLISION_MATRIX

- 执行时快照：0 open PR；8 条 agent/r23*/r24* 遗留分支全部 ahead=0（完全合并）
- 本轨改动文件与开放 PR 交集：**无**（0 开放 PR）
- push 前将重新 `git fetch origin --prune` + `gh pr list` 复核新增 PR 的 changed files 交集
- 本轨触碰的共享面：`tests/CMakeLists.txt`（仅末尾追加 R25-A 块）、`.gitignore`（末尾追加 allow-list 块）——均为 append-only，union 合并安全
- merge-integrity 守卫：`python scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`（push 前执行）

## 改动文件清单（相对 origin/master）
- src/processing/gdal/gdal_cell_geometry.{h,cpp}（+sceneLatitudeDeg）
- src/processing/algorithms/terrain_flow.{h,cpp}（+flowAccumulationCounts）
- src/operators/rs/rs_terrain_flow_operator.cpp、rs_terrain_solar_operator.cpp
- src/operators/rs/rs_spectral_derivative_operator.cpp、rs_spectral_resample_operator.cpp、rs_apply_mask_operator.cpp
- src/processing/algorithms/spectral_classification.cpp、src/operators/rs/rs_spectral_similarity_operator.cpp
- src/processing/algorithms/sar/sar_metadata.{h,cpp}（+datasetMetaDouble）、rs_sar_terrain_flatten_operator.cpp、rs_sar_terrain_correction_operator.cpp
- src/operators/rs/rs_qa_mask_operator.cpp、rs_post_classification_change_operator.cpp
- tests/test_r25_a_*.cpp ×7 + tests/CMakeLists.txt（追加块）
- .gitignore（追加块）、.planning/zcode-r25-A/*.md
