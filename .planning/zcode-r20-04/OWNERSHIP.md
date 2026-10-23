# ZCode R20-04 OWNERSHIP

## 允许写区（来自任务书 Track 04 清单）

`src/app/**`（排除 teaching*/experiment_studio）、`src/ui/**`、`resources/**`、`src/agent/cartography/**`、`src/agent/mapspec/**`、`src/agent/layout_tools/**`、`src/agent/symbology/**`、`src/agent/agent_copilot_dock_widget.*`、`src/agent/view_control_service.*`、`src/agent/raster_display_service.*`、`src/help/**`（排除 help_content_store.cpp）、对应 tests、`.planning/zcode-r20-04/**`。

## 本轨实际改动（随提交更新）

| Commit | 文件 | Owner 依据 |
|---|---|---|
| d5b95a9546 | tests/test_raster_processing_dialog_base.cpp | 对应 Qt/workbench tests ✓ |
| d5b95a9546 | tests/test_layout_tools.cpp | 对应 layout tests ✓ |
| f9bbe9a5f6 | src/app/dialogs/raster_processing_dialog_base.{h,cpp} | src/app/** ✓ |
| 7e147f0361 | src/agent/cartography/cartography_tools.cpp | src/agent/cartography/** ✓ |
| b7a04d7465 | src/agent/layout_tools/layout_service.cpp | src/agent/layout_tools/** ✓ |
| edc7c27e17 (R1) | src/app/dialogs/raster_processing_dialog_base.{h,cpp}、src/app/layout/qgslayoutdesignerdialog.cpp、src/agent/layout_tools/layout_tools.cpp（仅注释） | src/app/**、src/agent/layout_tools/** ✓ |
| R2 修复 | 同上两测试文件 + raster_processing_dialog_base.h 文档 | ✓ |

全部落在声明写区内；`git diff --name-only origin/master...HEAD` 持续核对，只读区零改动（R2 reviewer 独立复核确认）。

## 只读遵守（LIVE 复核）

- `src/core/**`、`src/gui/**`、`src/data/**`、`src/runtime/**`、`src/operators/**`、`src/analysis/**`、`src/agent/mcp_server.*`、`src/teaching/**`、`src/verification/**`、`pi/**` — 本轮零改动（`git diff --name-only origin/master...HEAD` 持续核对）。
- `src/processing/framework/**`（GuiJobHandle/TaskCenter/JobEngine 属 shell/processing）— 只读：#1560 修复在 dialog 基类 seam 内完成（task 签名 + 探针绑定 ctx），不改 JobEngine。
- 共享区（根 CMakeLists、tests/CMakeLists.txt 等）— 本轮不需要触碰（新测试加入已注册测试文件的既有 TEST_CASE）。
