# R21-C PR_COLLISION_MATRIX — 提交前冲突核对

## 快照（2026-10-23T19:05Z）
- Open PR：0。活跃同域分支：无新 `agent/r21-*` PR。
- 并行 worktree 现场：R21-A（science）、R21-B（data/model/runtime）在构建；其分支尚未推送（分支列表无 r21 远程分支）。
- 旧 `agent/r20-*` 分支 behind-only（已并入 master），无冲突面。

## 提交前动作（M7）
1. `git fetch origin`（如 github.com 恢复）或 `gh api` 复核 master HEAD == 911e0b17a3 或 rebase 到新 HEAD。
2. `gh pr list --state open` 复核零开放 PR；若出现新 PR 触碰本轨文件，读其 diff 决定 rebase/协调。
3. `git diff origin/master...HEAD --name-only` 100% 在 OWNERSHIP 写区内。
4. 对照 R20 #1670 的 files changed：本轨不得回滚 `ipc_envelope.cpp result 门 / capability 三加载器 / grounding_tools 类型门 / ipc_channel hexdump / ops_driver` 的任何 hunk（反回滚守卫，git log -S 抽查）。
