# R21-D TEST_RESULTS

运行环境：ctest ENVIRONMENT_MODIFICATION 等价（SicnuTestEnv.cmake）+
vendored 前缀 + `/tmp/r21a-libs`（r21a-libs 必须最前：本机 /usr/lib/libpulse.so.0 缺 pa_in_valgrind）。
入口：`.planning/r21-D/run-test.sh <binary> [catch args]`。QT_QPA_PLATFORM=offscreen。
构建：`build-r21-D`（Debug, ENABLE_TESTS=ON）；flock -j2 串行；峰值对象数 6388。

## W1b（Edit Lease 统一）— RED → GREEN×2

| 轮次 | 二进制 | 用例 | 结果 |
|---|---|---|---|
| RED（stash 生产修复，仅留测试） | test_r14_D_vector_edit_honesty [w1b] | 4 | **红** 0/4 用例，6 断言红（hasActiveEditLease=false / revision 未前进），exit≠0 |
| GREEN#1 | 同 | 4 | 38/38 断言，4/4 用例，exit 0 |
| GREEN#2 | 同 | 4 | 38/38 断言，4/4 用例，exit 0 |
| 回归（全文件） | 同 | 21 | 408/408 断言，exit 0（既有 17 用例无回归） |
| 回归#2 | 同 | 21 | 408/408 断言，exit 0 |

## R20-04 三 seam 回归（PR #1672）

| Seam | 二进制 | 结果 |
|---|---|---|
| GDAL 取消探针（#1560） | test_raster_processing_dialog_base | 120/120 断言，11/11 用例，exit 0 |
| 导出 DPI [72,1200] | test_layout_tools | 1311/1311 断言，14/14 用例，exit 0 |
| cartography raw schema registry 腿 | 随 test_mapspec / CLI 腿另行跑 | 见下 |

## CP11（#1655）— KNOWN_BASELINE_FAIL 分类闭环

- 默认（SICNU_CP11_RENDER 未设）：28 用例 25 passed / **3 failed** —— :811（produce atlas）、:1160（engine/operator/tool 字节一致 e2e）、:1296（corpus report-atlas-appendix-a4l）。与 R19-03/R20-04 记录的 :811/:1160/:1296 **逐点一致**。
- 机制（源码+门注释证据）：`tests/test_cartography_production_11.cpp:147-166` 记载 QgsLayoutItemLegend::paint 在部分主机挂起/失败（差分证据 .planning/cartography-production-11/EVIDENCE.md：健康并行主机同夜全绿）。本机 LD_LIBRARY_PATH 补丁环境下的离屏渲染为该主机差异的又一实例。
- 反证（`SICNU_CP11_RENDER=0`）：23 passed + 5 skipped、**0 failed** → 三红全部由主机渲染路径引起，非产品缺陷、非本轨 diff 引入。
- 处置：按 O-G5 如实归类；不修改 golden 快照、不跳过门。issue #1655 的关闭建议：环境门语义按设计工作（渲染用例在健康主机运行，本机可选择 opt-out），建议维护者在带显示环境复核后决定是否关闭或保持文档化。

## cartography raw schema 三腿

- registry 腿（R20-04 报告 17695→17868）：随 test_mapspec 全量套件见 M4 批次；cartography 改动仅 W1b 无关，schema 快照未动。

## FIXED-OPEN 复测族（2026-10-24，全部 GREEN）

| Issue | 二进制 | 结果 |
|---|---|---|
| #1535 算法 runner feedback UAF | test_async_algorithm_runner + test_raster_processing_dialog_base | 8/8 断言；120/120 断言 |
| #1536 guided workflow 行号 | test_guided_workflow_widget | 124/124 断言，11/11 用例 |
| #1537 STAC searchNext 代数 | test_stac_client | 28/28 断言，3/3 用例 |
| #1559 workspaceService seam | test_agent_workbench_context（+agent 套件见下） | 邻域绿 |
| #1561 光谱剖面离线程采样 | test_spectral_profile_widget | 154/154 断言，16/16 用例 |
| #1562 log_panel Level 过滤 | test_log_panel | 7/7 断言，4/4 用例 |
| #1563 CSV 转义 | test_accuracy_panel_wiring | 8/8 断言 |
| #1564 VaCursorProbe 节流 | test_view_link（含 VA 面板腿） | 1308/1308 断言（需 PROJ_DATA） |

## W1b 邻域回归（全绿）

test_qgis_display_manager 6490/6490 · test_command_registry 57/57 · test_selection_context 72/72 · test_workbench_full_shell_lifecycle 126/126 · test_workbench_host 50/50 · test_workbench_shutdown_policy 54/54 · test_mapspec 2830/239（raw schema registry 腿）· test_view_link 1308/1308（PROJ_DATA 补齐后）。

## 环境伪红记录（O-G5）

- test_view_link 3 断言红（cross-CRS 变换）：根因 PROJ_DATA 未设置（vendored proj.db 在 prefix/share/proj）。补 PROJ_DATA 后 1308/1308 全绿——环境问题，非产品缺陷。runner 脚本已固化。
