# R30-T06 · 独立 Review R2（绑定最终 HEAD）

- Reviewer：独立子 agent（新上下文，既未主导实现、也未做 R1）；全程只读，未改文件、未运行构建/测试。
- 绑定 HEAD：`3d6a225d80b810de961c6088a3a1864947b36f13`（`dcda90bf..3d6a225d` 仅 1 个提交）。
- **rebase 后注记**：PR 提交前 `origin/master` 前进 9 个提交到 `606e3c9101`，本轨已 rebase，最终生产 commit `0805c5e72e`、HEAD `30d9afeb7f`。独立最终差异复核确认两个生产补丁 **sha256 同为 `a611a1d3…`**、8 个关键文件 blob 全部 IDENTICAL、master 32 个改动文件与本轨 22 个文件零重叠、未触及本轨依赖的 7 个接口头，因此本 R2 对生产 diff 的结论继续成立，不冒充对新 HEAD 的重新审查。
- 审查输入：真实 commit diff + `REVIEW_R1.md` / `DECISIONS.md` / `ACCEPTANCE.md` / `ISSUE_TRIAGE.md` + 关键调用方源码。
- 静态结论：**未发现 P0/P1 阻断项**，最终 HEAD 达到「可提交 PR 的静态质量标准」；`git diff --check dcda90bf..3d6a225d` 退出码 0。
- 阻断性前提（R2 明确不声称通过）：**P1-4 与全部运行时结论（build、ctest -N、GREEN、RED、旧回归）需主 agent 实跑补充**。

## 一、R1 各发现在最终 HEAD 的状态

| R1 ID | 状态 | 依据 |
|---|---|---|
| P1-1 缺 GREEN 行为断言 | ✅ 已修（静态） | `test_r30_t06_group_zoom.cpp` 5 个 case 含 `zoom->trigger()` 后 extent 数值、纯投影拒绝、活动视图切换；`test_r30_t06_history_action_parity.cpp` 8 个 case 含 `QSignalSpy` 两侧非首产物 + `picks==2`、菜单/按钮/`ActionPlan` 资格一致、缺失产物不配对 |
| P1-2 CRS 静默回退 | ✅ 已修（静态） | 不再调 `layerExtentToOutputExtent()`；`settings.layerTransform(layer)` → `isValid()` → `setBallparkTransformsAreAppropriate(true)` → `try/catch(const QgsCsException&)` `transformBoundingBox`；失败或「两有效 CRS 但 transform 无效」均 `++skipped; continue;`。R2 完整推导同 CRS 短接场景 `isValid()==true`、不会误拒 |
| P1-3 固定主 canvas | ✅ 已修（静态） | `activeMapCanvas()` 与 `setExtent()` 同一动态解析；菜单创建与触发均使用；触发时校验 `liveCanvas != canvasGuard` → 拒绝；`mapCanvas()` 旧语义保留 |
| P1-4 旧回归 retry 文案 | ⏳ **仍需实跑** | 成功分支文案原样保留，静态无必然冲突；若真实 TaskCenter 走失败分支，旧断言会 FAIL |
| P2-1 根目录脚本污染 | ✅ 已修 | commit 的 20 个文件不含 3 个辅助脚本；`.gitignore` 仅追加本轨规划目录 allow-list（markdown-only） |
| P2-2 资格与结果口径 | ✅ 已修（静态） | `actionPlan()` 资格集合与 `compareSelectedOutputs()` 过滤集合相同（exists && supported && Raster），两侧同用 `rows[0]/rows[1]`，无「enabled 但过滤后为空」的反向不一致 |
| P2-3 注入缝单产物绕过 | ✅ 有依据保留 | 头文件已文档化；测试用带状态 lambda 区分两侧 |
| P2-4 headless 无 messageBar 静默 | ✅ 有依据保留 | 生产 shell 会 `setMessageBar`；菜单 disabled + tooltip/statusTip 是主拒绝反馈 |
| S1 菜单 enabled 快照 | ✅ 有依据保留 | 触发时重算 + 诚实拒绝覆盖主要风险 |
| S2 假成功文案 | ✅ 已修 | emit **之前**写中性 `"Comparison requested: …"` |
| S3 注释与实现不符 | ✅ 已修 | 注释与 D09/D11 对齐 |
| S4 账本状态滞后 | ✅ 已处理 | 本文件 + 更新后的 `ACCEPTANCE.md` / `LEDGER.md` / `PR_BODY.md` |

