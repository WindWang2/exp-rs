# R22-02 (r22-data) PR_COLLISION_MATRIX — 与 open PR 的文件交集（2026-10-25 13:4x UTC 复核）

## Open PR 变更文件全景（gh pr view --json files，逐条核对）

| PR | 轨 | head | 与本轨计划 write-set 交集 |
|---|---|---|---|
| #1673 r21-B data/model-runtime | data 邻域 | 941af0d1 | `src/dataset/dataset_store.cpp`（#1673 改 createDataset TOCTOU/A6；本轨仅改 `addSourceAssetsToDraft` 的 stepRow 误报，:707-762，hunk 不重叠）、`tests/CMakeLists.txt`（双方 append-only） |
| #1674 r21-E teaching/verification | — | 0d919f50 | 无 |
| #1675 r21-C pi/mcp harness | — | d088f0a4 | 无 |
| #1676 r21-A science | — | ec7d85ef | 无 |
| #1677 r21-D qt/qgis | — | 37c5f539 | 无 |

## 本轨计划触碰文件 × 交集评估

| 本轨文件 | 唯一相交 PR | 冲突性质 | 处置 |
|---|---|---|---|
| `src/geospatial/remote/offline_gate.cpp` 等 remote/* | 无 | — | — |
| `src/geospatial/util/atomic_fs.cpp` | 无 | — | — |
| `src/geospatial/remote/range_cache*.cpp` | 无 | — | — |
| `src/data/artifact_object_pool.cpp` | 无 | — | — |
| `src/dataset/sample_promotion.cpp` | 无 | — | — |
| `src/dataset/dataset_store.cpp` | #1673 | 不同函数、不同 hunk；均为加错误码分支，语义正交 | 保留双方；PR body 声明；push 前重查 #1673 files |
| `src/geospatial/raster/raster_reader.cpp` | 无 | — | — |
| `tests/CMakeLists.txt` | #1673（+其余 4 轨全部改过） | append-only 注册表 | 只在文件尾部/相应 io-test 段追加注册行，绝不移动他人行 |
| 新增 `tests/test_r22_data_*.cpp` | 无 | 全新文件 | — |
| 既有测试文件追加 case（test_io_remote_validator 等） | 无（5 PR 均未改这些文件） | 追加 TEST_CASE | 不改既有断言 |

## 结论

- 无符号级/同 hunk 冲突；`dataset_store.cpp` 与 `tests/CMakeLists.txt` 为可自动合并的相邻变更。
- Push 前动作：`git fetch origin` + 重跑 `gh pr list`/`gh pr view <n> --json files` 复核；若 #1673 在此期间改了 `addSourceAssetsToDraft` 邻域，rebase 后语义合并（不以 ours/theirs 粗暴覆盖）。

## Push 前复核（2026-10-26 交付时）

- origin/master 仍 = `911e0b17`（gh api 双确认；git fetch 经代理仍间歇失败，不影响远端 SHA 判定）。
- 新 open PR：**#1708（r22-infra）**、**#1709（r22-inference）**。
  - #1708：src/cli、src/verification、.github、CMakeLists.txt、scripts、tests 若干（test_data_manager/test_data_source_providers/test_asset_catalog_index 等）——与本轨 src/ 文件**零交集**；tests/CMakeLists.txt 双方 append-only。
  - #1709：src/operators/runtime（03 轨）、tests/CMakeLists.txt、新测试文件——本轨交集仍仅 tests/CMakeLists.txt（相邻追加，Git 可自动合并或 trivial resolve）。
- R21 五轨（#1673–#1677）head 未动。
- 结论：无语义冲突；唯一注意点为 tests/CMakeLists.txt 的 append 相邻性（合并顺序无关于正确性）。
