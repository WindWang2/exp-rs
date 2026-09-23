# 现状矩阵 — App / Workbench / UI Shell（master a9dc33fa7）

组件 × 权威数据源 × 调用者 × 错误模型 × 资源上界 × 现有测试 × 已知历史修复 × 剩余疑点。

## 1. Shell 生命周期（main_window*）

| 组件 | 权威数据源 | 调用者 | 错误模型 | 资源上界 | 现有测试 | 历史修复 | 剩余疑点 |
|---|---|---|---|---|---|---|---|
| ctor 装配序 (`main_window.cpp:101-330`) | 单一构造序：setupUi→canvas→commandRegistry→menu→docks→ProjectContext→panels→connections→restore→plugin | main.cpp:388 | 无异常路径（QGIS 无异常） | dock 数固定 | 无（窗口无 offscreen fixture） | 2fec5d1f7、8b692bc8b | 无完整 shell smoke fixture（明确后续方向） |
| `newProject()` (`main_window_project.cpp`) | QgsProject + ProjectContext + mission runtime | menu/ribbon/command | confirm dialog + typed warning | — | 无 | #1083 probe（openProject 侧） | **B1 已修**：mission 未随 session 清空 → 跨项目泄漏 |
| `openProject()` (同上) | probe-read 先行（#1083） | 同上 | fail-closed probe + typed warning | — | 无 | 5e9192052 | **B1 已修**（失败分支同样清 mission） |
| `saveProjectAs()` (同上) | QgsProject::write 单写 | 同上 | 失败回滚 fileName (#1097) | — | 无 | #1097 | **B3 已修**：成功后未重挂 sidecar watcher |
| mission save 决策 (`main_window_connections.cpp onProjectWrite`) | sidecar + XML 单权威 | QgsProject::writeProject signal | corrupt→refuse、reload-fail→refuse (#1148/#1149) | 单文件 | 无（store 层本轮补 harness） | #1200 | store harness 本轮落地 |
| dock 注册 (`main_window_docks.cpp`) | ctor 一次性创建 | window menu toggles | — | 固定 dock 集 | 无 | fc736af60 | contested（#1237/#1239 同函数），本轮不动 |
| layout save/restore (`main_window_misc.cpp savePanelState/restorePanelState`) | QSettings `mainwindow/state` v11 | ctor/closeEvent/resetPanelLayout | restoreState 返回值被忽略（B12，P3） | QByteArray | 无 | — | B12 记录在案 |
| plugin reload (`main_window.cpp registerPluginCommands/unregister`) | CommandRegistry 单权威 | PluginRegistry | duplicate-rejection + qWarning | generation 递增 | 无 | #1031、f3ec3f021 | **B5 已修**：unregister 不释放 shortcut 保留，reload 契约对带快捷键命令失效 |

## 2. Workbench selection / scientific inspector

| 组件 | 权威数据源 | 调用者 | 错误模型 | 资源上界 | 现有测试 | 历史修复 | 剩余疑点 |
|---|---|---|---|---|---|---|---|
| `SelectionContext` (`workbench/selection_context.*`) | canvas+layer tree+panel push（无业务拷贝） | command availability/palette/help | #778 dying-layer tombstone、#849 O(1) live-set | debounce 150ms、dying list 有界 | test_selection_context.cpp | #778/#849/#893 | attach 后 layer-tree model 重换（未观察到场景）— 疑点仅记录 |
| `InspectorHost` (`workbench/inspector_host.*`) | sci_inspection 投影 | main window docks | rebuild 守卫 | tab 有界 | test_inspector_host.cpp、test_sci_inspector.cpp | dba4c5bff、RS14-19 (#1205) | 本轮未发现新实证缺陷 |
| `sci_inspection` (`workbench/scientific/*`) | SciFact 纯函数层 | inspector host | typed unknown/unavailable | 缓存随 snapshot | test_sci_inspector.cpp | #1205 | 同上 |

## 3. GuidedWorkflow（既有 UI，非 #1237 teaching 域）

| 组件 | 权威数据源 | 调用者 | 错误模型 | 资源上界 | 现有测试 | 历史修复 | 剩余疑点 |
|---|---|---|---|---|---|---|---|
| `GuidedWorkflowWidget`（widgets/, 实验步进器） | `data/labs/*.lab.json`（loadLabSpecsFromDir，typed error entry，ADR 0146） | guided workflow dock | 错误条目置顶可见；提交拒绝可见 | 单在途 job（GuiJobHandle busy-gate） | 仅数据层测试 → **本轮补 widget 级状态机测试** | ADR 0146、#453/#696/#704（adapter） | **GW1 已修**：跨实验选择污染 + 步索引越界 |
| `GuiJobHandle` (`shell/gui_job_adapter.*`) | TaskCenter::taskUpdated 单流 | 各 GUI 面板 | catch-up (#453)、cancel 语义 (#696) | 每 handle 1 job | test_gui_job_adapter.cpp | #453/#696/#704 | — |
| `GuidedWorkflowWidget`（pipeline/, D17 dual-view） | WorkflowDocument + labSteps metadata | pipeline dock | fail-closed（cyclic 拒绝） | cards 随节点数 | test_guided_workflow_sync.cpp | #1097 export-before-reload | 与 widgets/ 同名类（既有命名，不属本任务改动面） |

## 4. Mission runtime（13.0）

| 组件 | 权威数据源 | 调用者 | 错误模型 | 现有测试 | 历史修复 | 剩余疑点 |
|---|---|---|---|---|---|---|
| `mission_runtime_store` | sidecar `.mission.json` + project XML 双通道单权威，last-good 旋转 | window onProjectRead/Write、refreshMissionRuntime、agent mission:* tools | poisoned fail-closed、refuse-on-unreadable | **无 → 本轮补 test_mission_runtime_store** | #1148/#1149/#1169/#1228-34 | store 契约固化后，窗口层 B1 修复依赖其 fresh-mint 行为 |
| `MissionTimelinePanel` | timeline 单源 | window | — | test_mission_surface_parity 等 | #1213 | — |

## 5. 资源/生命周期疑点（本轮只记录，不下结论）

- B2 secondary view 重开后 `m_dualViewportSync` 不重建（P2，`main_window_view.cpp`，UNCONTESTED — 下一 slice 候选）。
- B4 layout designer 跨 project clear 悬挂（P2，需要 designer 追踪）。
- B6/B7 在 `setupWorkbenchInfrastructure()` 内 = contested 同函数，本轮不动。
- B8 `PluginUiHost::setShellSink` 析构不清（P3 一行，teardown 顺序）。
- B9 LayerTreeMenuProvider 泄漏（P3）。
- B11 auto-load sample timer 可在 open-project 模态循环内触发（P3）。
- B13 layerWasAdded 名字观察重复 connect（P3）。
- B14 "Project saved" toast 与 mission 拒绝提示矛盾（P3）。
