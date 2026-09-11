# TEST MATRIX — professional-workbench-9

环境：Release（ci-fast preset）、`QT_QPA_PLATFORM=offscreen`、ctest `-j1`
（资源纪律）。全部 PASS 以本地运行为准；"not built / not run" 必须显式标注。

## 新增测试（按 milestone — 只列实际存在的目标）

| Milestone | 测试（真实 target） | 类型 | 锁定的行为 |
|---|---|---|---|
| M0 | test_marshal_ui | unit | 死亡 widget 丢弃回调；跨线程投递到 widget 线程；null receiver no-op |
| M0 | test_scan_pool（扩展，[issue861] 用例） | unit+behavior | per-owner generation 不互相失效；精确 cancel；全局路径不受扰 |
| M0 | test_histogram_widget（扩展为 widget 级） | widget | GDALOpen 失败 marshal 回 GUI 线程并呈现；空源不启动扫描 |
| M0 | test_project_lifecycle_stress | stress | 48 轮 clear/import/视图 churn + canvas 先死形状无 crash/泄漏 |
| M1 | test_workbench_state_model | unit+integration | phase/tool/task/broken facts 投影；rules 纯函数；真实 layer-tree 选择路径 |
| M2 | test_shortcut_conflicts（扩展） | contract | 跨文件 shortcut union 无重复；tooltip 不得声称未绑定快捷键（枚举形式 binding 亦参与冲突扫描） |
| M6 | test_workbench_enum_provider | unit+contract | layers/assets/models 解析；200 截断 + 空 id 哨兵；坏源降级自由文本；applyEnumSourceAnnotations（model/asset 注解、已注解尊重、递归） |
| M7 | test_catalog_pagination | widget+scale | 窗口化渲染 ≤cap；精确页状态；越界钳制；过滤重置第 0 页；pager 按钮驱动窗口；选择跨翻页保持 |
| M8 | test_plugin_ui_placement | contract | 渲染贡献成为 registry 命令；handler 触发原 action；action 删除 → 命令 disable；release 钩子清理；reload 可重注册 |
| M8（部分） | plugin declarative render pipeline 本体 | — | 由 plugin-platform 8.0 的 conformance kit 覆盖（非本方向测试面）；本方向只测 shell 命令生命周期 |

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
