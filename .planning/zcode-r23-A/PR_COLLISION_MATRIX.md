# r23-A PR_COLLISION_MATRIX(2026-10-26T16:41Z)

- Open PR = 0 → 无活动 PR 文件交集。
- 历史最近 merged(与本轨可能文件相邻):#1712(r22-science,已进 master)、#1676(r21-A)、#1671(r20-01)——本轨选片必须与它们的修复排重(见 ISSUE_TRIAGE)。
- 并行 r23 轨(A/B/C)各自独立 worktree 与 .planning/zcode-r23-{A,B,C}/ 目录;共享文件唯 tests/CMakeLists.txt(append-only 纪律)。
- push 前:`git fetch origin --prune` + `git diff --name-only origin/master...HEAD` 复核;若 master 前移,对照新 master 上 tests/CMakeLists.txt 注册块(union 合并,不删兄弟轨块)。
