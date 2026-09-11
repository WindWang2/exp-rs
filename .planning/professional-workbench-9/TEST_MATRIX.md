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

（每个 milestone 完成时在此追加：命令、目标、结果、时长、并行度）
