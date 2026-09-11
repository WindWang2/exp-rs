# FINAL REPORT — Professional Remote Sensing Workbench 9.0

Branch `feat/professional-workbench-9` · base `origin/master` @ `f316dfdbb4` ·
worktree `../exp-rs-professional-workbench-9`。

> **声明**：线上 CI/CD 未作为完成条件，未等待 CI；完成依据为本地可复现证据
> （Release 构建 + offscreen 测试矩阵 28/28 套件全绿 + stress/scale 度量，
> 全部记录于 TEST_MATRIX.md / PERFORMANCE.md）。

## 0. Baseline Gate（强制，先复验后开发）

在最新 `origin/master`（f316dfdbb4，含 8.0 全系列 + #848-#882 修复）上重做：
open PR #883-#886（四个并行 9.0 方向，均不触 `src/app/**`）；open issues 为 0；
五个历史风险 issue 逐项复验（ISSUE_TRIAGE.md）：#849/#857/#859/#861 主体
fixed-by-later-merge（逐项给出代码证据），#882 **still-valid 残留** → 本方向修复。
新发现 F1（19 处 shortcut 双权威）与 F4（requestEpoch 数据竞争）为 baseline 审计产出。

## 1. 交付内容（按 milestone）

### M0 — UI Safety Burn-down（commit c2951a4570 + 7315e0adf1）
- `marshal_ui.h`：worker→UI 完成投递的唯一评审版封装（投递到 receiver，
  widget 死亡由 Qt 自动丢弃）。
- histogram 扫描失败不再静默：CPL 错误 marshal 回 GUI 线程并在空状态呈现
  + `lastError()`（#882 审计 F3）。
- `RoiStatisticsWidget::m_requestEpoch` → `std::atomic`（worker 线程读取，
  GUI 线程写入的数据竞争，F4）。
- test_scan_pool 新增 #861 per-owner 回归（不同 owner 永不互相失效、精确
  cancel、全局路径不受扰、确定性信号量 rendezvous 行为用例）。
- test_project_lifecycle_stress：48 轮 clear→import→视图 churn，含
  「canvas 先于 clearProject 销毁」形状（#859/#857 族）——547 断言全绿。

### M1 — Workbench State Model（commit 170b08e88b）
- `WorkbenchStateModel`：phase（Empty/Populated）、toolMode、taskInFlight、
  hasBrokenLayer、layerCount 的单一聚合点 + 合并广播；**投影聚合器而非第二
  权威**（每个 fact 保持唯一上游）。
- `WorkbenchRules` 纯函数：canvasStackPage / importIsPrimaryAction /
  phaseSummary；updateCanvasEmptyState/updateLayersEmptyState 从 5 处手写
  hasLayers 探测收敛到单信号路径。
- 测试走真实选择路径（layer tree selection model → SelectionContext → model）。

### M2 — Command & Shortcut Authority（commit 63d69a60ee + 7315e0adf1）
- F1 修复：19 个 registry 已声明命令的菜单项从裸 addAction 迁移为
  `registry->action(id, installShortcut=true)` 投影——单一 handler、单一
  可用性契约、唯一 shortcut 持有者（原状：菜单与 registry 同时登记
  New/Open/Save/Quit/ZoomIn/Out/Ctrl+E/Ctrl+Shift+F/Ctrl+L/H/Ctrl+Shift+I/D/A/C/S/F5）。
- F2 修复（#882 残留）：pipeline dock tooltip 不再声称不存在的 Ctrl+N/O/S；
  workflow.new/open/save/run/stop 成为真实 registry 命令（默认无快捷键，
  Ctrl+N/O/S 归 project.*）。
- 两个机械 gate：跨文件 shortcut union 无重复（原 per-file 扫描看不见
  menus-vs-registry）；tooltip 不得声称未被绑定的快捷键（同句/邻句/
  文件内 setShortcut 令牌匹配）。

### M6 — SchemaForm Host 5.0（commit 82c02a80a0）
- 生产 `WorkbenchEnumProvider`：`layers:raster|vector`（画布真值）、
  `assets`（DataManager 快照）、`models`（ModelCatalog）；每源 200 上限 +
  真实截断标注；未知源 → 空 → 8.0 契约的自由文本降级。
- TaskPanelHost 安装 provider；图层推送同时喂 provider 与 push 通道（双通道
  永不分歧）；窗口装配点注入 DataManager。
- 生产者侧：`workflow_session_controller` 在 shell 边界为 x-ui-type
  model/asset 端口补 `x-ui-enum-source` 注解（算子代码零改动）；
  `refreshEnumSources` 使 model/asset combo 活解析。
- 8.0 遗留的「无人调用 setModelChoices/setAssetChoices → 空组合框」缺口就此关闭。

### M7 — Large Data UX（commit ea2db2d777）
- DataManagerPanel 真分页：页大小 = standalone row cap；pager 命名精确切片
  与总数；单页目录渲染与 8.0 完全一致（pager 隐藏）；新过滤重置到第 0 页；
  越界钳制；**选择身份跨翻页保持**（自审发现翻页丢选择的缺口并修复）。
- 200k 逻辑记录保持有界可浏览（完成定义条款）：test_catalog_pagination
  3/3 + test_asset_catalog_index 200k scale 全绿。

