# TEST MATRIX — professional-workbench-9

环境：Release（ci-fast preset）、`QT_QPA_PLATFORM=offscreen`、ctest `-j1`
（资源纪律）。全部 PASS 以本地运行为准；"not built / not run" 必须显式标注。

## 新增测试（按 milestone）

| Milestone | 测试 | 类型 | 锁定的行为 |
|---|---|---|---|
| M0 | test_scan_pool 补 per-owner 用例 | unit | 不同 owner generation 不互相失效 |
| M0 | test_workbench_project_stress | stress | project clear→import→clear→exit 循环无 crash/leak（sanitizer 可选档） |
| M0 | ui_callback helper 单测 | unit | 死亡 widget 丢弃回调；跨线程投递到 widget 线程 |
| M1 | test_workbench_state_model | unit | project phase/task phase/view/tool 状态转移与 facts 投影 |
| M2 | test_command_surface_consistency | contract | registry shortcut 唯一；菜单/工具栏 canonical shortcut 全部 registry-owned；文案 Ctrl+X ↔ registry 绑定一致 |
| M2 | workflow command 注册用例 | unit | workflow.* 进 registry；pipeline dock tooltip 与实际绑定一致 |
| M3 | test_qgis_display_manager 补 destroy-order 用例 | integration | view remove → bridge delete → canvas 销毁顺序安全 |
| M4 | test_layer_tree_consistency 补 broken-layer 用例 | integration | broken layer 的 facts/UI 状态一致 |
| M6 | test_workbench_enum_provider | unit+contract | 每种 enum source 解析；上限截断；坏源降级自由文本；value preservation across refresh |
| M7 | test_asset_catalog_index 补 200k 分页用例 | scale | 分页读取有界；selection 保持；无 O(N²) |
| M8 | test_plugin_ui_placement | contract | declarative placement→host wrapper；unload 摘除；crash 后 disable |
| M9 | 既有 theme parity / a11y 测试扩充 | unit | 新面板 empty/error 态可访问名 |

## 回归保持矩阵（必须全绿，8.0 继承）

test_selection_context · test_scan_pool · test_qgis_display_manager ·
test_command_registry · test_command_palette · test_workbench_host ·
test_workbench_shutdown_policy · test_schema_form_4 · test_schema_form_builder_v2 ·
test_asset_preview_service · test_asset_catalog_index · test_context_facts_8 ·
test_data_manager_panel · test_rs_empty_state_widget · test_adversarial_m1…m6 ·
test_ui_task_center_contract · test_theme_selector_parity ·
test_interactive_session_contract · test_active_view_host_viewport ·
test_processing_history_model · test_inspector_host · test_temporal_scene_model ·
test_workflow_session_controller · test_workflow_pipeline_ui。

## 执行记录

### 2026-09-12 — 完整矩阵（Release, ci-fast, offscreen, 串行单进程）

构建：`cmake --preset ci-fast` + `cmake --build build-ci-fast -j4`（Ninja/Make 均可，
本机 Arch, GCC 16.2.1, GDAL 3.13.3-2, Qt 6, QGIS 内嵌 fork）。

新增套件（全部 PASS）：
- test_marshal_ui 3/3 (4 asserts)
- test_scan_pool 8/8 (22; 含 #861 per-owner 回归 4 例)
- test_histogram_widget 5/5 (25; 含 GDALOpen 失败 marshal)
- test_workbench_state_model 6/6 (27; 含 SelectionContext broken-layer 集成)
- test_workbench_enum_provider 4/4 (21; 含 DataManager 真资产 + 200 截断)
- test_shortcut_conflicts 4/4 (28; 跨文件 union + 谎言 tooltip gate)
- test_catalog_pagination 3/3 (110; 窗口/翻页/钳制/选择保持)
- test_project_lifecycle_stress 2/2 (547; 48 轮 clear/import/视图churn/先关窗)

回归保持（全部 PASS，28/28 套件全绿）：
test_selection_context 13/13 · test_command_registry 9/9 · test_command_palette 4/4 ·
test_workbench_host 8/8 · test_workbench_shutdown_policy 4/4 · test_schema_form_4 12/12 ·
test_asset_preview_service 10/10 (1411) · test_asset_catalog_index 8/8 (317) ·
test_context_facts_8 3/3 · test_data_manager_panel 16/16 · test_rs_empty_state_widget 5/5 ·
test_ui_task_center_contract 6/6 (174) · test_theme_selector_parity 2/2 ·
test_processing_history_model 5/5 · test_workflow_session_controller 4/4 ·
test_workflow_pipeline_ui 16/16 · test_adversarial_m4 5/5 (558) · test_adversarial_m5 4/4 ·
test_active_view_host_viewport 2/2 · test_qgis_display_manager 21/21 (343)。

not run / not built：无（矩阵内全部目标已构建并执行；app 目标 sicnu_geo_rs 链接通过）。
线上 CI：未等待（按 CI policy，本地证据为准）。
