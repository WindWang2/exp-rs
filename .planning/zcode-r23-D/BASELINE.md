# BASELINE — R23-D（执行时 LIVE 事实）

- 执行时间：2026-10-26/27（UTC 20261026T165151Z 建 worktree）
- 仓库：https://github.com/WindWang2/exp-rs，默认分支 master
- origin/master = `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（= 协议快照值，未漂移；`git fetch origin --prune` 后 `git rev-parse origin/master` 复核）
- master HEAD 与协议基线一致：merge #1677 (r21-d cartography, 2026-10-26 15:12 UTC)
- Open PR = 0；Open Issues = 144；远端仅 master 一支（`gh pr list --state open` 空）
- 本地 worktree：执行时仅主仓一个（master，clean）；无兄弟 agent 在写
- git worktree（本轨）：`C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z`
  - 分支 `agent/r23-d-lab-grading-evidence-20261026T165151Z`，BASE_SHA = HEAD_SHA = `ad98a7a70`
- 工具链（LIVE 探测）：VS2022 Community MSVC 14.38（cl.exe）、Ninja（C:\Qt\Tools\Ninja）、Qt 6.8.0（C:\deps\Qt\6.8.0\msvc2022_64）、vcpkg checkout 恰在 pinned baseline `7f3781e19cc7d4e4882a4caec01668c6f7b5c163`、vcpkg_installed 3.0GB（复制自主仓 build-dev，隔离到本 worktree build-D/）、Node `C:/Program Files/nodejs/node.exe`、winflexbison（C:/deps/winflexbison）
- gh CLI 2.89.0 可用且已认证；Git Bash 无 flock（用 mkdir 锁目录替代串行化；当前无并发编译竞争）
- nproc=16；C: 盘剩余 834GB
- 构建：`build-D`（本 worktree 私有），Debug + ENABLE_TESTS=ON + VCPKG_MANIFEST_INSTALL=OFF（离线复用依赖），`CMAKE_BUILD_PARALLEL_LEVEL=2`

## 最近合并（供对照，PR 号≠合并顺序）

r22：#1708 infra、#1709 inference、#1710 data、#1711 teaching（含 #1694 修复）、#1712 science、#1713 workbench、#1714 agent（tests/CMakeLists.txt 被污染）、#1715 冲突标记修复、#1668（#1704 后续）
r21：#1673 data/runtime、#1674 teaching/verification（#1610–1613 修复）、#1675 MCP、#1676 science、#1677 cartography

## D 轨白名单（协议 §并行所有权）

允许写：src/lab/**, src/lab_pack/**, src/grader/**, src/recipes/**, src/faultlab/**, src/verification/**, src/verify_adapters/**；data/labs/**、data/faultlab/** 最小差分；tests/** 增量注册（共享，append-only）；docs/ 相关文档；.planning/zcode-r23-D/**
禁写：src/teaching/**, src/teaching_admin/**, src/app/**, src/verify/**, src/help/**（C）；src/data/**, src/dataset/**, src/experiment/**, src/geospatial/**（B）；src/analysis/**, src/processing/algorithms/**（A）；src/native/**, src/plugins/**, src/python/**, cmake/**, packaging/**, .github/**（E）；CMakeLists.txt 根、tests/CMakeLists.txt 仅 append-only 最小修改
