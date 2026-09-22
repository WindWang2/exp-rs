# Hardening 14/20 — App / Workbench / UI Shell — Recon Baseline

Track: `app-workbench-ui-shell` · Branch: `hardening/app-workbench-ui-shell` · Worktree: `../exp-rs-hardening-app-workbench-ui-shell`

## 启动时事实（2026-09-23，git fetch 后）

- `origin/master` = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`（与 prompt 的 recon seed 一致，刚合并 #1236）。
- Open issues：0。
- Open PRs（8 个，seed 已过期）：
  - #1237 `feat/undergrad-lab-cockpit` — teaching cockpit。改 `src/app/main_window.{h}`,`main_window_docks.cpp`,`main_window_menus.cpp`,`main_window_misc.cpp`,`src/app/workbench/command_defs.cpp`,`src/app/CMakeLists.txt`,`src/app/teaching/**`,`src/teaching/**`。
  - #1238 `feat/experiment-exploration-studio` — 改 `main_window.h`,`main_window_workbench.cpp`,`workbench/command_defs.cpp`,`src/app/CMakeLists.txt`。
  - #1239 `feat/teaching-admin-console` — 改 `main_window.h`,`main_window_docks.cpp`,`main_window_misc.cpp`,`src/app/CMakeLists.txt`。
  - #1240 `feat/science-context-broker` — `src/science_context/**`,`src/agent/*`；无 shell 冲突。
  - #1241 `feat/agent-ops-control-center` — `src/agent_ops/**`,`src/app/agent_ops/**`；无 shell 冲突。
  - #1242 geospatial hardening、#1243 spectral hardening、#1244 temporal hardening — 与本 Track 无文件交集。

## 精确冲突图（按 diff hunk 核实，非推测）

对共享文件逐 hunk 验证（`git diff a9dc33fa7 origin/<branch> -- <file> | grep '^@@'`）：

| 文件 | Open PR 触及的函数/区域 | 本任务是否可改 |
|---|---|---|
| `main_window.h` | #1237 :283(public)、:585(private)；#1239 :584(private) | 谨慎（本任务选择零改动） |
| `main_window_docks.cpp` | #1237/#1239 仅 `setupDockWidgets()` | 避开 |
| `main_window_menus.cpp` | #1237 仅 `setupMenu()` 尾部 | 避开 |
| `main_window_misc.cpp` | #1237 `showGuidedWorkflows()` 区、#1239 `resetPanelLayout()` :400 | 避开 |
| `main_window_workbench.cpp` | #1238 :27、`setupWorkbenchInfrastructure()` :595、`showDatasetExperimentBench()` :924 | 避开 |
| `workbench/command_defs.cpp` | #1237/#1238 `registerShellCommands()` 内新增块 | 避开 |
| `src/app/CMakeLists.txt` | #1237/#1238/#1239 | 避开 |
| `tests/CMakeLists.txt` | #1238 插入 :294；#1237/#1239 追加 EOF :12338 | 本任务改动位于 :6857（既有 target 块）与 :10390（mission 块后），两侧均无重叠 |
| UNCONTESTED | `main_window.cpp`,`main_window_connections.cpp`,`main_window_project.cpp`,`main_window_view.cpp`,`main_window_processing.cpp`,`main_window_layers.cpp`,`main_window_vector.cpp`,`workbench/command_registry.*`,`workbench/command_palette.*`,`workbench/selection_context.*`,`workbench/inspector_host.*`,`workbench/scientific/*`,`workbench/workbench_host.*`,`workbench/workbench_state.*`,`workbench/shutdown_policy.*`,`widgets/guided_workflow_widget.*`,`pipeline/guided_workflow_workbench.*`,`shell/gui_job_adapter.*`,`main.cpp` | 本任务修改面 |

本任务全部实现改动落在 UNCONTESTED 文件 + `tests/CMakeLists.txt` 的无重叠区域；零新增与 open PR 的同函数冲突。

## Defect 清单（recon subagent B1–B14 + 主 agent GW1）

见 `01-status-matrix.md` 的现状矩阵与本 PR 的修复范围：
- 本轮修复：B1（P1 mission 跨项目泄漏）、B3（P2 Save-As 后 watcher 不换轨）、GW1（P1 GuidedWorkflow 跨实验状态污染 + 越界读）、B5（P2-latifent CommandRegistry 快捷键保留泄漏）。
- 本轮仅记录：B2、B4、B6–B14（后续 slice）。

## 历史已修复（避免重复报告 / 移植旧码）

`01e22bfae` (#509-#521 widget lifetime)、`fc736af60`（重复 Task Center dock）、`2fec5d1f7`（Data Manager 创建顺序）、`3d578e595`（plugin manager 析构顺序）、`77c0452b6` (#777-#812 workbench/canvas lifetime)、`dba4c5bff`（InspectorHost tab rebuild）、`7287e3027`+`63d69a60e` (#792/#794/#795 shortcut ownership)、`f3ec3f021`（datasetExperiment 快捷键冲突）、`c2951a457`（m0 UI safety）、`6b7164af3`+`7988ffbbf` (#893 model reset)、`02bca69a3`（removeView disconnect / VA hub）、`5e9192052`+`c144696e8` (#1083 probe read)、`fe9c24c50` (#1097 ribbon/menubar)、`8b692bc8b` (#1208)、`cba5c1342` (#1213)、`1f3e61b47` (#1214)、`0b36446e2` (#1215)、#1200 内的 #1148/#1149（mission save 单通道 + first-publication）。

## 与旧分支 / 其他 Track 的去重结论

- `rs14-unified-verifier`：旧平行实现，权威是已合并 `src/verify` — 不涉及。
- `agent/flash-*`：远落后于 master，仅作缺陷线索矿；本轮未从中移植任何代码。
- `agent/rs14-experiment-debugger`、`agent/rs14-scene-suitability-assessor`：已合并残留，无独有提交。
- 5 个 feature PR（#1237–#1241）全部为**新增能力**，不修复本任务声明的任何 defect；无重复实现风险。
