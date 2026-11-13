# R30-T06 · DECISIONS（unattended 模式，按 .agents/AGENTS.md §7）

> 常规技术选择自行比较后写在这里，不逐步请求人工确认。每条含「选项 / 取舍 / 选定」。

| ID | 决策 | 备选与理由 | 选定 |
|---|---|---|---|
| D01 | 宿主无 `/goal-loop` / `/goal` | 已核实 `.agents/skills/`、`.claude/skills/`、`.zcode/skills/` 无 goal-loop；WorkBuddy 无该命令。改 Prompt 首行、或改宿主 hooks/权限都属于越界 | **不改宿主**；采用其方法论（完成条件→聚焦改动→亲自验证→记账→重新判断）自主迭代，证据入本轨账本 |
| D02 | host-build 锁 `held` | 强占/删活锁、终止其他 agent 都被任务禁止 | 先做不占锁工作（源码分析、实现、测试编写、记账），锁转 `free` 后用双锁 + 显式 `--parallel 4` 重试 |
| D03 | 本轮聚焦范围 | 12 个 WP / F1–F7 全量一次做完不现实；任务允许跨上下文恢复且禁止无效循环 | 本轮做 **C1（F1 组缩放）+ C2（F2 历史动作）** 两个可离线 Qt 复现、T06 独占、有现成测试设施的闭环；C3/C5/C6 记入 `PLAN.md`/`HANDOFF.md`，C4 保持 `NEEDS_REPRO` |
| D04 | F1 修复落在第一方而非 vendored QGIS | 方案 A：给 `actionZoomToGroup` 传 `m_activeViewHost->mapCanvas()`（`layer_tree_menu.cpp` 一行）。方案 B：第一方实现组 extent 计算与触发时 live canvas 校验。方案 C：改 QGIS `zoomToLayers` 加 null guard | **B 为主 + A 的等价目标**：QGIS action 把 canvas 以 `reinterpret_cast<void*>` 存进 `QAction::data`，菜单打开期间 canvas 被删 / 视图切换仍会 UAF；仅传指针不解决 F1 的「过期对象」要求。改 vendored QGIS 又违反「优先第一方接线、禁止全域替换」。故菜单改用第一方 action，触发时经 `QPointer<ActiveViewHost>` 重新查询 live canvas 并校验一致性 |
| D05 | F1 的「诚实拒绝」口径 | 空组、非空间表、无效/已移除图层、CRS 变换失败时：静默 return（现状）或弹错误 | 触发前按 live group 计算 `canZoom` 并 disable action + tooltip 写明原因；触发时再算一次，任一环节失败经 `ActiveViewHost::pushMessageBarAlert` 给出具体原因，**不**静默、**不**崩溃 |
| D06 | F1 不新增全局 command | 任务要求「统一菜单、ribbon、快捷键的目标与可用性」；实测 registry 只有 `layer.zoomTo`（单图层），无 group-zoom ribbon/shortcut | 不新增全局命令（YAGNI，且 `main_window*.cpp` 是共享热点）。菜单是唯一入口，其 enabled/tooltip 由同一 `ActionPlan` 逻辑派生；若后续发现 ribbon 入口再按同一函数接线 |
| D07 | F2 资格统一 | 方案 A：菜单复制 `updateActionStates()` 的布尔（易漂移）。方案 B：抽 `ActionPlan` 单一真源，按钮 + 菜单 + 触发前重查共用 | **B**，并给每个动作附拒绝原因（tooltip / 状态栏） |
| D08 | 双产物比较 | 方案 A：新增第二个 picker 分叉。方案 B：复用既有 `pickOutputArtifact()` 对两侧各调用一次 | **B**（任务明确「不再重复实现一个分叉版本」）。保持 `compareRequested(QString,QString)` 信号与 shell `openComparisonForPaths` 不变 |
| D09 | 跨类型比较判定 | 后端 `openComparisonForPaths` 只 `loadRasterLayer`，即只支持栅格；但 UI 没有权威 typed artifact 服务 | 用**轻量 UI 资格启发式**（路径存在 + 可比较类别），在 `DECISIONS`/PR 中明确标注为「UI 侧启发式、非新权威」；不做 GDAL 探测、不新建验证器。未知后缀保持宽容（`supported=true`）以免误禁用合法历史产物 |
| D10 | 失败重试的 diagnostic | `TaskCenter::retryTask` 文档明确「返回 0 表示失败」；`enqueueTask` 返回 `long`，<=0 为拒绝。仓内没有可供 UI 直接读取的 typed admission diagnostic | 只在 **newTaskId > 0** 时显示 queued；否则显示明确的失败说明（含原 task id 与「未被接受」），**不编造 diagnostic 文本**。父任务/来源关联参数保持原样，UI 不重写重试政策 |
| D11 | `outputChoicesFor.supported` | 现状恒 true（`.h:62` 默认） | 计算为「存在 + 常规文件 + 已知可打开类别」；未知类别保持 true 以兼容历史产物；`label` 追加类别/来源信息但保留原文件名与 `(missing)` 子串，确保 `test_r29_t04_history_actions.cpp` 的既有断言不被破坏 |
| D12 | 测试落点 | 改 `test_r29_t04_history_actions.cpp`（T06 拥有）vs 新建 | **新建 `tests/test_r30_t06_*.cpp`**，CMake 只追加 `R30-T06` 小块；旧综合测试只运行不改 |
| D13 | RED 阶段避免崩溃 | 当前 HEAD 触发 group zoom 会 SIGSEGV，Catch2 无法捕获进程崩溃 | RED 用「action 绑定的 live canvas 非空 / 空组与无 canvas 时 disabled / 菜单与按钮资格一致」等**状态断言**（当前稳定 FAIL、不崩）；修复后在同文件补「触发后 canvas extent 改变」行为断言，并在 LEDGER 记录两阶段证据 |
| D14 | 构建加速与合规 | 全新 worktree 全量编译很慢；ccache 10 GB / 43% 命中 | 允许使用系统 ccache（内容 hash 的编译缓存，不是借用其他 worktree 的二进制），但**不**从其他 worktree 复制产物；CMake 重新 configure + 重新链接。若命中率低则如实记录耗时，不谎称全绿 |
| D15 | 文档落后于代码 | `docs/repo-layout.md` 写 ADR 上限 0145，`CLAUDE.md` 注明 ADR 0166 取代 | 以 `ls docs/adr` / CMake 实测为准；本轨不改这两份文档（非本轮任务），仅记录 |
| D16 | PR 授权边界 | 用户长期偏好「不自行 push/PR/merge」；本次 Prompt 明确「提交 PR 即为交付终点」并给出 `gh pr create` | 按**本次显式指令**提交本轨 PR（仅推自己的新分支），**不合并、不启用 auto-merge、不删除任何其他分支**；提交前在回复中明示 |
| D17 | host-build 孤儿锁处理 | 锁文件 holder `pid=3555970`（T01 轨 `cmake --build ... test_experiment_evaluation`），`ps -p` 无该进程，`ps -eo` 无任何 `cmake --build`/`ninja`/`cc1plus` 进程，锁龄 906 s。选项：① 一直等到默认 `--stale-after 3600` 自动过期（可能阻塞整轨）；② 手动 `rm` 锁（违反「不删除活锁」，且绕过 guard 记账）；③ 用 guard **官方** `--stale-after 600` 让脚本自己按「holder pid 已死 AND 锁龄超阈值」规则 break | **③**：`resource_guard.py --lock-dir "$HOST" --parallel 4 --max-parallel 4 --stale-after 600 --wait 20 -- true` → `PROBE_EXIT=0`，`--status` 由 `held` 转 `free`。未手动删文件、未终止任何进程、未改 guard 默认策略；其余六轨仍按原规则竞争同一把 canonical 锁 |
| D14b | ccache 跨 worktree 命中 | 全新 worktree 全量编译很慢；ccache 默认把工作目录/绝对源路径纳入 hash，跨 worktree 命中率低 | 只在**本轨构建命令的 shell 内**导出 `CCACHE_BASEDIR=<WT>` 与 `CCACHE_NOHASHDIR=1`（不写入仓库、不污染全局）。这是内容 hash 的编译缓存，**不是**借用其他 worktree 的二进制；configure/链接仍由本 HEAD 独立完成。命中率与耗时如实记录 |
| D18 | F1 的 `isEmpty()` 语义 | 初版实现把 `QgsRectangle::isEmpty()` 也当作「不可用」，会误拒单点/单线图层（零面积但非 null 的合法 extent） | 与 `QgsLayerTreeViewDefaultActions::zoomToLayers()` 对齐：只用 `isEmpty()` 触发 `updateExtents()` 刷新，**只用 `isNull()` 决定是否跳过**。已同步在 `groupZoomExtent`、菜单 enabled 判定、触发时判定三处 |
