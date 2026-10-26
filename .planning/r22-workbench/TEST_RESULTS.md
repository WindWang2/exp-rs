# R22-05 TEST_RESULTS

- 分支：`agent/r22-workbench-20261025T163805`（base `911e0b17a3`）
- 构建：`build-r22-workbench`（Release；系统 Qt 6.12 + vendor GDAL 3.12/PROJ 9.7.1/GEOS 3.14.1；链接配方沿 R21-A：`--allow-shlib-undefined` + `/tmp/r21a-deb` wrapper2 stub；运行时 `/tmp/r22wb-qt` 系统 Qt shim 防 vendor Qt 6.10 被 build-rpath 拾取）
- 运行：`QT_QPA_PLATFORM=offscreen`、`PROJ_LIB=…/share/proj`、`LD_LIBRARY_PATH=/tmp/r22wb-qt:/tmp/r21a-libs:seismic:/tmp/r21a-deb`（CTestCustom 同类注入的环境在本轨显式给出）

## 新增测试（5 目标，RED→GREEN×2，最终态双跑）

| 目标 | 结果（×2 一致） |
|---|---|
| test_r22_workbench_async_runner | 24 断言 / 4 用例 全绿 |
| test_r22_workbench_view_tree | 70 / 2 全绿 |
| test_r22_workbench_temporal_pages | 30 / 1 全绿 |
| test_r22_workbench_log_tagfilter | 24 / 1 全绿 |
| test_r22_workbench_ui_contract | 34 / 5 全绿 |

RED 证据（未打补丁时）：async 2 断言红 + 父亡 SIGSEGV（UAF 实证）；view_tree 2 红 + 组默认动作 SIGSEGV；temporal 6 红；log 1 红；ui_contract 10 红。逐字输出持久化于 `RED_GREEN.md`；R2 评审员独立 revert 复现（12 断言红）。

## 邻域回归（受影响既有目标，全部重建后运行）

| 目标 | 结果 |
|---|---|
| test_async_algorithm_runner | 8 / 2 ✔ |
| test_ui_task_center_contract | 169 / 6 ✔ |
| test_raster_processing_dialog_base | 120 / 11 ✔ |
| test_gui_job_adapter | 29 / 3 ✔ |
| test_active_view_host_viewport | 12 / 2 ✔ |
| test_log_panel | 7 / 4 ✔ |
| test_batch_processing_dialog | 35 / 5 ✔ |
| test_layout_designer | 48 / 9 ✔ |
| test_workspace_browser_wiring | 39 / 4 ✔ |
| test_data_manager_panel | 101 / 16 ✔ |
| test_m2_batch_b_dialogs | 193 / 19 ✔ |
| test_m3_stress | 5241 / 7 ✔ |

关键安全/崩溃类（async_runner、view_tree、m2_batch_b_dialogs、m3_stress）双跑通过。

## KNOWN_BASELINE_FAIL / NOT_EXECUTED（诚实分类）

- **test_cartography_production_11**：本机 legend paint 挂死（R19-03/R20-04/R21-D 记录一致），且该文件在 open PR #1677 文件集内（本轨不写）。本轨未运行其渲染用例——与既往基线记录一致，非本轨回归。
- **对话框 close 生命周期的行为级测试**（构造真实 QgsProcessingAlgRunnerTask 中途 Esc）：NOT_EXECUTED——需要可运行的 processing 算法与 TaskCenter 集成夹具；以源码契约（canCloseSafely/reject/workerStarted/executor 空守卫四 token）+ R1/R2 人工推演替代。遗留项记录于 FINAL_REPORT。
- **双窗口编辑租约集成测试 / Atlas 覆盖恢复**：BLOCKED_ON_#1677（符号只在未合并分支）。
- 环境：vendor GDAL wrapper2_* 与 PCRE2 符号桩仅存在于构建/运行环境（/tmp shim），**不进入产品代码**（R21-A 同判）。

## 结论

本轨 5 个新目标与 12 个受影响既有目标全部通过，关键崩溃类用例双跑一致；无跳过断言、无 golden 修改、无容差扩大。
