# PR_COLLISION_MATRIX — R23-D 并发冲突矩阵（push 前必须重跑）

## 建档时（2026-10-26/27, UTC）

- 远端：Open PR = 0；远端分支仅 master；origin/master = ad98a7a70（= 本轨 BASE）。
- 本地：git worktree list 仅主仓（master）+ 本轨 exp-rs-D-20261026T165151Z。无兄弟 agent 写入迹象。

## 本轨触碰面 vs 其他轨

| 文件/目录 | 归属 | 冲突风险 | 缓解 |
|---|---|---|---|
| src/lab/session_store.cpp, spec_runtime.cpp | D 专属 | 无 | — |
| data/labs/grading/ndvi_landsat.rules.json（新增） | D 专属 | 无（新文件） | — |
| tests/test_r23_d_ct6_lab_flow.cpp, test_r23_d_lab_hardening.cpp（新增） | D 专属 | 无（新文件） | — |
| tests/CMakeLists.txt | 共享（append-only） | 中（历史 #1714→#1715 事故） | 只在文件末尾追加独立标记块；push 前 fetch，若 master 前进则真实三方合并保留注册并集 + ctest -N 证明目标存在 |
| .planning/zcode-r23-D/** | D 专属 | 无 | — |

## push 前重查清单（强制执行并回填时间戳）

1. `git fetch origin --prune` + `gh pr list --state open`（期望仍 0；若有新 PR 读其 files）
2. `git -C <repo> worktree list`（查新兄弟）
3. 若 origin/master ≠ BASE：`git merge origin/master`（无 -X ours/theirs），冲突逐处解决 + 全量重验（构建+关键 ctest）
4. `git diff --check` + 冲突标记扫描（check_whitelist.sh 内置）
5. `ctest -N` 证明本轨新增测试与他轨注册并存

- 重查时间戳：____（push 前填写）
