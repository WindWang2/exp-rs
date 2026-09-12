# GOAL — Professional QGIS Remote-Sensing Workbench 9.0

Repo `WindWang2/exp-rs` · branch `feat/professional-workbench-9` ·
worktree `../exp-rs-professional-workbench-9` · base `origin/master` @ `f316dfdbb4`.

把 Workbench 8.0 的 SchemaForm、preview、large catalog、context facts 推进成
**稳定、专业、状态一致的 QGIS 遥感桌面工作台**。优先解决 UI 生命周期、异步回调、
选择上下文、工具可用性和大数据交互问题。

## 方向不变量（§7）

- background thread 不直接写 QWidget/QObject UI state；
- destroyed widget 不接收旧 callback；
- generation 必须 per-owner；
- QGIS canvas/bridge/render job 生命周期显式；
- CommandRegistry 是 command id 权威；
- DataManager/QGIS project/layer tree 不产生三个互相漂移的"真相"。

## 完成定义（§8）

- #849/#857/#859/#861/#882 重新复验并处理；
- project clear/switch/exit stress 不 crash/hang/leak；
- command/shortcut/empty-state 一致；
- 200k+ logical catalog UI 仍有界；
- plugin UI、schema form、task state 与 QGIS interaction 可集成。

## Milestones

M0 UI Safety Burn-down · M1 Workbench State Model · M2 Command & Shortcut
Authority · M3 QGIS Canvas/View Lifecycle · M4 Layer & Data Interaction ·
M5 Professional RS Workspace · M6 SchemaForm Host 5.0 · M7 Large Data UX ·
M8 Plugin Declarative UI Placement · M9 UX Quality/Accessibility/Themes。

执行模式：全自动；最多 2 个只读 subagent 做 adversarial review；CI 不作为完成条件，
一切以本地可复现证据为准；构建并行度 ≤4（默认 2）。