## 二、R2 新发现与处置

| ID | 严重度 | 内容 | 处置 |
|---|---|---|---|
| R2-1 | P2 | `layer_tree_menu.cpp:252` `setExtent(extent, true)`：`qgsmapcanvas.cpp:1693-1702` 在 `mScaleLocked && magnified` 时只改 magnification factor、不设 extent；建议改单参数 | **有依据保留 `true`**：核实 `qgslayertreeviewdefaultactions.cpp:456` 上游 `zoomToLayers()` **本身传 `true`**，与 vendored QGIS 组缩放行为完全一致；改 false 会擅自改变 scale-lock 下既有语义。记入 `DECISIONS.md D19`。R2 原文称「上游用 false」与实测源码不符，已记录该反驳 |
| R2-2 | P2 | 账本写 group_zoom「4 个 case」，实际 5 个；history 8 个，合计 13 | ✅ 已修：`PR_BODY.md` / `ACCEPTANCE.md` 统一为 5 / 8 / 13 |
| R2-3 | P2/建议 | `PR_BODY.md` 引用 `REVIEW_R2.md` 但 HEAD 中不存在；`ACCEPTANCE.md` F1/F2 证据栏仍写「修复与验证待完成」 | ✅ 已修：本文件入库；`ACCEPTANCE.md` 改为「修复已落地、静态证据齐备，运行时证据 NOT_EXECUTED」 |
| R2-4 | 建议 | 单产物行绕过注入缝，导致「缺失产物不配对」用例的 `offered` 只覆盖右侧 | ✅ 记录为测试覆盖限制（`ACCEPTANCE.md` / `PR_BODY.md`）；产品逻辑由 `:735-742` 过滤 + `:757-770` 二次校验兜底，安全 |
| R2-5 | 建议 | D09/D11 的「宽容未知后缀」与 compare 强制 Raster 存在表述张力 | ✅ 已修：`DECISIONS.md D09` 明确宽容只作用于 `supported`（open/inspect），不延伸到 compare |
| R2-6 | 建议 | 两个新 target 的 `TIMEOUT` 不一致 | ✅ 已修：`test_r30_t06_group_zoom` 补 `TIMEOUT 120`，与 history target 一致 |

## 三、R2 核对通过项

`ActionPlan actionPlan() const` const 正确性；`buildContextMenuActions` 的 `triggered(bool)→void()` connect 合法且 context=`this`；`OutputArtifactChoice` 尾部字段带默认成员初始化器、不破坏旧断言；`m_statusLabel` 在 emit 前写入；`groupZoomExtent` 用 `layerNode->layer()` + `isValid()` 而非 `QgsProject::mapLayer()`；QPointer 按值捕获 + connect context=`menu`；CMake 链接列表经 `test_active_view_host_viewport` / `test_workspace_snapshot` / `test_command_registry` 对照合理，`qt_add_executable` 默认 AUTOMOC；`sicnu_discover_tests` 的 `TIMEOUT` / `TEST_PREFIX` 转发合法；跨轨边界干净（20 个文件全在本轨允许范围，未碰 `main_window*.cpp` / 根主题 / fixtures / 既有 target 名）；账本未冒充运行时通过。

## 四、最终复核（提交 PR 前）

- 最终 HEAD 若在 R2 后有追加提交（`DECISIONS` / `ACCEPTANCE` / `LEDGER` / `RESUME` / `REVIEW_R2` / `PR_BODY` / `tests/CMakeLists.txt` 的 TIMEOUT / `.gitignore`），需按任务规则复核：这些追加**不改动生产逻辑与测试断言**，仅文档、账本与一个 `TIMEOUT 120` 的 CMake 元数据；补丁等价性以 `git diff` 为准，不做全文件覆盖。
- 生产代码与测试代码自 `3d6a225d` 起**无改动**，故 R2 对生产 diff 的结论继续成立。
- 运行时验证状态见 `ACCEPTANCE.md` 与 `PR_BODY.md`「必要限制」：`BLOCKED / NOT_EXECUTED`，不做虚假「全绿」声明。
