# R24-A PR Collision Matrix

侦察时点（2026-10-27T02:2xZ）：开放 PR = 0；远端仅 r23 残留分支（已合并，不再动）。并行 B/C 轨尚未推分支。

## 预期写集 vs 兄弟轨

| 文件/目录 | R24-A | R24-B（data/model runtime） | R24-C（Pi-MCP/Qt 工作台） | 冲突处置 |
|---|---|---|---|---|
| src/processing/algorithms/** | **写** | — | — | 无 |
| src/analysis/{...}/** | **写** | — | — | 无 |
| src/operators/rs/** | **写** | runtime/ 不碰 | — | 无（rs/ vs runtime/ 不相交） |
| src/processing/framework/** | 只读 | 可能写 | — | 冲突→CROSS_TRACK 记录 |
| src/core/** | 只读 | 可能写 | 可能写 | 冲突→CROSS_TRACK |
| src/app/**、pi/** | 只读 | — | 写 | 无本轨动作 |
| tests/CMakeLists.txt | 末尾 append R24-A 块 | 末尾 append R24-B 块 | 末尾 append R24-C 块 | append-only 并集；推送前跑 `scripts/check_merge_registration_integrity.py --require-refs` + 手动核对无冲突标记、无重复注册 |
| .gitignore | 不动 | 兄弟轨惯例 append | append | 不相交 |
| data/processing/algorithm_meta/ | 仅关联算法确需时单 sidecar | — | 只读消费 | 无 |

## 推送前复核（PR 创建流程第 3 步）

1. `git fetch origin --prune` → 重列开放 PR 与分支
2. `git diff origin/master...HEAD --name-only` 全量列写集，与上表核对
3. 如 B/C 已有开放 PR 且路径相交 → 仅在本 worktree 内 merge/rebase 最新 master，并集保留两侧注册，跑 `scripts/check_merge_registration_integrity.py --require-refs`
4. `git diff --check`（空白/冲突标记）
