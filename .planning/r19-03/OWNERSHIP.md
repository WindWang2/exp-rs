# R19-03 OWNERSHIP — 实时文件所有权（写前门）

基线 `e12a9afbc`。原则：本轨可写 = 任务书专项 03 列表 + 现场核验；其他一律只读；冲突跳过并记 CROSS_TRACK_FINDINGS。

## 本轨可写（源码）

| 路径 | 依据 | 备注 |
|---|---|---|
| `src/app/main_window*.cpp/.h`、`src/app/workbench/**`、`src/app/panels/**`、`src/app/dialogs/**`、`src/app/widgets/**`、`src/app/editing/**`、`src/app/classification/**`、`src/app/visualanalytics/**`、`src/app/shell/**`、`src/app/log_panel.*`、`src/app/stac_client.*` | 任务书 C01–C06/C09/C10/C13 | #1662 大多已修，仅动实证残留 |
| `src/agent/cartography/**`、`src/agent/mapspec/**`、`src/agent/layout_tools/**`、`src/agent/symbology/**` | 任务书 C07/C08 | produce legend 守卫（r18-d C4 遗留）在此 |
| `src/agent/commands/workspace_commands.*` | ~~扩界声明~~ **撤销**：#1571 经实读已在 master 修复（undo/redo 锁外回调 + 部分失败不入 redo 栈），本轮不改 | 见 LOOP_LEDGER 轮2 |
| `src/agent/agent_copilot_dock_widget.*`、`src/agent/view_control_service.*`、`src/agent/raster_display_service.*` | 任务书 | |
| `src/help/**` **排除** `help_content_store.cpp` | 任务书（#1664 红线） | #1639 两项在 help_presenter / terminology_provider |
| `resources/styles.qss`、`resources/styles-dark.qss` | 任务书 C09 | 本轮不动（无截图 oracle 前禁"美化"） |
| `tests/test_r14_D_vector_edit_honesty.cpp`、`tests/test_e2e_open_issues.cpp`、`tests/test_cartography_production_11.cpp`、`tests/test_m2_batch_a_dialogs.cpp`、`tests/test_m2_batch_b_dialogs.cpp`、`tests/test_classification_studio_widget.cpp`、`tests/test_spectral_profile_widget.cpp`、`tests/test_pipeline_canvas_widget.cpp`、`tests/test_georef_dual_window_workbench.cpp`、`tests/test_m2_dialog_stress.cpp` | 既有测试文件内容修改（不改注册）；#1663/#1664 文件集均不含它们（已核） | #1558/#1621/#1622/#1657 |
| `.gitignore` | r18-d 先例：append-only 本轨 planning 白名单块 | 已加 3 行 |

## 本轨只读（含红线）

- **#1663（R18-A）写集**：`src/analysis/segmentation/**`、`src/operators/rs/**`、`src/processing/algorithms/{atmospheric_correction,chunked_processor,image_fusion,mosaic}.cpp` 及其测试、`.planning/r18-a*`
- **#1664（R18-E）写集**：`src/teaching/**`、`src/teaching_admin/**`、`src/lab/**`、`src/grader/**`、`src/verification/**`、`src/faultlab/**`、`src/experiment_studio/**`、`src/cli/lab_batch_runner.*`、`src/help/help_content_store.cpp`、`data/faultlab/faults.schema.json` 及其测试、`.planning/r18-e*`；另将 `src/app/teaching/**` 主动划为只读（教学业务逻辑，任务书明令；#1664 虽零 src/app 文件）
- 轨 01（data/geospatial/runtime/experiment/operators）、轨 02（Pi/tool_catalog/workflow/sdk/science_context）逻辑
- `src/processing` 业务、`src/core`、vendored QGIS upstream、`src/gui/antigravity_*` 自动弱桩（#1638 三项 → CROSS_TRACK，仅诊断）
- 公共文件：根 `CMakeLists.txt`、`tests/CMakeLists.txt`、`docs/adr` 索引、`CLAUDE.md`（本轮均不动）

## 候选任务 → 归属核验表

| 任务 | path | upstream PR | master blob 现状 | owner |
|---|---|---|---|---|
| #1558 恒真回归 | tests/test_e2e_open_issues.cpp | 无 | :817/:871 恒真断言仍在（实读） | 本轨 |
| Edit 菜单残留 gate | src/app/main_window_menus.cpp | #1661 已并 | r18-d R2 P3-5 明示遗留 | 本轨 |
| #1571 undo/redo 锁 | src/agent/commands/workspace_commands.cpp | 无 | 实读确认锁内回调 | 本轨（扩界 D-1） |
| produce legend 守卫 | src/agent/cartography/produce.cpp | 无 | r18-d C4 明示遗留 | 本轨 |
| #1621/#1622/#1657 测试债 | tests/*（上表） | 无 | 恒真/墙钟/泄漏均在（issue 证据） | 本轨 |
| #1639 help 截断 | src/help/{help_presenter,terminology_provider}.cpp | 无 | 待实读复核 | 本轨 |
| #1632(1/2) terrain | src/app/dialogs/terrain_dialog.* | 无 | 待实读复核 | 本轨 |
| #1632(5) clampText | src/app/visualanalytics/va_selection_hub.cpp | 无 | 待实读复核 | 本轨 |
| #1632(6) erase 橡皮筋 | src/app/editing/rs_sample_erase_tool.cpp | 无 | 待实读复核 | 本轨 |
| #1560 cancel 通道 | src/app/dialogs/raster_processing_dialog_base.cpp | 无 | 空回调+自述注释（实读） | 本轨（大 WP，视预算） |
| #1565 ladderLabelsZh | src/app/teaching/guided_lab_workspace.cpp | 无 | — | **跳过**（教学只读） |
| #1619/#1620/#1638 src/gui 桩 | src/gui/antigravity_*, CMakeLists | #1662 部分处理 | 弱桩 ABI 禁改 | CROSS_TRACK |
