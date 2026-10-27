# R24-B PR COLLISION MATRIX

侦察时点（2026-10-27T02:30Z）：开放 PR = 0；远端分支仅 master + 2 个已合并 r23 残留。

| 轨 | 写集 | 与本轨交集 | 避让 |
|---|---|---|---|
| A 科学算法 | `src/processing/algorithms/**`, `src/analysis/**` 等 | 无（本轨禁改该区） | 只读 |
| B 本轨 | `src/{geospatial,data,dataset,experiment,operators/runtime,runtime}/**` + `tests/test_r24_b_*` + 本规划目录 | — | — |
| C Pi-MCP/Qt-QGIS | `src/agent/**`, `src/app/**`, `pi/**`, `src/gui/**` 等 | 无（本轨禁改该区） | 只读 |
| 共享 | `tests/CMakeLists.txt` | 唯一共享写点 | 末尾 append `# BEGIN R24-B`/`# END R24-B`；推送前重 fetch + `git diff origin/master...HEAD --name-only` 全列 + merge-integrity 脚本 |

推送前复查流程：`git fetch origin --prune` → `gh pr list --state open` → 若出现新开放 PR 且路径重叠，只在本地 worktree 合并/变基最新 master 解决，不丢任何一侧注册。
