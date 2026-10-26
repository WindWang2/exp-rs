# R22-05 RED → GREEN 证据（持久化）

环境：`build-r22-workbench`（Release，系统 Qt 6.12 + vendor GDAL/PROJ/GEOS + `/tmp/r22wb-qt` Qt shim + `/tmp/r21a-deb` wrapper2 stub + `--allow-shlib-undefined`，R21-A 配方沿袭）；运行时 `QT_QPA_PLATFORM=offscreen`、`PROJ_LIB=seismic-prefix/share/proj`、`LD_LIBRARY_PATH=/tmp/r22wb-qt:/tmp/r21a-libs:seismic-lib:/tmp/r21a-deb`。
注：/tmp 构建日志在会话后即失，以下为当时逐字记录（R2-6 要求持久化；R2 评审员亦独立复现 RED——见 REVIEW_R2.md "verification log"）。

## RED（生产修复应用前，@911e0b17a3 + 测试，2026-10-26 09:0x）

```
=== test_r22_workbench_async_runner ===
cpp:110 FAILED: REQUIRE( QApplication::overrideCursor() == nullptr )   [AsyncGdalRunner 析构光标滞留]
cpp:122 FAILED: REQUIRE( QApplication::overrideCursor() == nullptr )   [AsyncAlgorithmRunner 析构光标滞留]
cpp:134 fatal: SIGSEGV                                                [父 widget 先亡 → QPointer(悬垂 m_parentWidget)]
test cases: 2 | 2 failed（后续用例被 fatal 信号中止）

=== test_r22_workbench_view_tree ===
cpp:130 FAILED  [zoom 1:1 未重投影：3857 米当度，偏差 5 个数量级]
cpp:150 fatal: SIGSEGV + FAILED [组删除经 QGIS 默认动作 → 层被删、租约滞留]
test cases: 2 | 0 passed | 2 failed

=== test_r22_workbench_temporal_pages ===
cpp:74/79 FAILED ×3 组（seasonal_breaks/model_select/phenology_multi → currentIndex=0 汇总页；marker 不在可见页）
assertions: 30 | 24 passed | 6 failed

=== test_r22_workbench_log_tagfilter ===
cpp:65 FAILED  [键入 "ndv" 后可见计数仍 2 —— typed filter 被忽略]
assertions: 24 | 23 passed | 1 failed

=== test_r22_workbench_ui_contract ===
5/5 用例红（:53 外部取消未置 m_canceled；:67 无 deleteLater；:86 DPI：；:87/:88 item(s)/component(s)；:90 缺单复数分支；:105 头文件无 jobCancelInFlight；:112 closeEvent 无逃逸；:123 .arg(19)；:128 无 editingToolCount）
assertions: 30 | 20 passed | 10 failed
```

R2 独立复核（2026-10-26）：评审员自行将 8 个生产文件 revert 至 911e0b17a3 重建 ui_contract → 5/5 用例、12 断言红；restore 后字节一致复绿（worktree clean）。

## GREEN（修复后，双跑一致）

```
2026-10-26 09:4x，pass1 == pass2：
test_r22_workbench_async_runner    :: All tests passed (24 assertions in 4 test cases)
test_r22_workbench_view_tree       :: All tests passed (70 assertions in 2 test cases)
test_r22_workbench_temporal_pages  :: All tests passed (30 assertions in 1 test case)
test_r22_workbench_log_tagfilter   :: All tests passed (24 assertions in 1 test case)
test_r22_workbench_ui_contract     :: All tests passed (30 assertions in 5 test cases)   [R1 后 32/5；R2 后 33/5]
```

## R1/R2 修复后复跑（2026-10-26 11:xx，最终态）

```
test_r22_workbench_async_runner :: 24/4 ✔   test_m2_batch_b_dialogs :: 193/19 ✔
test_r22_workbench_view_tree    :: 70/2 ✔   test_data_manager_panel :: 101/16 ✔
test_r22_workbench_temporal_pages :: 30/1 ✔ test_m3_stress :: 5241/7 ✔
test_r22_workbench_log_tagfilter :: 24/1 ✔  （R2 修复后的最终双跑见 TEST_RESULTS.md）
test_r22_workbench_ui_contract  :: 32→33/5 ✔
```

## 邻域回归（修复后单跑，全部既有目标）

```
test_async_algorithm_runner 8/2 ✔    test_batch_processing_dialog 35/5 ✔
test_ui_task_center_contract 169/6 ✔ test_layout_designer 48/9 ✔
test_raster_processing_dialog_base 120/11 ✔  test_workspace_browser_wiring 39/4 ✔
test_gui_job_adapter 29/3 ✔          test_active_view_host_viewport 12/2 ✔
test_log_panel 7/4 ✔
```
