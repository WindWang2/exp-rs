# R30-T06 · 路径所有权与边界

## 本轨主写（T06 独占，按任务表）

- `src/app/workbench/**`：SelectionContext、CommandRegistry/Palette、Inspector、history、通用面板
- `src/app/panels/data_manager_panel.*`、`asset_catalog_index.*`
- `src/app/preview/**`、`src/app/active_view_host.*`、`src/app/layer_tree_menu.*`
- `src/app/display/**`、`src/app/cartography/**`
- `src/app/shell/secondary_map_view_*`、`view_link_controller.*`、`rs_dual_viewport_sync_controller.*`
- 通用 `src/app/shell/schema_form_builder.*`
- `src/agent/cartography/**`（整文件例外除外）、`src/agent/mapspec/**`、`src/agent/symbology/**`、
  `src/agent/layout_tools/layout_service.*`
- 最小共享接线：`main_window*.cpp/.h`、app/tests CMake 仅加 `R30-T06` 小块

## 本轮实际触碰（预期）

| 文件 | 用途 | 所有权 |
|---|---|---|
| `src/app/layer_tree_menu.cpp/.h` | F1 组缩放 live canvas / 生命周期 / 诚实拒绝 | T06 ✅ |
| `src/app/workbench/processing_history_panel.cpp/.h` | F2 统一 action plan、双产物比较、重试反馈 | T06 ✅ |
| `tests/test_r30_t06_*.cpp`（新建） | 本轨新增回归 | T06 ✅ |
| `tests/CMakeLists.txt` | 仅追加 `R30-T06` 小块（不重命名/重排他人 target） | 共享热点，最小改动 |
| `.planning/r30-t06-workbench-cartography/*` | 本轨账本与验证摘要 | T06 ✅ |

## 明确排除（不写）

- `src/app/workbench/dataset_experiment_panel.*`、Studio / experiment / study / teaching → **T07**
- `src/app/agent_ops/**`、Workbench `mission_*` / `agent_context_tool.*` → **T05**
- `src/processing/framework/task_center.*`、JobEngine、`src/app/shell/workflow_session_controller.*` → **T04**
- `src/agent/output_verifier.*`、`src/agent/cartography/cartography_operators.cpp`、
  `tests/test_cartography_production_11.cpp` → **T04**（整文件例外）
- `src/data/**`、`src/dataset/**`、`src/geospatial/**`、`operators/io/**` → **T03**
- `src/operators/runtime/**`、model catalog/readiness → **T02**
- `src/analysis/**`、`experiment/evaluation.*` → **T01**
- vendored `src/core/**`、`src/gui/**`：本轮 **不改**。F1 的修复在第一方
  `layer_tree_menu.cpp` 完成；QGIS `qgslayertreeviewdefaultactions.cpp` 仅作为缺陷证据引用，
  不做全域替换所有 canvas 调用。
- `tests/test_r29_t04_history_actions.cpp`（旧 R29 综合测试，T06 拥有但本轮**不改**）：
  新增覆盖一律写入 `tests/test_r30_t06_*.cpp`，避免并行轨改同一旧文件。
- 根主题、生成快照、fixtures 全量覆盖：不做。

## 接口冻结

- `ProcessingHistoryPanel::compareRequested(const QString&, const QString&)` 信号签名保持，
  shell `main_window_workbench.cpp:525-528` 的 `openComparisonForPaths` 不变。
- `outputChoicesFor` 已 public static，返回类型 `QVector<OutputArtifactChoice>` 不变，
  新增字段只做**可选追加**，旧消费者（含 `test_r29_t04_history_actions.cpp`）可构建。
- `LayerTreeMenuProvider::createContextMenu()` 签名与返回 `QMenu*` 不变。
- 不要求其他六轨同时合入才能编译；本轨变更独立可构建。
