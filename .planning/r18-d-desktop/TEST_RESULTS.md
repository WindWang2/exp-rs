# R18-D TEST_RESULTS

环境：build-r18-d（Debug, ENABLE_TESTS=ON, QT_QPA_PLATFORM=offscreen, -j2, 全局锁 /tmp/exp-rs-five-tracks-build.lock）
依赖：vendor prefix（GDAL/PROJ/GEOS/GSL/QCA）+ 系统 Qt6 6.12 + 共享 FetchContent 缓存 catch2-v3.7.1（~/.cache/exp-rs-fdeps）+ /tmp/r18d-deb 链接/运行 stub 目录（wrapper2stub、cblas→openblas、Qml 家族→系统 6.12、armadillo stub、pulsecommon→系统）
测试运行方式：ctest discovery 因运行库链修复前不可用，采用直接运行测试二进制（Catch2 过滤 [r18d] 与全量），env: QT_QPA_PLATFORM=offscreen + LD_LIBRARY_PATH=/tmp/r18d-deb:build-libs:/usr/lib:vendor。

## Configure/Build 迭代（退出码均保留在 /tmp/r18d-*.log）
- 失败1 PROJ → 失败2 GSL → 失败3 FetchContent catch2（网络代理阻断）→ 失败4 netcdf → 失败5 vendor 静态库无 -fPIC（expat/zlib/sqlite3/zstd）→ 失败6 wrapper2_*/cblas *_64_（vendor GDAL LAPACK wrapper）→ 失败7/8 vendor Qt 6.10 Qml 混链 → build15 全 12 目标 EXIT=0
- 目标名勘误：test_cartography_quality/visual/platform9 编译进 test_mapspec 单目标（tests/CMakeLists.txt:878-892）

## RED 实证（修复前，2026-10-19）
| WP | 断言失败证据（file:line + 展开） |
|---|---|
| W4 APP-10 | r14D:382 !true（"Stopped editing" 仍出现）; :383 false（无 "Failed to start"）; :431/:432 lease 泄漏 |
| W2 菜单 gate | r14D:1011/:1037 editingAction("mActionScaleFeature")==nullptr（不在 gate）|
| W3 activeEditable | r14D:1079/:1083 false（活跃会话被报为无）|
| W6 inflight | r14D:1114 false（任务 running 后快照仍 idle）|
| W1a 外部编辑 | r14D:1147/:1152/:1155（门面 startEditing 后 UI 全停旧态）|
| C2 typed text | mapspec:1001（数字 text 通过验证）:1027（labels/notes/annotations 同）|
| C5 legend title | mapspec:1059（数字 title 编译"成功"）|
| C3 异构页修复 | quality:291（290.0<=210.5 不动）; :293 仍报; :300 ledger "still_reported"（不收敛实证）|
| C1 单页导出 | prod11:967 result.ok=false（pages={1} 必失败）|
| C6 atlas 表达式 | prod11:1006 layout 非空 / :1007 无错误（吞错实证）|
| C7 NaN DPI | 首跑 PASSED（QGIS 内部兜底拒了 NaN）→ 断言加强为 error 必须含 "DPI"（区分守卫与兜底）后按守卫契约 RED |

## GREEN×2（修复后）
- test_r14_D_vector_edit_honesty [r18d]: 76/76 × 2
- test_mapspec [r18d]: 23/23 × 2
- test_cartography_production_11 [r18d]: 16/16 × 2
- test_layout_tools robustness（含 NaN DPI 守卫消息断言）: 24/24 × 2

## 回归（全量）
- test_r14_D_vector_edit_honesty 全量: 228/228（16 用例）
- test_interactive_session_contract: 18/18
- test_maptool_teardown_r4: 7/7
- test_workbench_host: 50/50
- test_workbench_state_model: 29/29
- test_workbench_shutdown_policy: 54/54
- test_workbench_full_shell_lifecycle: 126/126
- test_georef_dual_window_workbench: 28/28
- test_qgis_display_manager: 6490/6490
- test_layout_tools 全量: 1304/1304
- test_mapspec 全量（含 quality/visual/templates/platform5-9/operators10）: 2830/2830（239 用例）
- test_cartography_production_11 全量: 238/238（23 过 + 5 skipped=渲染 gate，需 SICNU_CP11_RENDER=1）

### 回归中发现并修复
- C6 首版过严：无 coverage 层时 setFilenameExpression 必失败 → atlas 模板全挂（templates:212/213、visual:245、platform5:1241）。修正为 coverage 解析到时才强制契约；模板全量恢复 GREEN。

## NOT_EXECUTED
- SICNU_CP11_RENDER=1 的 5 个渲染 gate 用例（produce/atlas 渲染路径）：本机 QgsLayoutItemLegend::paint 对空层集存在已知挂起风险（skipIfHostRenderBroken 的文档化原因），未在本轮执行；C1 的直接 exportMapAtlas/exportMapLayout 契约已由非渲染路径覆盖。
- headful/手动视觉审查：无显示环境，NOT_EXECUTED（offscreen 语义等价覆盖交互态）。
- sanitizer/race 构建：构建资源已被五轨排满，本轮未跑；本轮改动全部为 UI 线程内信号/纯函数，无新增锁或线程。

## R1/R2 修复后的最终数字（2026-10-19）
- test_r14_D_vector_edit_honesty [r18d]: **188/188 ×2**（19 项菜单 gate + 菜单孪生直接断言）
- test_r14_D_vector_edit_honesty 全量: **340/340**（16 用例）
- test_cartography_production_11 [r18d]: **19/19 ×2**（含字段级 atlas 反例）; 全量 241/241
- test_mapspec [r18d]: 23/23 ×2; 全量 2830/2830
- test_layout_tools: robustness 24/24 ×2; 全量 1304/1304
- 8 个 app 回归二进制: 全 GREEN（full_shell_lifecycle 126/126、host 50/50、state_model 29/29、shutdown 54/54、interactive 18/18、maptool 7/7、georef_dw 28/28、qgis_display 6490/6490）
- Review: R1 REQUEST_CHANGES→修复（de0db471d）；R2 APPROVE_WITH_P2P3→P3 全处置（见 REVIEW_R1/R2.md）
