# R30-T06 · 独立 Review R1 → 处置记录

- Reviewer：独立子 agent（新上下文，未主导实现，全程只读、未运行构建/测试、未改文件）。
- 审查对象：工作树真实 `git diff`（base `dcda90bf`）+ 关键调用方 + 本轨接受标准。
- 审查时 HEAD：实现已落地但**尚未提交**、尚未构建；所有「通过」均为静态代码核对结论。
- R1 总结论：**当时不建议进入 R2**（3 个 P1 + 1 个需实跑确认 + 4 个 P2 + 4 个建议）。

## 一、发现与处置（逐条）

| ID | 严重度 | 证据 / 复现 | Owner | 处置 | 状态 |
|---|---|---|---|---|---|
| P1-1 | P1 | 两个新测试只有 `isEnabled()/toolTip()/label` 状态断言；`groupZoomExtent` 触发路径、`setArtifactPickFn`、`compareRequested`、`buildContextMenuActions` 全部无覆盖 | 本轨 | 补 GREEN 行为断言：`zoom->trigger()` 后 canvas extent 数值、`groupZoomExtent` 纯投影拒绝、`QSignalSpy` 验证两侧非首产物、菜单/按钮/`ActionPlan` 资格一致、缺失产物不被配对、菜单带拒绝原因 | ✅ 已修（①③④⑤）；②「canvas 被删触发不崩」与「失败提交不显示 queued」未单列用例 → 记入限制 |
| P1-2 | P1 | `src/core/qgsmapsettings.cpp:543-565` 的 `layerExtentToOutputExtent()` **内部已 catch `QgsCsException` 并返回未变换 extent**；我的 `catch(...)` 是死代码，CRS 失败会把图层 CRS 坐标混入输出 extent | 本轨 | 改为 `settings.layerTransform(layer)` → `isValid()` 时 `setBallparkTransformsAreAppropriate(true)` + 显式 `try/catch (const QgsCsException&)` 调 `transformBoundingBox`；无效变换且两 CRS 有效时 skip 图层；不再调用静默回退 API | ✅ 已修 |
| P1-3 | P1 | `ActiveViewHost::mapCanvas()` 只返回构造时的 `m_mapCanvas`；`setExtent()`（`active_view_host.cpp:141-150`）却用 `displayManager->mapCanvas(activeViewId())` 动态解析。菜单打开期间切视图会静默缩放错误 canvas | 本轨 | `ActiveViewHost` 新增 public `activeMapCanvas() const`（动态解析 + fallback），菜单创建与触发均改用它，并在触发时校验与 `canvasGuard` 一致（切视图即拒绝 + 提示重开菜单） | ✅ 已修 |
| P1-4 | P1（需实跑） | 旧回归 `test_r29_t04_history_actions.cpp:265-266` 只认 `"retry queued as new task"` / `"resubmitted as new task"`；新失败文案 `"No task was queued."` 不含两者 | 本轨 / 旧测试 owner | 成功分支文案保持不变；失败分支必须诚实。**待 `ctest` 实跑该用例并贴真实输出**；若 FAIL 再与旧测试 owner 协调（本轮不改旧综合测试文件） | ⏳ 待实跑 |
| P2-1 | P2 | 根目录 `syntax_probe.py` / `run_build.sh` / `wait_and_run.sh` 未被 `.gitignore` 忽略，`git add -A` 会带进 PR | 本轨 | 提交时**显式列出文件**、绝不用 `git add -A`；PR 前从工作树删除这 3 个脚本 | ⏳ 提交前执行 |
| P2-2 | P2 | 资格只要求「存在一对 raster」，动作却强制用户所选两侧均为 raster → 按钮 enabled 但点击后被取消 | 本轨 | `compareSelectedOutputs()` 先把两侧候选过滤为「exists && supported && Raster」的 `HistoryEntry` 副本，再交给既有 `pickOutputArtifact()`；资格与结果口径一致，过滤后为空时诚实拒绝 | ✅ 已修 |
| P2-3 | P2 | `pickOutputArtifact()` 单产物行直接返回 `first()`，绕过注入缝；同一注入函数被调用两次且无「哪一侧」标识 | 本轨 | **保留 + 文档化**：在 `processing_history_panel.h` 明确注入缝只适用多产物行；测试用带状态的 lambda 区分两侧（已验证 `picks == 2`）。不改签名以免扩大接口面 | ✅ 有依据保留 |
| P2-4 | P2 | `pushMessageBarAlert` 无 messageBar 时静默；headless 测试无法观察拒绝 | 本轨 | **有依据降级**：生产 shell 会 `setMessageBar`；菜单 `toolTip`/`statusTip` 与 disabled 状态是主拒绝反馈；headless 静默不影响用户 | ✅ 有依据保留 |
| S1 | 建议 | `canZoom` 在菜单创建时算一次，模态期间状态变坏不刷新 enabled | 本轨 | **降级**：触发时重算 `groupZoomExtent` + 诚实拒绝已覆盖主要风险，与 QGIS 上游 action 行为一致 | ✅ 有依据保留 |
| S2 | 建议 | `emit` 后写 `"Comparing X with Y"`，若 shell `loadRasterLayer` 失败仍显示假成功 | 本轨 | 改为 emit **之前**写中性文案 `"Comparison requested: … (see the comparison window)."` | ✅ 已修 |
| S3 | 建议 | 注释称 `supported` 含「已知可打开类别」判断，实际只 `exists && isFile()` | 本轨 | 注释与 `DECISIONS D09/D11` 对齐：`kind` 只是标签/比较提示，不是可打开性权威 | ✅ 已修 |
| S4 | 建议 | `ACCEPTANCE.md` 状态未随进度更新 | 本轨 | R2 / PR 前按真实测试输出复核 | ⏳ 待 R2 |

