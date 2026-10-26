# R22-04 (r22-agent) BASELINE

- 采集时间：2026-10-26（UTC 会话内实时命令，非快照推断）
- 仓库：WindWang2/exp-rs，viewerPermission=ADMIN，defaultBranch=master
- `git fetch origin master --prune` 成功；`origin/master = 911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（与任务书已知基线一致，`gh api` 未再复检 compare——fetch 已返回同 SHA）
- 本地 master = 28e1891390，为 origin/master 的祖先（behind 59），不可作为基线；本轨从 origin/master 建 worktree。
- Worktree：`/home/kevin/project/exp-rs-r22-agent-20261025T164309Z`，分支 `agent/r22-agent-20261025T164309Z`（唯一本轨 worktree）。
- Open PR（5，全为 R21）：#1673(B data/model-runtime) #1674(E teaching/verification) #1675(C pi-mcp-harness) #1676(A science) #1677(D qt-cartography)。R20 PR #1668-#1672 已全部合并进 master。
- Open issues 中 agent lane：#1690 #1691 #1692 #1693 #1704 #1705（详见 ISSUE_TRIAGE.md）。
- 兄弟 R22 worktree（data/inference/science/workbench）已存在但尚无对应 open PR；无远端分支冲突。

## 构建

- 复用 R21-C 配方（CMakeCache 实测值）：Debug + ENABLE_TESTS=ON + CMAKE_PREFIX_PATH=vendor prefix(paleo-singlefactor-closure, PROJ/GEOS) + Qt6=/usr/lib/cmake/Qt6 + SICNU_VENDOR_GDAL=OFF 等。
- 构建目录：本 worktree 内 `build-r22-agent`（独占）。共享锁 `/tmp/exp-rs-r21-build.lock`（flock，与 R21/R22 各轨互斥）。`CMAKE_BUILD_PARALLEL_LEVEL=4` 硬上限，ctest `-j1`。
- configure log：/tmp/r22a-configure.log
