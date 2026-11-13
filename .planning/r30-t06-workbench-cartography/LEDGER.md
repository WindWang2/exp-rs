# R30-T06 · LEDGER（本轨唯一持久账本）

> 状态只允许 `DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `BLOCKED` / `NOT_EXECUTED`。
> 只有前两者满足完成条件。禁止把未解决阻塞悄悄移到「建议」。

| # | 条目 | 状态 | 证据 |
|---|---|---|---|
| L01 | 启动取证：真实 master / open PR / issues / 分支 / worktree / 文档 | **DONE** | `BASELINE.md`、`PR_BRANCH_MATRIX.md`、`RESUME.md`；`git fetch origin --prune` 成功 |
| L02 | 创建独立 worktree 与本轨 `.planning/` 体系 | **DONE** | `zcode/r30-t06-workbench-cartography-20261113T142904Z` @ `dcda90bf` |
| L03 | 读取真实 skill / 确认 `/goal-loop` 未注册 | **DONE** | `.agents/skills/` + `.claude/skills/` + `.zcode/skills/` 列表；`RESUME.md` |
| L04 | `resource_guard.py --help` 与双锁路径核实 | **DONE** | 427 行脚本，参数齐全；host 锁 canonical 绝对路径写入 `RESUME.md` |
| L05 | WP01 六候选 triage | **DONE** | `ISSUE_TRIAGE.md`，含 QGIS `zoomToLayers` 调用链逐行证据 |
| L06 | R29 / #2044 已实现项标 `ALREADY_DONE_WITH_EVIDENCE` | **DONE** | `PR_BRANCH_MATRIX.md`、`ISSUE_TRIAGE.md §C`；未重复领取 |
| L07 | C1（F1）RED 回归测试 | **文件已写，待 RED 运行** | `tests/test_r30_t06_group_zoom.cpp`（3 个 RED-safe case，只用 baseline API） |
| L08 | C2（F2）RED 回归测试 | **文件已写，待 RED 运行** | `tests/test_r30_t06_history_action_parity.cpp`（5 个 RED-safe case） |
| L09 | C1 生产修复 | **已实现，待构建验证** | `src/app/layer_tree_menu.cpp/.h`：`groupZoomExtent()` + 第一方 live-canvas action + QPointer 生命周期 |
| L10 | C2 生产修复 | **已实现，待构建验证** | `src/app/workbench/processing_history_panel.cpp/.h`：`ActionPlan` 单一真源、菜单复用、双产物比较、失败提交反馈 |
| L11 | 本轨 HEAD 独立 configure + build（-j4 双锁） | **进行中** | host 孤儿锁经 guard 官方 `--stale-after 600` 规则转 `free`（DECISIONS D17）；configure 已用双锁 + `--parallel 4` 后台启动 |
| L21 | 编译期语法自检（辅助，非验证） | **DONE** | `syntax_probe.py` 复用主 worktree `compile_commands.json`，2 个生产 TU `-fsyntax-only` **exit=0**；2 个测试 TU 因模板缺 QGIS GUI/Catch2 include 而报错（模板不匹配，非代码缺陷），待真实 CMake 验证 |
| L12 | `ctest -N` / `--show-only=json-v1` 核对真实测试名与数量 | **NOT_EXECUTED** | 依赖 L11 |
| L13 | 新增回归 + 直接受影响既有测试通过 | **NOT_EXECUTED** | 依赖 L11/L12 |
| L14 | 独立 R1 review（新上下文子 agent 读真实 diff） | **NOT_EXECUTED** | 待 `REVIEW_R1.md` |
| L15 | R1 发现修复 + R2 复查（绑定最终 HEAD） | **NOT_EXECUTED** | 待 `REVIEW_R2.md` |
| L16 | `git diff --check` / staging 核查 / fetch 复核 base | **NOT_EXECUTED** | 提交前 |
| L17 | PR_BODY.md + 提交本轨 PR（不合并） | **NOT_EXECUTED** | PR URL / base / head SHA 待填 |
| L18 | WP03 / WP06–WP11 全量覆盖 | **NOT_EXECUTED** | 见 `PLAN.md`；不冒充完成 |
| L19 | WP12 三条完整链（目录→地图 / 任务→结果 / 地图→交付） | **NOT_EXECUTED** | 本轮 PR 只覆盖前两条中的可离线子场景；不谎称三条全链 |
| L20 | 高 DPI / 焦点 / 明暗主题真实桌面截图证据 | **NOT_EXECUTED** | 本轮 Qt offscreen 不足以证明；明确限制 |

## 失败命令台账

| 命令 | 退出码 | 说明 |
|---|---|---|
| `ctest --test-dir <main>/build-dev -N`（复合探查中） | 137 / SIGTERM | 复合命令超时被杀；非本轨验证，未在本轨重跑 |
| `python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --status` | 0 | 输出 `held` → L11 BLOCKED，非失败 |

## 迭代计数（goal-loop 借鉴）

- 轮 1（CP0）：启动取证 → 完成。
- 轮 2（CP1）：源码 triage → 完成，信息增量：6 候选逐条证据 + 5 处 F2 子差距。
- 连续 3 轮失败 / 8 轮无增量的换层规则：当前未触发。
