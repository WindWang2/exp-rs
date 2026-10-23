# TEST_RESULTS — zcode-r20-01

环境：CachyOS linux x64，16 核 / 62 GB；构建 `build-r20-01`（Debug，ENABLE_TESTS=ON），全局锁 `exp-rs-zcode-global-build.lock`、`-j2`；测试 `QT_QPA_PLATFORM=offscreen`，二进制直跑（单进程）。

## 构建记录

| 阶段 | 命令 | 退出码 | 说明 |
|---|---|---|---|
| 基线全量 | `flock … cmake --build build-r20-01 --parallel 2` | 0 | ~2h，master+（当时工作区的修复/测试尚未入） |
| 增量（修复+测试） | 同上 | 0 | 全部目标 |
| 目标构建（R1 修复后） | 同上（全目标） | 0* | *build2 中 ld 链接 test_execution_fingerprint 时段错误（工具链瞬态，非代码错误），按协议降 -j1 重试该目标 exit=0；随后全目标 -j1 收尾 exit=0 |
| RED 构建 | checkout origin/master -- src/ 后 --target 6 目标 | 0 | master 源码 + 新测试可编译（mosaic 除外，见下） |
| 恢复构建 | checkout HEAD -- src/ 后同 7 目标 | 0 | RED→GREEN 闭环 |

## GREEN（HEAD，修复后）——第 1 遍（R1 评审员同机复核 + 本轨主跑）

| 二进制 | 结果 | 断言/用例 |
|---|---|---|
| test_d16_temporal_trend | ✅ all passed | 84 / 4（含新用例 "Raster trend keeps value/time pairing…"） |
| test_terrain_viewshed | ✅ | 1453 / 7（含新 "viewshed marks DEM-NoData cells…"） |
| test_mosaic_plan | ✅ | 101 / 10（含新 "X-flipped … rejected like a Y mirror"） |
| test_classifier_mlp | ✅ | 16 / 3（新 classOrder 用例走真实训练路径：8 断言） |
| test_classifier_isodata | ✅ | 461 / 8（含新 fail-closed 用例） |
| test_sar_insar | ✅ | 284 / 11（含新 large-scene/shift/degenerate 用例） |
| test_sar_kernels | ✅ | 113 / 13（含新 incidence-domain 用例） |

## GREEN 第 2 遍（独立复跑，与第 1 遍断言数逐一相同）

test_d16_temporal_trend 84/4 ✅ · test_terrain_viewshed 1453/7 ✅ · test_mosaic_plan 101/10 ✅ · test_classifier_mlp 16/3 ✅ · test_classifier_isodata 461/8 ✅ · test_sar_insar 284/11 ✅ · test_sar_kernels 113/13 ✅（7/7 exit=0）
恢复后再验证（RED 闭环后）：同上 7 目标 + test_sar_operators 901/28 全绿。

## RED（master 源码 + 新测试）

方法：`git checkout origin/master -- src/` → 目标重建 → 直跑 → `git checkout HEAD -- src/`。
**实测（2026-10-23）**：6/6 二进制 exit=42，每个恰好 1 个新用例失败、既有用例全过——判别力与归因同时成立：
- test_d16_temporal_trend:279 `REQUIRE(slope[0]==Approx(0.0667))` 扩展 `0.1f == Approx(0.0667)`（message: raster slope=0.1 single=0.0667 oracle=0.0667）
- test_terrain_viewshed:250（NoData 像元应为 255）
- test_classifier_mlp:157（classOrder isValid 失败，[7,7]）
- test_classifier_isodata:264（CV_32F y 未被拒绝）
- test_sar_insar:465（20k 格点系数不可恢复）
- test_sar_kernels:418 `REQUIRE(isnan(...))` 扩展 `false`，**message: theta=90 gamma0=1.63312e+16**（预测的有限毒值实测穿透）
- test_mosaic_plan：对 master 头文件编译失败（xDirectionMismatch 字段不存在）= T3 编译级 RED（结构性新增 API，无运行态可比）

## 回归（受影响模块既有测试族）

R1 评审员同机实跑（21 目标全绿）：test_mosaic(38)/test_mosaic_balancing(91)/test_mosaic_quality(17)/test_mosaic_scale(11)/test_mosaic_seamline(61)/test_mosaic_blend(5072)/test_quality_mosaic_operator(2002)/test_quality_mosaic_registration(12)/test_terrain(1450)/test_terrain_agent_tools(52)/test_terrain_analytics_e2e(1088)/test_spatial_tools(145)/test_d16_temporal_tools(51)/test_classification_pipeline(240)/test_classifier_load_save(623)。
operator seam 改动后：test_sar_operators 901/28 ✅、test_sar_radiometric_state 404/16 ✅（含 rs:sar_backscatter 既有契约）。

## R2 处置后（最终态，连续两遍全绿，exit=0 ×18）

test_d16_temporal_trend 84/4 · test_terrain_viewshed 1453/7 · test_mosaic_plan 101/10 · test_classifier_mlp 21/4（+MLP 守卫用例） · test_classifier_isodata 461/8 · test_sar_insar 284/11 · test_sar_kernels 113/13 · test_sar_operators 910/29（+incidence-domain 缝用例） · test_sar_radiometric_state 404/16
