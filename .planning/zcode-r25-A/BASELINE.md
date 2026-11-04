# R25-A BASELINE — 执行时实时侦察记录

- 侦察时间：2026-10-28 14:03 UTC 起
- `origin/master = 2610cb4741e69a89124921e44c03e340e2522ee2`（与任务书快照一致，`git fetch origin --prune` 后确认无前进）
- Open PR：**0**（`gh pr list --state open` 为空）
- Open Issues：144（`gh issue list --limit 300`）
- 遗留远程分支 8 条（agent/r23*×5、agent/r24*×3），全部 `rev-list --count origin/master..origin/$b = 0`（已完全合并，无冲突面）
- gh CLI 可用，账号 WindWang2，token scopes 含 repo/workflow
- 本轨 worktree：`../exp-rs-r25-A-20261028T140339Z`；分支 `agent/r25-A-science-processing-20261028T140339Z`；BASE_SHA 见上
- 源 worktree 干净（仅未跟踪 `.workbuddy/`，未触碰）

## 构建纪律（自 CLAUDE.md）
- `-j2` 上限（`CMAKE_BUILD_PARALLEL_LEVEL=2`）；禁 `-j$(nproc)`；共享锁 `flock $HOME/.cache/exp-rs-build.lock`
- `ctest -j1`；`QT_QPA_PLATFORM=offscreen`
- 构建目录：worktree 专属 `build-r25-A`

## 最近合并的科学轨历史（避免重修）
- PR #1716 (R24 wave1)、#1717、#1718 (R24-A science)、#1719 (R24-B)、#1720 (R24-C)、#1721 (R23-C)、#1722 (R23-A science)、#1723 (R23-B)
- #1718 修复：负反射率、viewshed 亚米格环、D8 平地、时序重复日期、SAR 角域（经代码复核确认）
- #1722 修复：曲率、波长、极点、MNF、VCA、dNBR legacy（经代码复核确认）
