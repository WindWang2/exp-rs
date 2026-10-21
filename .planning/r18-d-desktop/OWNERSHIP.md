# R18-D OWNERSHIP

## 独占写入区（本轨）

- `src/app/workbench/**`（workbench_host、interactive_session、command_registry、selection_context、session_adapters、workbench_state、shutdown_policy、project_session_boundary 等）
- `src/app/main_window*.cpp/.h`（workbench/vector/connections/menus/processing/misc）
- `src/app/map_tools/**`、`src/app/editing/**`、`src/app/display/**`、`src/app/shell/**`
- `src/app/qgis_app_facade.cpp/.h`、`src/app/qgsattributetabledialog.*`（如需，仅限编辑状态同步）
- `src/agent/cartography/**`、`src/agent/mapspec/**`、`src/agent/layout_tools/**`、`src/agent/symbology/**`（如存在）
- `resources/styles.qss`、`resources/styles-dark.qss`
- 本轨已注册测试文件：test_interactive_session_contract、test_maptool_teardown_r4、test_cartography_*、test_mapspec、test_workbench_*、test_r14_D_vector_edit_honesty、test_georef_dual_window*、test_remote_map_display、test_qgis_display_manager、test_platform7/9、test_edit_*、test_layout_tools

## 只读/禁区

- `pi/**`、MCP/Agent harness、`src/workflow/**`（R18-C）
- 模型运行器（R18-B）、科学算法 `src/analysis`/`src/operators`（R18-A/r10）、教学/验证纯业务层（E）
- `src/core/**`、`src/processing/**`（#1467 cluster，他人领域）
- QGIS vendored 源 `src/core/layout/*`（vendored QGIS）不直接改；通过本轨层修复
- `src/app/teaching_admin/**`、`src/app/experiment_studio/**`：E 的视图模型契约只读
- `tests/CMakeLists.txt`、根 `CMakeLists.txt`、`src/agent/CMakeLists.txt`：默认只读，非必要不动
- `.goal-loop-ledger.md`（仓库根）：不写

## 冲突监测

- 开放 PR #1534（r10 算法）写集与本轨零交集（实测）。
- 每次 rebase/新基线变化后重跑 `overlap_scan.py`；碰 #1534 写集即停。
- 兄弟 worktree：r18-a/r18-b/r18-c 在各自目录，不触碰。
