# R30-T06 · ISSUE_TRIAGE / 候选缺口 triage（base dcda90bf）

判定口径：`CONFIRMED_OPEN`（当前源码可复现、归本轨、无活动 PR 覆盖）、
`FIXED_OPEN`（已修但 issue 仍开）、`PARTIAL`、`SUPERSEDED`、`NEEDS_REPRO`。
open PR = 0，故无活动冲突；issue 集合里没有逐条对应本轮 6 个候选的单条 issue。

## A. 本轨 6 个源码候选（WP01，逐条读真实源码）

| # | 候选 | 真实源码证据 | 判定 |
|---|---|---|---|
| C1 | 组缩放空 canvas | `src/app/layer_tree_menu.cpp:95` `defActions->actionZoomToGroup( nullptr )` → `src/gui/layertree/qgslayertreeviewdefaultactions.cpp:129-134` 把 `reinterpret_cast<void*>(canvas)` 存进 `QAction::data` → `:397-402` 取回 → `:359-370 zoomToGroup(canvas)` → `:404-457 zoomToLayers(canvas,...)`，`:435` / `:445` `canvas->mapSettings()`、`:456-457` `canvas->setExtent/refresh`。组内有任一有效空间图层即**空指针解引用**；纯属性表/空组在 `:449` 静默 return。`:368` `QgsProject::mapLayer(id)` 返回 nullptr 时 `:416` 也会 `layer->extent()` 崩溃。 | **CONFIRMED_OPEN**（F1） |
| C2 | 历史按钮/菜单/比较 | `processing_history_panel.cpp:371-383` 右键 6 action 全 enabled，不复用 `:349-369` 的 `updateActionStates()`；`:490-500` 比较硬取两侧 `outputPaths.first()`；`:395-410` `outputChoicesFor` 只填 `exists`，不计算 `supported`（`.h:62` 默认 true）；`:453-477` `enqueueTask` 返回 0/负仍显示 "resubmitted as new task 0"；`:385-392` `cancelSelected` 无 `running` 门禁（按钮有、菜单/键盘没有）。 | **CONFIRMED_OPEN**（F2 / WP04-05） |
| C3 | 制图草稿/修订 | `cartography_dock.cpp:566` 在判断 `compiled`（`:567`）**之前** `adoptSpec(...)` → `:537-542` 无条件写入 `m_currentSpec` / `m_composedLayoutName`；`:496` `hasComposedLayout` 仅判名称非空，不查 `LayoutService::findLayout`（对比 `:744` designer 有查）；`:585,613,687` Check/Fix/Produce 用缓存 `m_currentSpec`，Compose `:548` 读控件；`:775` 预览只渲染 page 0。 | **CONFIRMED_OPEN**（F5/F6 / WP07-08），本轮按范围取舍 |
| C4 | 当前视图→制图来源身份 | `main_window_workbench.cpp:775-800` 按 `layer->source()` 去重（`:779-780`），空选择 fallback 主 `m_mapCanvas`。 | **NEEDS_REPRO**：静态看到去重字符串不等于证明输出错误；需要第二视图/同 source 不同样式/可见性/顺序的真实 GUI 场景，本轮不凭字符串断言 |
| C5 | 大 collection 尾项可达 | `data_manager_panel.cpp:893-937`：`:913` 到 `m_standaloneRowCap` 后 `continue`，`:931-935` 只显示 "Showing first N of M — use the filter"；对照 `:1130-1158` standalone 有 `m_standalonePage` / pager / `Items x–y of z`。collection 分支无后续页入口。 | **CONFIRMED_OPEN**（F3 / WP06） |
| C6 | 质量/多页交付投影 | `cartography_dock.cpp:531-535` 只 `setPlainText(JSON)`；`:723-728` 只显示 output/page_count；`:766-785` 只 `renderPageToImage(0)`。 | **CONFIRMED_OPEN**（F6），本轮按范围取舍 |

## B. 相关 open issues

| Issue | 状态 | 依据 |
|---|---|---|
| #1934 `[P2/P3][app]` 165 条 | **NEEDS_REPRO** | 汇总型，逐条清单在外部 gist；含 `rs_class_table_widget.cpp` 等**非本轨**路径。不批量领取，不据标题断言 |
| #1931 `[P2/P3][agent]` 119 条 | **NEEDS_REPRO / 跨轨** | 前 5 条多为 `src/agent/cartography/cartography_operators.cpp` → 整文件归 **T04**，本轨不碰 |
| #1942 `[P2/P3][gui]` 553 条 | **NEEDS_REPRO / 跨轨** | vendored QGIS 上游风格问题，本轨只在用户路径证明缺陷时最小修正 |
| #1805 `[P3][app-ui]` | **PARTIAL** | F14 提到 `cartography_dock.cpp:315-355` 终态 lifetime 泄漏 → 属本轨但未在本轮 6 候选优先集；其余 F6/F7/F12/F13/F17/F18 多为本轨外或 vendored |
| #1632 `[P3] src/app` 6 项 | **NEEDS_REPRO** | 技术债/文档漂移，逐条指向 `terrain_dialog.h` 等，与本轮结论不重叠 |
| #1628 `[P3] src/agent` 5 项 | **SUPERSEDED（对本轨）** | `mcp_server.cpp` 等归 T05/T04 |
| #1695 `[R17-ui][P3]` | **SUPERSEDED（对本轨）** | 死代码/i18n 清单，基线 `@911e0b17`，非本轮用户路径 |
| #1956 / #1930 / #1932 / #1601 等 | **NEEDS_REPRO / 跨轨** | 全项目审查汇总、双 verifier 核心（归 T04/T07）、agent-ops（T05） |

## C. 已修但 issue 仍开 / 不再重复领取

- R29-T04 `#1828` 已合入：历史 action 稳定身份、model reset 按 ID 恢复选择、open/inspect 多产物 picker、
  retry/resubmit 新 task ID 说明。`test_r29_t04_history_actions.cpp` 覆盖 identity/selection/actiontarget/artifacts/retry。
  → 本轮 **不重写**这些；只在其上补 `ALREADY_DONE_WITH_EVIDENCE` 之外的剩余闭环（C2）。
- `#1998` 隐藏面板 palette refresh 延后 → `ALREADY_DONE_WITH_EVIDENCE`。
- `#2031/#2039/#2044` 严格整数 / enum index / int 范围 → `ALREADY_DONE_WITH_EVIDENCE`；
  保留「严格整数 vs 历史小数截断/默认回退」两种字段契约，**不做全局替换**。

## D. 结论

本轮优先实施 **C1（F1）+ C2（F2）**，二者都是 T06 独占、可离线 Qt 复现、有现成测试设施、
且不依赖其他六轨接口。C3/C5/C6 记入 `PLAN.md` / `HANDOFF.md` 作为后续工作包或跨轨交接，
不制造无证据的「全部完成」声明。
