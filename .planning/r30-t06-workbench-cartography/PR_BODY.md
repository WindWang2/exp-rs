# R30-T06 · 工作台、地图与制图交付（本轮：组缩放 + 历史动作资格）

> 本轨标识 `t06-workbench-cartography`；启动 base `dcda90bf00a705f00be1e119e44cf1a502fd38ea`（#2044），
> 开发期间 master 前进 9 个提交至 `606e3c91017d8dc3d3ad363e613065ef214a82c6`（#2045–#2053），本分支已 rebase 到该 base。
> 本轮只交付 **F1 / WP02** 与 **F2 / WP04-WP05** 两个已复现并修复的工作包；
> 其余 WP 明确 `NOT_EXECUTED`，见 `PLAN.md` / `ACCEPTANCE.md` / `HANDOFF.md`，不冒充全轨完成。

## 原问题与用户影响

| 编号 | 问题（真实源码证据） | 用户可观察后果 |
|---|---|---|
| F1 | `src/app/layer_tree_menu.cpp:95` 调用 `defActions->actionZoomToGroup( nullptr )`。QGIS `src/gui/layertree/qgslayertreeviewdefaultactions.cpp:129-134` 把 canvas 以 `reinterpret_cast<void*>` 存进 `QAction::data`，`:397-402` 取回后进入 `:404-457 zoomToLayers`，`:435/:445` `canvas->mapSettings()`、`:456` `canvas->setExtent()` | 对**任一含空间图层的组**点「Zoom to Group」→ 空指针解引用崩溃；空组 / 纯属性表组静默无反应；canvas 在菜单打开期间被删 → 悬空指针 UAF |
| F1b | `ActiveViewHost::mapCanvas()` 只返回构造时的 `m_mapCanvas`；而 `setExtent()`（`active_view_host.cpp:141-150`）用 `displayManager->mapCanvas(activeViewId())` 动态解析 | 多视图下组缩放会静默作用到错误 canvas |
| F2 | `processing_history_panel.cpp:371-383` 右键 6 个 action 全部 enabled，不复用 `:349` 的 `updateActionStates()`；`:490-500` 比较硬取两侧 `outputPaths.first()`；`:395-410` `supported` 恒为默认 true；`:453-477` `enqueueTask` 返回 0/负仍显示「resubmitted as new task 0」；`:385-392` `cancelSelected()` 无 running 门禁 | 终态任务可从菜单/键盘被 cancel；两个多产物任务永远只比较各自第 0 个文件；跨类型/缺失产物被当成可比较；失败重提交被显示成已入队；按钮与右键资格互相矛盾 |

## 已有实现基线（不重复领取）

- R29-T04（#1828）已实现：历史 action 稳定身份、model reset 按 ID 恢复选择、open/inspect 多产物 picker、retry/resubmit 的新 task ID 说明。`test_r29_t04_history_actions.cpp` **未改动**。
- #1998 隐藏数据面板 palette refresh 延后；#2031/#2039/#2044 严格整数 / enum index / int 范围 —— 各字段「严格整数」与「历史小数截断/默认回退」契约**保持不变**，本 PR 无任何全局替换。
- 制图 export/produce 仍走 `submitOperatorJob()` → 真实 TaskCenter 异步路径，本 PR **未改** `cartography_operators.cpp` / TaskCenter / JobEngine / OutputCommitter。

## 本次功能增量

