# Recon baseline — Workbench/Project Lifecycle Shell Fixtures（master e4904cd3c）

Track 14/15：收尾 #1269（hardening/app-workbench-ui-shell，merged a4f3b324f）声明的“相邻预存/下一 slice 候选”。执行时复核：`origin/master = e4904cd3c`，open PR = 0，open issue = 0，与 recon seed 一致。

## 与 #1269 known-limits 的逐项对账（执行时全部复核为仍存在）

| #1269 记录 | 执行时证据（e4904cd3c） | 本 track 处置 |
|---|---|---|
| openProject 二次 read 失败后 `QgsProject::fileName()` 仍指向目标文件 | `src/core/project/qgsproject.cpp:1997` `mFile.setFileName(filename)` 在解析前赋值、失败不复位；`main_window_project.cpp:319-326` 失败分支不清 identity，也不刷新 canvas/title → 空会话持有幻影文件名，`saveProject()`（:343-345，非空 fileName 直写）可用空会话覆盖目标文件 | R4 修复（事务边界） |
| secondary view 重开后 `m_dualViewportSync` 不重建 | `main_window_view.cpp:320-330` 控制器创建焊死在 `if (!m_secondaryMapView)` 首建分支；`closeSecondaryMapView():392` delete 控制器但 widget 仅 hide 存活 → 重开后 sync 永久缺失、View toggle 失效（`toggleDualViewportSync` 无控制器即拒绝） | R5 修复（session 提取） |
| layout designer 跨 project clear 悬挂 | 唯一创建点 `newLayout()`（`main_window_project.cpp:237-248`）：`QgsPrintLayout` 归 layout manager，designer 仅 `WA_DeleteOnClose`，无跟踪、无 layout 生命周期连接；`project.clear()`/`removeLayout` 删除 layout 后 designer 仍开着渲染死 scene（ruler/undo/inspector/paint 皆可触尸） | R3 修复（self-defense） |
| `setupWorkbenchInfrastructure()` 内两处 raw-pointer 注册 | 复核修正：两工具的捕获在 83125ede9/362435c40（均早于 #1269 recon）已是 QPointer-guard（`WorkbenchContextTool` provider lambda、`RsEditAgentTool::Sources`）。真正剩余：`SpatialToolRegistry`（`src/agent/spatial_tools/spatial_tool.h:131`）进程级单例、first-wins、**无 per-tool unregister**（仅 `reset()`）→ 窗口死 forever-pin（guarded-but-dead），任何二次 shell 装配（含未来 offscreen fixture）被首个注册静默遮蔽 | R2 修复（scoped token 收敛） |
| “需要完整 shell fixture”（sicnu_geo_rs 无静态库、无先例） | 复核确认 app 无 main-window 静态库；测试按 target 挑选 TU（先例：`test_dual_viewport_sync` 等 30+ 窄 target）。本 track 以“行为所需部件最小 fixture”落地：4 个新窄 target 各自编译被测 seam 的真实 TU（headless ProjectContext / 真实 designer / 真实 canvas 对 / 真实 registry） | R4/R3/R5/R2 各自附 fixture |

## 去重 / 冲突图

- open PR = 0（执行时）；`main_window_view.cpp`/`main_window_project.cpp`/`main_window.cpp`/`main_window_workbench.cpp` 在 #1269 状态矩阵标记 UNCONTESTED；`setupWorkbenchInfrastructure` 的 #1238 争议随其 merge（914d877f6）解除，其改动（ExperimentStudioDock 块 :599-614）与两处注册块（:694-716）不相交，本 track delta union-friendly。
- `tests/CMakeLists.txt`：4 个新 target 追加 EOF（此前 #1237/#1239 亦用 EOF 追加，:12763 起），零重叠插入。
- `src/app/CMakeLists.txt`：两行新增（`workbench/project_session_boundary.cpp` 进 `sicnu_qgis_display` 静态库；`shell/secondary_map_view_session.cpp` 进 app 源表）。
- 历史 branch（agent/flash-*、rs14-unified-verifier）无本模块独有实现，无移植。

## 数值/状态不变量

- 事务边界：hook（story boundary）在空会话上**恰一次**，失败路径不重入；失败后 `fileName()` 为空、governance store 关闭、Save-As 行为等同首次发布（幻影路径零参与）。
- 会话状态机：sync 控制器存活 ⇔ 视图打开；widget 跨 close 复用（指针不变、连接不重复）；reopen 产生**新** engine view id，旧 id 释放。
- registry：token 析构 → 注册消失；first-wins 语义不变；in-flight executor 持 shared_ptr 完成 execute。
