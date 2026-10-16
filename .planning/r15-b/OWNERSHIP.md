# R15-B 写入所有权（OWNERSHIP）

## 可写
- `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`
- 测试：`tests/test_r15_B_*`，以及本轨历史测试（geodata IO：`test_io_*` 中本轨负责的文件）
- `tests/CMakeLists.txt`：仅最小追加（新增 1 个 target 块）

## 本轮实际改动文件
| 文件 | WP | 说明 |
|---|---|---|
| src/geospatial/util/atomic_fs.{h,cpp} | B13 | 新增 `hasUncommittedPublishJournal` / `throwIfInterruptedPublish` |
| src/geospatial/raster/raster_reader.cpp | B13 | open 前调用读侧闸门 |
| src/geospatial/vector/vector_reader.cpp | B13 | 同上 |
| src/geospatial/fabric/catalog_service.cpp | B05 | searchAll 远端续页去环 |
| src/geospatial/remote/remote_identity_resolver.cpp | B04/B15 | 拒绝/传输失败 ⇒ 空 token |
| src/experiment/reproduction_bundle_import.cpp | B12 | data/ 迁移只接受被校验的常规文件 |
| src/experiment/experiment_store.cpp | B07 | 扫描/计数中途错误 ⇒ 类型化失败 |
| tests/test_io_remote_validator.cpp | B04 | 历史 oracle 与 #1504 安全默认对齐（最小改动） |
| tests/test_r15_B_geodata_persistence.cpp | 全部 | 新增 |
| tests/CMakeLists.txt | — | 追加 1 个 target |

## 保护（只读，未修改）
- `src/geospatial/util/resource_uri.cpp`、`src/geospatial/remote/offline_gate.cpp`、`src/geospatial/remote/range_cache.{h,cpp}`（#1505 已合并，但保守保持只读；仅新增回归 oracle）
- `src/core/temporal_cube.cpp`、`src/python/**`、`src/runtime/gpu/gpu_plane.cpp`、`src/operators/runtime/**`（C）、`src/processing/**`（A）、`src/app/**`（E）
- 根 `CMakeLists.txt`、`.gitignore`、共享清单：未改
- 其他工作树（exp-rs、exp-rs-ci-fix、exp-rs-merge-1272、exp-rs-wt/*）：未触碰