**F1 / WP02**
- 新增 `LayerTreeMenuProvider::groupZoomExtent(canvas, group, reason)`：纯投影，跳过已移除 / 无效 / 非空间（Null geometry）/ 无 extent 图层；显式 `QgsCoordinateTransform` + `QgsCsException` 处理 CRS 失败（**不**依赖 `layerExtentToOutputExtent()` 的静默回退，R1 P1-2）；5% margin 与 QGIS 上游一致；null/empty 语义与 `zoomToLayers()` 对齐（R1 / D18）。
- 组菜单改用第一方 action：创建时按 live canvas 计算可用性并写 tooltip/statusTip；触发时经 `QPointer<ActiveViewHost>` / `QPointer<QgsMapCanvas>` / `QPointer<QgsLayerTreeNode>` 重新解析，视图切换或对象销毁即**诚实拒绝并提示重开菜单**，不再解引用过期对象。
- `ActiveViewHost::activeMapCanvas() const` 新增（动态解析活动视图 + fallback 主 canvas），与既有 `setExtent()`/`refreshCanvas()` 语义一致（R1 P1-3）。`mapCanvas()` 保持原语义不变。

**F2 / WP04-WP05**
- 新增 `ActionPlan actionPlan() const` 单一资格真源（每个动作附拒绝原因）。`updateActionStates()`、`buildContextMenuActions(QMenu*)` 与**所有 slot 的触发前重查**共用；按钮 tooltip 追加可读原因，菜单 action 同样 disabled + tooltip。
- `outputChoicesFor()` 计算 `supported`（存在且为常规文件）、`kind`、`sourceDir`，label 显示名称 / 类型 / 缺失状态；`artifactKind()` 是 **UI 侧后缀启发式、非新权威**，未知后缀保持可打开以免误禁用历史产物（DECISIONS D09/D11）。
- `compareSelectedOutputs()`：两侧候选先过滤为「存在 && supported && Raster」副本，再复用既有 `pickOutputArtifact()` **各选一个真实产物**；拒绝跨类型、单边缺失、取消，不产生错误配对；`compareRequested(QString,QString)` 信号与 shell `openComparisonForPaths` 完全不变。状态文案改为中性「Comparison requested」，不宣称未验证的成功（R1 S2）。
- `rerunSelected()`：只有 `newTaskId > 0` 才显示 queued；失败显示明确「未被接受 / No task was queued」，不编造 typed diagnostic（TaskCenter 未向 UI 暴露）。父任务/来源参数保持原样。
- `setArtifactPickFn()` 注入缝（仿 `ActiveViewHost::ConfirmationFn`），仅用于测试绕开模态菜单；生产仍用原 anchored QMenu。

**测试**：新增 `tests/test_r30_t06_group_zoom.cpp`（**5** 个 case）与 `tests/test_r30_t06_history_action_parity.cpp`（**8** 个 case），合计 **13** 个，含真实 `zoom->trigger()` 后 canvas extent 数值、活动视图切换、`groupZoomExtent` 纯投影拒绝、`QSignalSpy` 验证两侧非首产物（`picks == 2`）、菜单/按钮/`ActionPlan` 资格一致、缺失产物不被配对。两个 target 均为 `TIMEOUT 120` + 独立 `TEST_PREFIX`。`tests/CMakeLists.txt` 仅末尾追加 `R30-T06` 小块，未重命名/重排任何既有 target。

## 与其他轨的边界

| 轨道 | 本 PR 是否触碰 |
|---|---|
| T01/T02/T03 | ❌ 未触碰 |
| T04 `cartography_operators.cpp` / TaskCenter / JobEngine / `workflow_session_controller.*` / `output_verifier.*` / `test_cartography_production_11.cpp` | ❌ 未触碰；制图执行权威保持原样 |
| T05 `agent_ops` / `mission_*` / `agent_context_tool.*` | ❌ 未触碰 |
| T07 Studio / experiment / study / teaching / `dataset_experiment_panel.*` | ❌ 未触碰 |
| 共享热点 `main_window*.cpp/.h`、根主题、生成快照、fixtures | ❌ 本轮完全未改（仅只读调用链取证） |
| `.gitignore` / `tests/CMakeLists.txt` | 仅追加本轨 `R30-T06` 小块 |

跨轨请求与后续工作包见 `HANDOFF.md`。本 PR 可独立构建，不要求其他六轨同时合入。

## 接口变化

