# TEST_RESULTS — r21-A

环境：worktree build-r21-A（Debug, ENABLE_TESTS=ON），runner `.planning/r21-A/run-tests.sh`
（vendor GDAL 依赖 shim /tmp/r21a-libs + PROJ_LIB + wrapper2 stub），QT_QPA_PLATFORM=offscreen，
每次单测试二进制串行执行。ctest 在本机不存在（/home/kevin/.local/bin/ctest 为 cache 陈旧记录）。

## O-A1：R20 #1671 七族契约回归（两遍全绿）

| 测试族 | 第一遍 | 第二遍 |
|---|---|---|
| test_d16_temporal_trend | 84 assertions / 4 cases PASS | 同左 PASS |
| test_terrain_viewshed | 1453 / 7 PASS | 同左 PASS |
| test_mosaic_plan | 101 / 10 PASS | 同左 PASS |
| test_classifier_mlp | 21 / 4 PASS | 同左 PASS |
| test_classifier_isodata | 461 / 8 PASS | 同左 PASS |
| test_sar_kernels | 139 / 14 PASS | 同左 PASS |
| test_sar_insar | 284 / 11 PASS | 同左 PASS |

注：test_mosaic_plan 首遍 3 failed（PROJ 找不到 proj.db，环境问题）→ 设 PROJ_LIB 后两遍绿。

## 本轨新增/受影响测试族（两遍全绿）

| 测试族 | 断言数/用例 | 第一遍 | 第二遍 |
|---|---|---|---|
| test_obia_segmentation | 871 / 16 | PASS | PASS |
| test_spectral_classification | 205 / 32 | PASS | PASS |
| test_spectral_unmixing | 72 / 11 | PASS | PASS |
| test_classification_intelligence_e2e | 6208 / 6 | PASS | PASS |
| test_atmospheric | 37548 / 48 | PASS | PASS |
| test_fast_6s_atmospheric | 1215 / 9 | PASS | PASS |
| test_radiometric_calibration | 195713 / 34 | PASS | PASS |
| test_rs_operators | 9114 / 84 | PASS | PASS |
| test_temporal_calendar | 96 / 11 | PASS* | PASS |
| test_terrain_solar | 69 / 5 | PASS* | PASS |
| test_image_fusion | 1721 / 49 | PASS | PASS |
| test_pca | 287 / 8 | PASS | PASS |
| test_continuum_removal | 69 / 7 | PASS | PASS |

*首遍暴露两个真实缺陷（AG4-1 窗口起点索引 off-by-one、terrain_solar 测试自身反向断言），修复后两遍绿——
缺陷发现与修复均见 RED_GREEN.md 第 15/16 项。

## 未执行项（诚实归因）

- 全量 ctest 套件（~450 个目标）未跑：ctest 不可用 + 全量串行耗时数小时；本轨以目标域 20 个族 + 构建 100% 通过替代，
  提交前 R1/R2 评审阶段将追加 sar 家族全量（test_sar_*）、terrain 家族全量（test_terrain_*）、
  temporal 家族全量（test_temporal_*）与 mosaic/fusion 家族作为回归底网。
- Tsan 不可用（preset sanitizer-debug 无 TSan；且工作树 Qt/GEOS 双版本环境复杂）。#1591 并发修复以
  线程 hammer + 结果一致性断言验证（确定性错误在 hammer 下可暴露），非 TSan 实证。
- Win/mac 不可用（单 Linux 主机）。

## 家族底网清扫（sar/terrain/temporal/mosaic 52 族 + 分类/谱学 104 族）

- sar/terrain/temporal/mosaic：51/52 首遍绿；test_sar_foundation5 首遍 1 failed →
  terrainRadiometricFactor 域检查 (0,180) 应为 (0,90)（本轨 sin 形式重实现引入）→ 修复后绿。
- 分类/OBIA/谱学/辐射 104 族：1 failed → test_obia_operators kmeans 方法用例 →
  AG5-3 修复（未映射簇写 0）暴露 RsClassRaster::paint 既有契约矛盾（头文件"ids>=1" vs
  "0 写 NoData" vs OBIA 调用方"缺席段画 0"）→ 对齐为允许 0、拒绝负值 → 绿。
- 上述两失败均为本轨改动真实暴露的下游契约问题，非环境问题；修复后两遍全绿。

## 最终状态

- 构建：100%（Debug, tests ON, EXIT=0）
- O-A1 七族：两遍全绿
- 本轨新增/受影响 13 族 + 家族底网 ~156 族：两遍全绿

## R2 后终验（HEAD=884a1e08）

- O-A1 七族：两遍全绿（计数同首轮）。
- R2 修复受影响 12 族：两遍全绿（test_object_hierarchy 271/36、test_rs_operators 9114/84、
  test_image_fusion 1721/49、test_temporal_calendar 96/11、test_temporal_operators_10 472/10、
  test_atmospheric 37548/48、test_obia_segmentation 871/16、test_sar_kernels 139/14、
  test_sar_operators 910/29、test_obia_operators 170/18、test_classification_intelligence_e2e 6208/6、
  test_spectral_formula_drift 145/4）。
- sar + terrain 全家族（29 个二进制）：全绿。
- 构建：Debug + tests ON，100%。
