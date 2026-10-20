# R19-03 TEST_RESULTS

环境：worktree `exp-rs-03-qt-cartography-ux-20261020T102458Z`，build `build-r19-03-qt-cartography-ux`（Debug, ENABLE_TESTS=ON）；GCC 16.2.1 / CMake 3.30.5 / Qt 6.12 系统 + vendor GDAL 3.12.2 前缀（paleo-singlefactor-closure）；`-j2` + 全局锁 `$GIT_COMMON_DIR/r19-global-j2-build`；测试 `QT_QPA_PLATFORM=offscreen`，运行库环境 `LD_LIBRARY_PATH=/tmp/r19-03-deb:/usr/lib:<build>/src/core:<build>/src/gui`（/tmp/r19-03-deb 含 Qt6→系统 6.12 家族、GDAL 传递依赖闭包，复用 r18-a/r18-d 策划）。

## Configure/Build 迭代（/tmp/r1903-*.log）

- configure1 GSL 缺 → configure2 netcdf 缺 → configure3 EXIT=0（显式 -DGSL* / -DNETCDF*，对齐 r18-d 配方）
- build1 3445 TU 后 test_pipeline_canvas_widget 链接失败（vendor Qt 6.10 Network 私有符号 vs 系统 6.12 Core）→ build2 DBus 同类 → 预置 Qt6 全家 stub 链接后 build4 完成（等 r19-02 轨锁共约 100 分钟，跨轨 -j2 纪律实测互斥生效）
- test_help_core 首编译失败：我的用例 `QString::fromUcs4(int)` 无此重载 → 改显式代理对 D842 DFB7 后 EXIT=0（U+20BB7）
- 运行时依次补 libodbc/libodbcinst/libodbccr/libblosc/libxerces-c-3.2/libgeos_c/libcurl-gnutls 符号链接（均来自既有 vendor 前缀/r16-a 闭包，非新构建）

## RED 实证（修复前，全部真实失败）

| Oracle | 断言失败证据（file:line + 展开） |
|---|---|
| O-2 terminology 上界 | test_help_core.cpp:608 `CHECK( d->summary.size() <= kHelpSummaryMaxChars )` 展开 `302 (0x12e) <= 120` |
| O-1 菜单 gate | test_r14_D_vector_edit_honesty.cpp:1313/:1347 `REQUIRE( a != nullptr )` 展开 `nullptr != nullptr`（mActionCutFeatures；两 SECTION 各一处） |
| O-3 恒真替换 | test_m2_batch_a_dialogs.cpp（Set spectral curve profile）`REQUIRE( summary != nullptr )` 展开 `nullptr != nullptr`（spectralSummaryLabel 查无 objectName） |

## GREEN×2（修复后，commit 04e81fac0）

- `test_help_core [r19d]`: 6/6 ×2
- `test_r14_D_vector_edit_honesty [menu_session_gate]`: 30/30 ×2
- `test_m2_batch_a_dialogs` 全量: 132/132（11 用例）×2

## 变异探针（证明新断言可红）

- spectral_library_dialog.cpp 构造期 updateSpectrumSummary() 移除 → test_m2_batch_a_dialogs 1 用例失败（131/132）→ 恢复后 132/132。O-1/O-2 的可红性即其 RED 本身。

## 回归（全量，全部 EXIT=0）

| 二进制 | 结果 |
|---|---|
| test_r14_D_vector_edit_honesty 全量 | 370/370（17 用例） |
| test_help_core 全量 | 2172/2172（15 用例） |
| test_spectral_library_dialog | 48/48 |
| test_m2_dialog_stress | 720/720 |
| test_help_system / test_global_help_tips | 78/78 / 25/25 |
| test_e2e_open_issues | 443/443（47 用例） |
| test_terrain | 1450/1450 |
| test_visual_analytics | 100056/100056 |
| test_gui_job_adapter | 29/29 |
| test_classification_studio_widget | 35/35 |
| test_spectral_profile_widget | 154/154 |
| test_pipeline_canvas_widget | 139/139 |
| test_georef_dual_window_workbench | 27/27 |
| test_mapspec | 2830/2830（239 用例） |

## KNOWN_BASELINE_FAIL（本机渲染路径，与本次 diff 无关）

- test_cartography_production_11：3/28 失败（:811 atlas REQUIRE result.ok、:1160 e2e sha、:1296 corpus，错误形态 "atlas … no coverage layer" / 渲染相关）。**本 diff 零编图文件**（git diff --stat 核对：src/app 菜单/对话框、src/help、tests 三文件）；#1655 已在 master 把门反转为默认运行（SICNU_CP11_RENDER=0 才跳过），本机离屏渲染字体/图例路径即触发。记录待编图 owner 在带显示环境复核；SICNU_CP11_RENDER=1 探针 EXIT=42（同 3 失败，非挂起）。

## NOT_EXECUTED

- Windows/macOS 路径（C12）：本机仅 Linux offscreen
- sanitizer 构建：三轮共享构建资源已满；本轮改动无新线程/锁（门控循环为 GUI 线程同步调用）