- **新增（可选追加，旧消费者可构建）**：`LayerTreeMenuProvider::groupZoomExtent`（static）、`ActiveViewHost::activeMapCanvas()`、`ProcessingHistoryPanel::{ArtifactKind, ActionPlan, actionPlan, artifactKind, buildContextMenuActions, setArtifactPickFn}`、`OutputArtifactChoice::{kind, sourceDir}`（尾部字段，带默认成员初始化器）。
- **不变**：`compareRequested(const QString&, const QString&)` 及其余 4 个信号、`createContextMenu()`、`outputChoicesFor` 返回类型与既有字段、`mapCanvas()`、`LayerTreeMenuProvider` 构造签名。
- 公共结构字段 / 错误码 / 状态 / 取消 / 输出身份语义全部冻结；无跨轨必需接口。

## 真实命令与结果

> 详细台账见 `LEDGER.md` / `ACCEPTANCE.md`；下列均为本轨独立 worktree 的真实执行结果，不引用其他 worktree 产物。

| 命令 | 结果 |
|---|---|
| `git fetch origin --prune`；`gh api --paginate pulls?state=open` / `issues?state=open` / `branches` | master `dcda90bf`；open PR **0**；open issues **48**（过滤 `pull_request`）；远程分支 111，T06 相关 6 支 `ahead=0` |
| `python3 scripts/dev/resource_guard.py --help` | 支持 `--lock-dir/--parallel/--max-parallel/--wait/--stale-after/--status/--json` |
| 4 个 TU 的 `-fsyntax-only`（复用主 worktree `compile_commands.json` flags，include 重定向到本 worktree） | **`TOTAL_FAILURES=0`**：`layer_tree_menu.cpp`、`processing_history_panel.cpp`、`test_r30_t06_group_zoom.cpp`、`test_r30_t06_history_action_parity.cpp` 全部 `exit=0`；期间抓到并修复一处真实编译错误（`addFeatures` 需非 const 左值） |
| `git diff --cached --check` | `EXIT=0`，无 whitespace 错误 |
| 独立 R1 review（新上下文子 agent 读真实 diff + 关键调用方） | 3 个 P1 + 1 个待实跑 P1 + 4 个 P2 + 4 建议；**已逐条处置**，见 `REVIEW_R1.md` |
| `cmake --preset dev-default -DOpenCV_DIR=… -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`（双锁 + `--parallel 4`） | ⚠️ 日志出现 `Configuring done (33.4s/53.1s)`，但**生成阶段被宿主中断**：`build-dev/tests/CTestTestfile.cmake` 始终未生成，`build.make` 最多 2238/4133。见「必要限制」 |
| `cmake --build … --parallel 4 --target <5 targets>` / `ctest -N --show-only=json-v1` / `ctest -j1` | **NOT_EXECUTED**（依赖上一步）；不声称 case 数、不声称通过 |

## 必要限制（不虚构成功）

