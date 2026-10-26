# R22-01 science PR 冲突矩阵（2026-10-25T13:40Z 首次盘点）

## 与 OPEN PR 的文件交集
| PR | 交集文件 | 冲突评估 |
|---|---|---|
| #1676 (R21-A) | `src/processing/algorithms/math_utils.cpp` | #1676 只抽 `bothNormalizable`（L171-197 区域）；本轨改 `computeStatsFromAccumulators`（L142-169）。无文本重叠，合并时语义兼容。 |
| #1676 (R21-A) | `src/operators/rs/rs_kmeans_operator.cpp` | #1676 改 scaler 时序（~L330）；本轨改 reservoir RNG（L210-285）。不同函数，git 可自动合并。 |
| #1673/#1674/#1675/#1677 | 无文件交集 | data/teaching/pi-mcp/qt 轨，无冲突 |

## 与历史合并 PR
- #1671（R20-01 已合并）：science 数值回归已进 master，本轨基线测试必须全绿后才能开始 RED。
- #1663（R18-A 已合并）、#1534：science 相关历史修复已在 master，不重复。

## Push 前复检义务
- 重跑 `gh pr list` + `gh pr view <n> --json files` 对比新增文件交集；
- `git fetch origin master` 成功后比对 SHA，若 master 前进则 rebase 语义核对。

## 2026-10-26T05:0xZ Push 前复检（第二轮）
- 远端 master 仍为 911e0b17（gh api 确认）——无 rebase 需要。
- 新增 open PR：#1708(r22-infra)、#1709(r22-inference)、#1710(r22-data)、#1711(r22-teaching)。
- 文件交集复核：
  - `tests/CMakeLists.txt`：#1708/#1709/#1710/#1711 与本轨均为文末 append-only
    注册块——若相邻行合并可能产生行级冲突，解决方式为保留双方追加块（语义零冲突）。
  - `.gitignore`：各轨白名单 stanza 互不相交。
  - 源文件：零交集。
- #1709 已实施 #1706（FeatherAccumulator）——本轨 ISSUE_TRIAGE 中"#1706 留待
  下一里程碑"的记录已被兄弟轨 supersede，以 #1709 为准，本轨不重复实施。
- #1676（math_utils.cpp/rs_kmeans_operator.cpp 轻度交集）结论不变：不同函数区域，
    git 可自动合并。
