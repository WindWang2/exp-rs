# R21-B OWNERSHIP — 文件写权白名单（提 PR 前 100% 对账）

## 准入写区（任务规约 §5）
- `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`
- `src/operators/runtime/**`、`src/operators/framework/model_catalog*`
- `src/runtime/**`、`src/python/**`、`src/jobs/**`
- 对应 `tests/`、`benchmarks/` 中本轨命名文件
- `.planning/r21-B/**`

## 共享灰区（最小 append-only）
- 根 `CMakeLists.txt`、`tests/CMakeLists.txt`、`data/` 公共注册表、公共 ADR：仅在必需时 append；提交前与 origin/master 比对防冲突

## 禁改
- `src/analysis/**`、`src/processing/algorithms/**` 算法核心、`src/agent/**`、`src/sdk/**`、`src/workflow/**`、`src/plugins/**`、`src/app/**`、`src/gui/**`、`src/teaching/**`、`src/verification/**`
- `src/operators/rs/**`（算法行为归 A 轨；rs:infer 适配器只读或最小契约对齐）

## 本轨预计触碰文件（初版，随波次更新）
- src/data/providers/{ogr_vector_source_provider,gdal_raster_source_provider}.cpp（A1）
- src/data/artifact_store.cpp、src/data/workspace_catalog.cpp（A4）
- src/experiment/reproduction_bundle_import.cpp（A5）
- src/dataset/dataset_store.cpp（A6）
- src/operators/runtime/tile_inference_engine.cpp（B5）
- src/python/qgis_python.cpp、src/python/sicnu_python_api.cpp（#1647-1）
- tests/ 对应新增/扩展用例文件