- **host-build 锁由七轨共享**，本轮期间被 T01/T02/T03 轨的重型任务反复占用；其 guard 进程多次成为**死 PID 孤儿锁**。本轨始终用官方 `resource_guard.py --stale-after` 规则处理死 PID 锁，**未手动删活锁、未终止其他 agent**。
- 宿主 `SAFE_DELETE_BULK_CONFIRM_REQUIRED`（阈值 50）会拦截 guard 的 `.staged` 文件清理，因此改用「只读 `--json --status` 轮询 + 零 `--wait` 单次 guard」；CMake 生成阶段（约 4000 个 `build.make`）多次被宿主中断，本轨 `build-dev` 只完成部分生成。
- 因此 **`cmake --build` / `ctest` 未实跑**：新增回归、直接受影响的既有测试（`test_r29_t04_history_actions`、`test_processing_history_model`、`test_r22_workbench_view_tree`）**均未取得运行时证据**，R1 的 P1-4（旧 retry 文案兼容性）也**未实跑确认**。
- **RED 未实跑**：base 上触发组缩放会 SIGSEGV，Catch2 无法在进程内捕获；且最终测试含修复后新增 API 的行为断言，无法在 base 上编译。已改以逐行调用链 / 边界证明（`ISSUE_TRIAGE.md`、`REVIEW_R1.md`）替代，并在此明确记录。
- Qt offscreen 不足以证明高 DPI / 焦点 / 明暗主题 / 屏幕阅读名称（WP11），本轮无真实桌面截图证据。
- 三条完整链（WP12）、大 collection 分页（WP06）、制图草稿/修订（WP07-09）、项目边界矩阵（WP10）均 `NOT_EXECUTED`，不制造空 PR 或虚假完成声明。
- 另两处测试覆盖限制（R2-4）：单产物行绕过 `setArtifactPickFn` 注入缝，故「缺失产物不配对」用例的 `offered` 只覆盖多产物的一侧（产品逻辑由候选过滤 + `QFileInfo` 二次校验兜底）；「菜单打开期间切视图 → 触发被拒绝」「canvas 被删不崩」「CRS 变换失败」「失败提交不 queued」未单列运行时用例，只有源码级证明。
- `setExtent(extent, true)`（R2-1）与 vendored QGIS `qgslayertreeviewdefaultactions.cpp:456` 上游完全一致；`mScaleLocked` 下只改 magnification 属上游既有语义，本轮不擅自改变，见 `DECISIONS.md D19`。

**交付形态**：按任务「必要环境…缺失时先提交可审查的 Draft PR 并明确 BLOCKED/NOT_EXECUTED；这不满足『全部开发完成』，待条件具备后继续」，本 PR 以 **Draft** 提交，**不自称全轨完成、不自行合并、不启用 auto-merge、不删除任何其他分支**。恢复步骤见 `RESUME.md`：环境具备后先补齐 configure → `ctest -N` → build → ctest（含 P1-4 复核），再用真实输出更新 `ACCEPTANCE.md` / `LEDGER.md` 并转为正式 PR。

## Review

- **R1**：独立子 agent（新上下文，未主导实现），读真实 `git diff` + 关键调用方 + 接受标准；结论「当时不建议进入 R2」；3 个 P1 + 1 个待实跑 P1 + 4 个 P2 + 4 个建议，逐条处置见 `REVIEW_R1.md`。
- **R2**：另一个独立子 agent，绑定 `3d6a225d80b810de961c6088a3a1864947b36f13` 复查修复与最终 diff；**未发现 P0/P1 静态阻断项**，R1 的 P1-1/P1-2/P1-3 与 P2-2 全部静态确认修好，6 个 R2 新发现（P2/建议）已处置或有依据保留。见 `REVIEW_R2.md`。
- **最终差异复核（rebase 后）**：第三个独立子 agent 复核 HEAD `30d9afeb7f`：独立重算确认 `git diff dcda90bf..3d6a225d` 与 `git diff 606e3c9101..0805c5e72e` 的 sha256 同为 `a611a1d3…`；22 个改动文件无其他轨夹带、无生成物/缓存/脚本；master 的 32 个改动文件与本轨零重叠，且未触及本轨依赖的 7 个接口头；`git diff --check origin/master..HEAD` = 0；`git status --short` 干净。结论：静态层面可提交 PR（Draft）。
- **Review SHA**：R2 绑定 `3d6a225d`（历史对象，补丁等价）；最终生产 commit `0805c5e72e`、HEAD `30d9afeb7f`。R2 之后的提交仅含文档/账本与 `tests/CMakeLists.txt` 的 `TIMEOUT 120`，**不改动生产逻辑与测试断言**。
- **复核未完成项**：R2 明确要求 P1-4 与全部运行时结论由主 agent 实跑补充；本轮因环境阻塞未完成，故以 **Draft PR** 交付。

Refs #1934（app swarm 汇总，逐条清单在外部 gist，未逐条领取）
Refs #1805（app-ui 低优先级汇总，含 F14 cartography dock lifetime，本轮未覆盖该条）
