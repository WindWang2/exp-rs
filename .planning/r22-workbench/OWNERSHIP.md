# R22-05 OWNERSHIP（本轨写入白名单）

允许（任务书）：`src/app/**`（排除 `src/app/teaching_admin/**`）、`src/gui/**`、`src/ui/**`、`src/agent/cartography/**`、`src/agent/mapspec/**`、`src/agent/layout_tools/**`、`docs/cartography/**`、`docs/USER_GUIDE.md` 工作台章节、`tests/test_r22_workbench_*`、`tests/CMakeLists.txt`（append-only 小块）。

本轨实际写入计划（逐工作包更新）：

| 工作包 | 文件 | 冲突核对 |
|---|---|---|
| W1 | src/app/dialogs/async_runner_base.{h,cpp}、tests/test_r22_workbench_async_runner.cpp | #1677 文件集无交集 ✔ |
| W2 | src/app/dialogs/temporal_analysis_dialog.cpp（algorithmChanged 页映射）、tests/test_r22_workbench_temporal_pages.cpp | 无 open PR 占用 ✔ |
| W3 | src/app/active_view_host.cpp（zoomToNativeResolution 重投影） | 无占用 ✔（active_view_host 非 #1677 文件） |
| W4 | src/app/active_view_host.{h,cpp}（removeLayers 重载）、src/app/layer_tree_menu.cpp（组删除走 host）、tests/test_r22_workbench_view_tree.cpp | 无占用 ✔ |
| W5 | src/app/log_panel.cpp（typed tag 过滤）、tests/test_r22_workbench_log_tagfilter.cpp | 无占用 ✔ |
| W6 | src/app/panels/workspace_browser_panel.cpp（ImportCenter deleteLater） | 无占用 ✔ |
| W7/W8 | src/app/cartography/cartography_dock.cpp（DPI:）、src/app/layout/qgslayoutdesignerdialog.cpp（item(s)×4）、src/app/active_view_host.cpp（1 layers 单复数） | 无占用 ✔ |
| W9 | src/app/shell/gui_job_adapter.{h,cpp}（jobCancelInFlight 共享）、src/app/dialogs/sicnu_algorithm_dialog.cpp（closeEvent 逃逸）、src/app/dialogs/raster_processing_dialog_base.cpp（删本地副本） | 无占用 ✔ |
| extra | src/app/map_tools/map_tool_manager.cpp（计数）、src/app/dialogs/batch_processing_dialog.cpp（外部取消停止链）、tests/test_r22_workbench_ui_contract.cpp | 无占用 ✔ |
| 注册 | tests/CMakeLists.txt（append-only 块） | r22-inference 也 append 于 EOF——合并时为平凡文本冲突，双方保留即可 |