## 二、R1 静态核对通过项（无需改动）

QPointer/lambda 生命周期与 connect context；`QgsRectangle` null/empty 语义与上游对齐（`DECISIONS D18`）；`QMenu::exec` 生命周期；`actionPlan()` 与 `selectedEntry()` 同走 `actionTargetRow()`（资格与触发目标恒等）；`compareRequested(QString,QString)` 等 5 个信号签名与 shell 接线不变；`OutputArtifactChoice` 尾部追加字段不破坏旧断言/聚合初始化；`buildContextMenuActions` 的 `connect` 合法；`TaskCenter::retryTask` 返回 0 表示失败；跨轨边界（未碰 T04/T07/T05/JobEngine/vendored `src/core`/`src/gui`，`tests/CMakeLists.txt` 仅末尾追加，旧综合测试未改）；`git diff --check` 干净；CMake 链接列表经 `test_command_registry`/`test_active_view_host_viewport`/`test_workspace_snapshot` 对照合理。

## 三、 reviewer 对修复的静态覆盖判断

R1 二次确认：P1-2、P1-3、P2-1、P2-2 **已覆盖**；P1-1 **基本覆盖**（② canvas 被删、④ 失败提交不 queued 建议确认入测）；P1-4 仍需主 agent 贴 `test_r29_t04_history_actions` 真实输出；P2-3/P2-4/S1–S4 属待处置/建议，不阻断 R2。

## 四、本轨对「未完全覆盖」的诚实记录

- **② canvas 被删 / 视图切换触发时不崩**：实现有 `QPointer` + 触发时 live 一致性校验，源码级证据充分，但本轮 Qt offscreen 测试未单列该用例 → `ACCEPTANCE.md` F1 记为「实现已覆盖，运行时用例未入测」。
- **④ 失败提交不显示 queued**：需要让真实 TaskCenter 返回 0/负才能触发，测试未构造该失败注入；生产路径已按 `>0` 门禁 + 明确失败文案，靠 `ctest` 旧回归的成功路径间接验证 → 记为限制。
- **RED 未实跑**：见 `ACCEPTANCE.md` / `LEDGER.md`，给出逐行调用链与边界证明，不虚构 RED 输出。
