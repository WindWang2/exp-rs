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

### 重查结果（2026-10-27 08:0x 本地，push 前执行）
- `git fetch origin --prune`：**origin/master = ad98a7a70 未漂移**（无需 rebase/merge）
- 新远端分支：`agent/r23-e-platform-plugin-release-20261026T170017Z` → **Open PR #1716**（r23-E：plugin isolation/worker compat，21 文件）
- **与 #1716 文件交集 = 1：tests/CMakeLists.txt**（双方各自 append-only 注册块；E 块在前部区域、D 块在文件末尾）。#1716 未合并，本轨不预合并兄弟分支；后合并者按 #1715 先例做注册块并集（union），两侧均无删除行，预期为纯相邻追加冲突，解法唯一（两块都保留）
- 本地 worktree：主仓（master）+ 本轨；无其他兄弟写入
- `git diff --check`：clean；`check_whitelist.sh origin/master`：**WHITELIST: PASS**（.gitignore 与 tests/CMakeLists.txt 均 pure additions；冲突标记 0）
- `ctest -N`（经 run_tests.cmd）：r23_d 5 用例在册（Test #1311-1315），与既有全部注册并存

- 重查时间戳：2026-10-27T00:0xZ（UTC，push 前）
