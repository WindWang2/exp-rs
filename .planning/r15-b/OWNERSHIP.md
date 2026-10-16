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

## 所有权变更记录
- #1505 合并后（第 8 项）：`src/geospatial/util/resource_uri.cpp`（bfe83d41）、`src/geospatial/remote/range_cache.cpp`（7ed6dc02）、`src/geospatial/remote/offline_gate.{h,cpp}` 与 `http_fetch.cpp`（fc712c72）属于本轨的 src/geospatial/** 范围。上面"保护"一节中对它们的只读约束到此为止。
- **2026-10-17（UTC+8）用户批准**：为实现 #1505 T11（生产默认开启私网阻断），本轨**仅为此目的**获得 `src/cli/**` 写权限。用户原话："implement #1505 T11 in PR #1513, and you MAY now write to src/cli/** for this purpose only"。
  - 实际修改：`src/cli/cli_commands.cpp` 中的远端入口 `data cache <url>`、`data cache prefetch`、`data identity`，以及 `--allow-private-network` 显式放行开关。
  - 为了让依赖回环 fixture 的既有 CLI 测试显式放行，`tests/test_cli_commands_json.cpp` 做了最小改动（局部 helper）。
  - src/cli/** 的其余部分未改。
