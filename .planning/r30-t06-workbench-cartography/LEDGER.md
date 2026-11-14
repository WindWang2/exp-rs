# R30-T06 · LEDGER（本轨唯一持久账本）

## 合并前补充（2026-11-14，当前）

| 项 | 状态 | 证据 |
|---|---|---|
| Windows configure/generate + 7 targets build（guard / -j2） | DONE | 生产/测试提交 `87427610d`；详见 MERGE_VALIDATION_20261114.md |
| 定向原生 CMake/Catch2 注册发现 | DONE | 58 cases；新增 group/history 前缀为 5/8 |
| 新增 + 直接受影响旧回归 + 两构建修复目标 | DONE | 58/58 PASS，0 失败，34.07 s；含 P1-4 旧 retry 文案 |
| 首次运行发现后的修复 | DONE | 54/58 -> 58/58；补 currentChanged 按钮刷新，LRU 名称 ASCII 化；排序夹具未放宽断言 |
| 合并注册完整性 | DONE | 1204 targets / 56 add_test；lost=0、markers=0、duplicates=0 |

以下为原 Draft 历史账本；构建/运行的旧阻塞由本补充覆盖，其他未执行范围不变。


> 状态只允许 `DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `BLOCKED` / `NOT_EXECUTED`。
> 只有前两者满足完成条件。禁止把未解决阻塞悄悄移到「建议」。
> 绑定最终 HEAD：`30d9afeb7f`（生产 `0805c5e72e` @ base `606e3c9101`，rebase 后；
> 与 R2 审查的 `3d6a225d` 补丁字节等价，sha256 `a611a1d3…`）。

| # | 条目 | 状态 | 证据 |
|---|---|---|---|
| L01 | 启动取证：真实 master / open PR / issues / 分支 / worktree / 文档 | **DONE** | `BASELINE.md`、`PR_BRANCH_MATRIX.md`、`RESUME.md`；`git fetch origin --prune` 成功 |
| L02 | 创建独立 worktree 与本轨 `.planning/` 体系 | **DONE** | `zcode/r30-t06-workbench-cartography-20261113T142904Z` @ `dcda90bf` |
| L03 | 读取真实 skill / 确认 `/goal-loop` 未注册 | **DONE** | `.agents/skills/` + `.claude/skills/` + `.zcode/skills/` 列表；`RESUME.md` |
| L04 | `resource_guard.py --help` 与双锁路径核实 | **DONE** | 427 行脚本，参数齐全；canonical host 锁路径写入 `RESUME.md` |
| L05 | WP01 六候选 triage | **DONE** | `ISSUE_TRIAGE.md`，含 QGIS `zoomToLayers` 逐行调用链 |
| L06 | R29 / #2044 已实现项标 `ALREADY_DONE_WITH_EVIDENCE` | **DONE** | `PR_BRANCH_MATRIX.md`、`ISSUE_TRIAGE.md §C`；未重复领取 |
| L07 | F1 新回归测试 | **DONE（13 cases 中的 5 个）** | `tests/test_r30_t06_group_zoom.cpp`：5 个 `TEST_CASE`，含触发后 extent 数值、活动视图切换、纯投影拒绝 |
| L08 | F2 新回归测试 | **DONE（13 cases 中的 8 个）** | `tests/test_r30_t06_history_action_parity.cpp`：8 个 `TEST_CASE`，含 `QSignalSpy` 两侧非首产物、菜单/按钮/`ActionPlan` 资格一致 |
| L09 | F1 生产修复 | **DONE（静态）** | `layer_tree_menu.cpp/.h`：`groupZoomExtent()` + 第一方 live-canvas action + QPointer；`active_view_host.h::activeMapCanvas()` |
| L10 | F2 生产修复 | **DONE（静态）** | `processing_history_panel.cpp/.h`：`ActionPlan` 单一真源、菜单复用、双产物比较、失败提交反馈 |
| L11 | 本轨 HEAD 独立 configure + build（-j4 双锁） | **BLOCKED** | CMake 生成阶段反复被宿主中断：`tests/CTestTestfile.cmake` 未生成、`build.make` 最多 2238/4133；已尝试 6 种锁/进程模式（`DECISIONS.md D20`）。未手动删活锁、未终止他人 |
| L12 | `ctest -N` / `--show-only=json-v1` 核对真实 case 数与 prefix | **NOT_EXECUTED** | 依赖 L11；不把「未核对」说成 0 tests 或通过 |
| L13 | 新增回归 + 直接受影响既有测试通过 | **NOT_EXECUTED** | 依赖 L11/L12；R1 P1-4（旧 retry 文案兼容性）未实跑 |
| L14 | 独立 R1 review | **DONE** | `REVIEW_R1.md`：3 P1 + 1 待实跑 P1 + 4 P2 + 4 建议，逐条处置，结论「当时不进入 R2」 |
| L15 | R1 修复 + 独立 R2 复查（绑定最终 HEAD） | **DONE** | `REVIEW_R2.md` 绑定 `3d6a225d80`：未发现 P0/P1 静态阻断；6 个 P2/建议全部处置或有依据保留 |
| L16 | `git diff --check` / staging 核查 | **DONE** | `git diff --cached --check` = 0；R2 复核 `git diff --check dcda90bf..3d6a225d` = 0；显式列 20 个文件提交，**未用 `git add -A`** |
| L17 | PR_BODY.md + 提交本轨 PR（不合并） | **进行中** | `PR_BODY.md` 已写；按任务以 **Draft PR** 交付并明确 BLOCKED/NOT_EXECUTED |
| L18 | WP03 / WP06–WP11 全量覆盖 | **NOT_EXECUTED** | 见 `PLAN.md` / `HANDOFF.md`；不冒充完成 |
| L19 | WP12 三条完整链 | **NOT_EXECUTED** | 本轮未做三条全链；不谎称 |
| L20 | 高 DPI / 焦点 / 明暗主题真实桌面证据 | **NOT_EXECUTED** | Qt offscreen 不足以证明 |
| L21 | 编译期语法自检（辅助，非验证） | **DONE** | 4 个 TU `-fsyntax-only` 全部 `exit=0`（`TOTAL_FAILURES=0`）；期间抓到并修复 `addFeatures` 非常量左值引用的真实编译错误 |
| L22 | 本地 commit C1 | **DONE** | `3d6a225d80b810de961c6088a3a1864947b36f13`，20 个文件 |
| L23 | 环境阻塞记录与可恢复检查点 | **DONE** | `DECISIONS.md D17/D20`、`RESUME.md`、`ACCEPTANCE.md`、`HANDOFF.md`；不虚构构建/测试/PR 成功 |
| L24 | RED 实跑 | **NOT_EXECUTED（已给边界证明）** | base 触发组缩放会 SIGSEGV，Catch2 无法进程内捕获；最终测试含修复后新增 API，无法在 base 编译。替代证据：`ISSUE_TRIAGE.md` 逐行调用链 + `REVIEW_R1.md` |

## 失败命令台账

| 命令 | 退出码 | 说明 |
|---|---|---|
| `ctest --test-dir <main>/build-dev -N`（早期复合探查） | 137 / SIGTERM | 复合命令超时；非本轨验证 |
| 内联 `python3 - <<'PY'` 语法自检 | 137 / SIGTERM | heredoc + 4 TU 编译超过默认时限；改磁盘脚本 + `timeout 600000` 后成功 |
| `cmake --preset dev-default`（首次，未带 OpenCV_DIR） | 1 | `src/processing/CMakeLists.txt:12` 要求 `OpenCV_DIR`；已补共享前缀，二次 configure 到达生成阶段 |
| guard `--wait 45/30/20` 多次 | 75 | T01/T02/T03 轨活锁 + 死 PID 孤儿锁；用官方 `--stale-after` 处理孤儿，活锁只等待 |
| guard `--wait` 产生 `.staged` 清理 | — | 触发宿主 `SAFE_DELETE_BULK_CONFIRM_REQUIRED`（阈值 50）；改为只读 `--json --status` 轮询 + 零 wait 单次 guard |
| 前台 configure（3 次）/ 后台 payload（3 次，含 `--wait 5400` 与沙箱豁免） | 137 / SIGTERM 或中途被杀 | CMake 生成阶段挂起/被宿主终止；详见 `DECISIONS.md D20` |
| `fish` 下用 `$?` | 121 | shell 语法差异；后续一律 `bash -c` 包裹 |

## 迭代计数（goal-loop 借鉴）

- 轮 1（CP0）：启动取证 → 完成。
- 轮 2（CP1）：源码 triage → 完成，信息增量：6 候选逐条证据 + 5 处 F2 子差距。
- 轮 3（CP2/CP3）：实现 + 语法自检 → 完成，抓到并修复 1 处真实编译错误。
- 轮 4：独立 R1 → 3 个 P1 修复 + P2/S 处置 → 完成。
- 轮 5：构建验证尝试 → 连续多轮因**同一类环境阻塞**（宿主中断 CMake 生成 + 七轨锁竞争）失败；按「连续 3 轮失败列出三个不同假设 / 8 轮无增量换调查层级」规则，已换过 6 种执行模式并定位到宿主层，故**停止无效重试**，转交环境阻塞记录。
- 轮 6：独立 R2 → 无 P0/P1 静态阻断，6 个 P2/建议处置 → 完成。