### M8 — Plugin Declarative UI Placement（commit 20be27a4c4）
- 8.0 明确留给 workbench 的 seam 补全：shell 安装 `PluginUiSchemaRenderer`
  的 sink；对每个加载的 host-process 插件 describe → 渲染 → 经同一
  反向所有权 sink 附加（dock/menu/settings）；E6008（无 UI）为正常答案。
- 生产 `PluginUiInvokeDelegate` → 新增 `PluginRuntimeHost::invokePluginUi`
  透传（有界 ui.invoke，超时参数）。
- `registerPluginCommands`：每个渲染的菜单贡献成为 registry 命令
  `plugin.<pluginId>.<n>`，availability 跟随渲染 action——卸载/崩溃自动
  disable，不留死菜单项。

### M9 — UX Quality / Accessibility（增量，融入各 milestone）
- histogram 失败态同时设置 accessibleName（屏幕阅读器可达）。
- pager 标签带 accessibleName；新面板均提供 empty-state（沿用 RsEmptyStateWidget
  契约）；theme parity 套件全绿（新组件使用默认 palette，未引入硬编码主题色，
  除错误文本既定的调色板红）。

### M3/M4 — 生命周期与图层交互（验证收尾 + 集成）
- #857/#859 修复在新 stress 下复验（48 轮、canvas 先死形状、多视图清除）。
- test_qgis_display_manager 21/21、test_active_view_host_viewport 2/2、
  test_selection_context 13/13 保持全绿；broken-layer fact 经 M1 模型投影
  获得集成测试。

### M5 — Professional RS Workspace（经集成交付）
- 说明：M5 的「任务为中心的工作区」没有以孤立对话框堆叠方式实现——
  其可达性由 M1（状态驱动的空态/建议）、M2（palette 可见的 workflow.* 命令）、
  M6（工具表单的动态模型/资产/图层选项）与既有 TaskCenter/结果溯源链共同
  交付；证据见上述套件 + test_ui_task_center_contract / test_workflow_*。
- 更深的 workspace 编排（跨步骤模板推荐等）记为 follow-up，不在本方向
  伪造交付。

## 2. 环境驱动的 master 修复（如实声明）

本机 Arch 在本方向进行中升级了 GDAL 3.13.3-2 与用户态工具链，暴露两处
**master 现状在本机无法编译**（与 本方向改动无关）：
1. `src/geospatial/metadata/canonical_metadata.cpp` — GDALMDArrayRead 的
   `count` 参数在 3.13.3 变为 `const size_t*`。一行 seam 修复
   （a6b349d69d），与同块既有 size_t 拼写一致。
2. `src/experiment/run_bridge.cpp` — `const auto run = runById(...)`
   经 `operator->` 调用非 const setter（GCC 16 拒绝）。去掉 const 并注明。
两处均为最小修复、独立 commit、PR 中单独列出，便于所属 track 复核。

## 3. 真实测试证据

见 TEST_MATRIX.md 执行记录：28 个套件全部 PASS（含 8 个新增套件、
20 个 8.0/7.0 回归套件），Release/offscreen/串行。sicnu_geo_rs 应用目标
链接通过。未运行项：无（矩阵内全部构建并执行）；线上 CI 未等待。

## 4. Adversarial review

主 Agent 自审两轮（REVIEW_LOG.md Round 0/1）+ 2 个只读 subagent（A：
architecture/correctness/concurrency/security；B：tests/performance/
portability/docs-vs-code）。发现与处置逐条见 REVIEW_LOG.md Final 节；
P0/P1 全部修复，P2 原则上全部修复，P3 修复或逐条记录接受理由。

## 5. Known limitations / follow-ups（诚实清单）

- QPointer 自 worker 线程 `.data()` 读取与 GUI 线程析构在理论上存在数据
  竞争（仓库既有一贯模式；对齐字长读取在支持的架构上无实际撕裂）。彻底
  方案（weak_ptr 握手）涉及全部 scan 用户，记 follow-up，未在本轮大翻改。
- `plugin.<pluginId>.<n>` 命令 id 中的索引在 reload 后可能漂移；命令
  生命周期由 availability 守卫保证正确性，稳定 id 需要上游 schema 增加
  contribution id 字段。
- 分页翻页触发整树 coalesced 重建（非增量插入）；200k 索引下的上界由
  8.0 的 coalesced timer 保证（PERFORMANCE.md），增量行插入记 follow-up。
- M5 深度编排与 M9 全量主题/a11y 审计超出本方向完成定义，follow-up。
- 一处对 RoiStatisticsWidget 表格导出路径的 #861 关联改进（导出按钮在
  computing 时的禁用一致性）未纳入本轮。

## 6. Compatibility / migration

- 无 schema/协议破坏性变更；CommandRegistry 注册表只增（workflow.*、
  plugin.*）；菜单视觉与快捷键行为不变（快捷键从菜单裸 action 平移到
  registry 投影，绑定值相同）。
- `x-ui-enum-source` 生产者注解只影响 host 渲染层；算子 schema 未变。
- GDAL/GCC 修复对行为无影响（类型拼写与 const 正确性）。
