# R30-T06 · PR / 分支矩阵（base dcda90bf，2026-11-13）

## open PR

**0 条**（启动取证与 PR 提交前两次 `gh api --paginate pulls?state=open` 均为空集合）。
本轨 PR 将是第一条 R30 open PR。提交前 `origin/master` 已前进到 `606e3c9101`（#2053），
本轨 rebase 后 ahead 2 / behind 0；9 个新提交与本轨文件零重叠，详见 `BASELINE.md`。
→ PR-first 队列已清空；本轨不存在「活动 PR 覆盖同一修复」的冲突，但仍需按 `ISSUE_TRIAGE.md`
避免重复领取 R29 已实现项。

## 本轨相关已合并 PR（conversation / reviews）

| PR | 合并时间 | head 分支 | 与本轨关系 |
|---|---|---|---|
| #1828 R29-T04 | 2026-11-11 15:16Z | `zcode/r29-t04-workbench-20261108T194233Z` | 历史动作身份统一、多产物 picker、retry/resubmit 新 ID |
| #1829 UI | 2026-11-11 14:58Z | `codex/fix-ui-review-followups` | tool dialog 布局 / 失败保存保留项目 |
| #1957 R29-T01 | 2026-11-11 15:01Z | `zcode/r29-t01-science-...` | typed readiness（T06 只消费，不重做判定） |
| #1958 R29-T02 | 2026-11-11 15:01Z | `zcode/r29-t02-data-runtime-...` | QGIS host-thread 制图派发（属 T04 执行权威） |
| #1827 R29-T03 | 2026-11-11 15:05Z | `zcode/r29-t03-pi-mcp-...` | Pi/MCP（T05） |
| #1998 | 2026-11-12 09:05Z | — | 隐藏数据面板 palette refresh 延后 |
| #2028 | 2026-11-13 10:58Z | — | cartography `asInt` 加 `isInt` 防护 |
| #2031 / #2039 / #2044 | 2026-11-13 | swarm 分支 | 严格整数 / enum index / schema form int 范围 |

`gh api issues/{1828,2044}/comments` 与 `pulls/{1828,2044}/reviews` **均为空集合**；
按要求即使 API 为空也检查了 conversation 与仓内独立 review 记录（主 worktree
`.workbuddy/review/` 存在），本轨不把空 review 冒充「已审」。

## 远程分支（111 条，T06 相关 6 条）

`git rev-list --left-right --count origin/master...origin/<branch>`（左 = master 独有，右 = 分支独有）：

| 分支 | master-only | branch-only | diff 文件数 | 最近提交 |
|---|---|---|---|---|
| `agent/r23-c-agent-workbench-20261026T164126Z` | 504 | **0** | 0 | 2026-10-28 merge master |
| `agent/r25-D-qt-qgis-cartography-20261028T170355Z` | 452 | **0** | 0 | 2026-10-29 planning note |
| `agent/r26-workbench-20261030T151124Z` | 366 | **0** | 0 | 2026-10-31 merge master |
| `agent/r27-t04-qt-qgis-workbench-ui-20261102T162559Z` | 253 | **0** | 0 | 2026-11-04 merge master |
| `agent/r28-t04-workbench-cartography-20261104T210902Z` | 140 | **0** | 0 | 2026-11-06 merge master |
| `zcode/r29-t04-workbench-20261108T194233Z` | 87 | **0** | 0 | 2026-11-11 teaching test |

**结论**：全部 `ahead = 0`、与 master 无文件差异 → retained 分支，不是活动开发的证据。
`git for-each-ref refs/remotes/origin/ | grep -i r30` → **0 条 R30 分支**，本轨为新开第一条。
其余近期分支（`fix/swarm-20261113*`）对应 #2031–#2044 已合并补丁，亦为 retained。

## 本机 worktree（只读核对）

`git worktree list --porcelain` 列出 50+ worktree；与本轨主题接近的有
`exp-rs-r28-t04-workbench-cartography-*`、`exp-rs-r29-t04-workbench-*`、`swarm-20261113b-carto`
等。均**只读**核对，未推送工作与远端事实分开记录；本轨不借用其构建产物。
