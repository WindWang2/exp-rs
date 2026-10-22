# OWNERSHIP — zcode-r20-01

BASE = `28e1891390bb812dc7ef2fe045ac90492c9525f1`（origin/master）

## 文件级所有权证明（git diff --name-only BASE...HEAD 逐项）

| 文件 | Owner | 合法性 |
|---|---|---|
| src/analysis/classification/rs_classifier_isodata.cpp | 01 | `src/analysis/**` 优先允许区 |
| src/analysis/classification/rs_classifier_mlp.cpp | 01 | 同上 |
| src/processing/algorithms/mosaic_plan.cpp/.h | 01 | `src/processing/algorithms/**` 优先允许区 |
| src/processing/algorithms/sar/sar_calibration.cpp | 01 | 同上 |
| src/processing/algorithms/sar/sar_insar.cpp | 01 | 同上 |
| src/processing/algorithms/terrain_viewshed.cpp | 01 | 同上 |
| src/processing/algorithms/trend_analysis.cpp | 01 | 同上 |
| tests/test_classifier_isodata.cpp | 01 | 同名算法既有测试（`test_*classification*`/算法测试约定） |
| tests/test_classifier_mlp.cpp | 01 | 同上 |
| tests/test_d16_temporal_trend.cpp | 01 | `test_*temporal*` 既有测试 |
| tests/test_mosaic_plan.cpp | 01 | `test_*mosaic*` 既有测试 |
| tests/test_sar_insar.cpp | 01 | `test_*algorithm*` 族 SAR 既有测试 |
| tests/test_sar_kernels.cpp | 01 | 同上 |
| tests/test_terrain_viewshed.cpp | 01 | `test_*terrain*` 既有测试 |
| .planning/zcode-r20-01/** | 01 | 本轨账本目录 |

- 无共享根文件（CMakeLists、CLAUDE.md、docs/adr）修改；全部测试改动都在**已注册测试文件**内 append，零 CMake 变更。
- `src/agent/spatial_tools/terrain_spatial_tools.cpp`（T2 的消费方）**未修改**——契约修复在算法侧（viewshedR3 兑现其头文件承诺），消费方按既有 `v==255` 过滤逻辑自动恢复正确，无需跨轨改动。

## 跨轨发现登记（见 CROSS_TRACK_FINDINGS.md）

#1541（02）、#1642（02/03）、#1646 其余（02/03）、#1467 主体（02）。
