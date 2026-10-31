# TEST_RESULTS — R26-science

构建：build-r26-science（Debug, ENABLE_TESTS=ON, OTB/GDAL-vendor/ONNX OFF，
ccache launcher），-j4 + flock ~/.cache/exp-rs-build.lock；ctest -j1。

## RED（未修复 master 2610cb4741 之上，逐用例过滤运行）
| WP | 结果 | 证据 |
|----|------|------|
| S1 | FAIL(SIGABRT) | libstdc++ vector 断言 `__n < this->size()`：pct=100 OOB 读 |
| S2 | FAIL | classCounts {1:~5,2:~5} ≠ {1:10,2:8}（cap 被 NoData 吃掉） |
| S3 | FAIL | knot x=[1,1,2] 非严格递增；fromJson 往返 false |
| S4 | FAIL | varS=66/18≠48/18（时间并列项缺失）等 5 断言 |
| S5 | FAIL | 折组成与排序参考模拟不一致（QHash 序 {16,27,5}≠升序） |
| S6 | FAIL | remap 缺失时 foldAccuracies=1.0（伪精度）≠0 |
| S7 | FAIL | {0:3,5:3}→5≠0（哨兵 best==0） |
| S8 | FAIL | 对角对象 polygonize 出 2 个要素≠1 |
| S9 | FAIL | NaN→0.0≠NaN；负值→0.0≠log((2+eps)/eps) |
| S10 | FAIL | changedArea=4.0(ft² 当 m²)≠0.371612 |
| S12 | FAIL | method=7/version=2 被接受（fromJson true）≠false |
| S11 | PASS(未复现) | OpenCV 5.0 copyTo 行视图跨类型就地正确转换（探针
   /tmp/cv_probe.cpp: 1.5/2.5/3.5/4.5 正确落父矩阵）→ 判 UNREPRODUCED，
   保留测试为 CV_64F 契约守卫，不改生产代码 |

## GREEN ×2（修复后 HEAD）
- 第 1 轮：All tests passed（190 assertions / 12 cases）
- 第 2 轮：All tests passed（190 assertions / 12 cases）
- S4 修订后（tau-a 契约保留 + varS 时间并列项）+ E2E 加入：
  ctest test_r26_science_oracles Passed（13 cases, 342 assertions 含 E2E 152）

## 复审后最终验证（R1+R2 修复全部落地）
- 16 个套件 × 连续两轮全绿（RUN A / RUN B，逐二进制退出码 0）
- test_r26_science_oracles: 354 assertions / 13 cases
- 注册完整性: 1064 targets + 51 add_test（base 1063/50；added 1+1，lost 0+0）
- git diff --check 干净；ctest test_r26_science_oracles Passed

## 既有回归（修复后 HEAD，直接执行二进制）
| 套件 | 结果 |
|------|------|
| test_training_data_extraction | PASS 766 断言/10 用例 |
| test_probability_calibration | PASS 69/10 |
| test_cross_validation | PASS 8/4 |
| test_spatial_cross_validation | PASS 592/7 |
| test_feature_scaler (+minmax) | PASS 46/4 + 15/1 |
| test_object_hierarchy | PASS 271/36 |
| test_segment_features | PASS 104/10 |
| test_image_enhancement | PASS 6247/16 |
| test_d16_temporal_trend | PASS 72/4（Gilbert 基准保留：tau-a=42/45、varS=124） |
| test_change_detector | PASS 46/6 |
| test_change_detection | PASS 888684/41（含流式/全帧奇偶与地理 CRS 面积） |
| test_obia_segmentation | PASS 871/16 |
| test_simple_segmenter | PASS 85/13 |

回归变异检测：RED 阶段本身即证明 11 个 WP 的测试对未修复（=回归态）代码失败。
