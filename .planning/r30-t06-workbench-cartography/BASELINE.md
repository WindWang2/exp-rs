# R30-T06 · BASELINE（启动取证，2026-11-13）

## PR 提交前的 base 变更（2026-11-13 复核）

`git fetch origin --prune` 后 `origin/master` 从 `dcda90bf` 前进 9 个提交到
`606e3c91017d8dc3d3ad363e613065ef214a82c6`（#2045–#2053，全为整数范围 / JSON 转义修复，
共 32 个文件，集中在 `src/agent/**`、`src/operators/**`、`src/runtime/**`、`src/workflow/**`、
`src/cli/**`、`src/geospatial/**`、`src/plugins/**`、`src/processing/**`、`src/help/**`）。
与本轨 22 个文件**零重叠**，未触及 `qgsmapsettings.h`、`qgslayertreeviewdefaultactions.cpp`、
`qgsmapcanvas.h`、`task_center.h`、`qgis_display_manager.h` 等依赖接口。
本轨已 `git rebase origin/master`（无冲突），并独立重算确认生产补丁字节等价
（`git diff dcda90bf..3d6a225d` 与 `git diff 606e3c9101..0805c5e72e` sha256 同为 `a611a1d3…`）。
最终 base = `606e3c9101`，生产 commit = `0805c5e72e`，HEAD = `30d9afeb7f`（及后续账本提交）。

## 真实基线（重新读取，不使用快照 SHA 冒充最新）

| 项 | 实测值 | 取证命令 |
|---|---|---|
| `origin/master` | `dcda90bf00a705f00be1e119e44cf1a502fd38ea` | `git fetch origin --prune` 后 `git rev-parse origin/master` |
| 提交时间/标题 | 2026-11-13 20:01:29 +0800 `fix: keep schema form integers inside int range (#2044)` | `git log -1 --format=...` |
| 默认分支 | `master` | `gh repo view WindWang2/exp-rs --json defaultBranchRef` |
| 最新合并 PR | #2044 | `gh api pulls?state=closed&sort=updated` |
| open PR | **0** | `gh api --paginate pulls?state=open&per_page=100` |
| open issues | **48**（已过滤 `pull_request != null`） | `gh api --paginate issues?state=open` |
| 远程分支 | 111 | `gh api --paginate branches?per_page=100` |
| 本轨 worktree | `zcode/r30-t06-workbench-cartography-20261113T142904Z` @ base | `git worktree add -b` |

主 worktree `/home/kevin/projects/rs-studio/main` 当前在 `fix/pr-review-followups-2026-11-06`
@ `ed1c1c1f`，有未提交文件 `.workbuddy/`、`PR_REVIEW_MERGE_2026-11-06.md`。
**保留，未重置、未清理**；本轨所有写入均在新 worktree。
`git worktree list --porcelain` 列出 50+ 既有 worktree（r28/r29/swarm 等），全部只读保留。

## 已合并的相关工作（不能描述为「尚未实现」）

| PR | 合并时间 | 内容 |
|---|---|---|
| #1828 R29-T04 | 2026-11-11 15:16Z | Workbench 动作身份统一、实验比较权威接线、Studio 会话/复现导入、CompletionLedger |
| #1829 UI | 2026-11-11 14:58Z | tool dialog layout 回归、失败保存时保留项目 |
| #1957/#1958/#1827 | 2026-11-11 15:01–15:05Z | R29 T01/T02/T03（科学评价 / published-claim+QGIS host-thread 派发 / Pi-MCP） |
| #1998 | 2026-11-12 09:05Z | 隐藏数据面板 palette refresh 延后 |
| #2031/#2039/#2044 | 2026-11-13 | 严格整数 `isInt` / `isInt64` / schema form int 范围 |
| #2028 | 2026-11-13 10:58Z | cartography `asInt` 读取加 `isInt` 防护 |

PR conversation / 正式 reviews / inline threads：#1828、#2044 的
`issues/{n}/comments` 与 `pulls/{n}/reviews` 均返回**空集合**（已记录，未编造 review 结论）。
#1934/#1931/#1942 等 open issues 的逐条清单放在外部 gist，本轨不据标题批量领取。

## 仓内规则与文档

- `CLAUDE.md`（96 行）、`.agents/AGENTS.md`（28 行）、`docs/repo-layout.md`（97 行）已读。
- `.agents/AGENTS.md` 优先级：冲突时 `CMakeLists.txt` / `CMakePresets.json` 决定，然后本文件；
  unattended 模式把决策写入 `DECISIONS.md`。四条约束：想清楚再写、YAGNI、外科式改动、构建+测试验证。
- `CMakePresets.json` v3：`dev-default`（Debug/ENABLE_TESTS=ON/ shortcuts ON，binaryDir `${sourceDir}/build-dev`）；
  仓内 preset 的 build jobs 为 1–2，**本轨按任务用显式 `--parallel 4` 上限，不改 preset 默认值**。
- `scripts/dev/resource_guard.py`（427 行）已读 `--help`：支持 `--lock-dir/--wait/--parallel/--max-parallel/--stale-after/--status/--json`，
  忙时退出 75，payload 用 `--` 分隔，会 clamp `--parallel` 与导出环境变量。
- 文档落后于代码的点：`docs/repo-layout.md` 的 ADR 上限 0145 被 `CLAUDE.md` 的 ADR 0166 说明取代 → 记入 `DECISIONS.md`，不影响本轨。

## 环境

CMake 4.4.5 / GCC 16.2.1 / Ninja 1.13.2 / Qt6 `/usr/lib/cmake/Qt6` / ccache 4.14.1
（10 GB，43.06% 命中）/ 62 GiB RAM（20 GiB 已用）/ 489 GiB 空闲。
主 worktree 的 `build-dev`（Unix Makefiles、11-05、旧 HEAD）**仅作存在性参考，不用于证明本 HEAD**。
