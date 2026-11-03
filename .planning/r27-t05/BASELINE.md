# R27-T05 Baseline

- **BASE_SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa` (= `origin/master` at fetch time)
- **侦察时间 (UTC)**: 2026-11-02T16:25:07Z（fetch 后 `origin/master` 未再移动）
- **Worktree**: `../exp-rs-r27-t05-20261102T163500Z`，分支 `agent/r27-t05-workflow-experiment-teaching-release-20261102T163500Z`
- **gh auth**: WindWang2（owner，token 具 repo/admin:org/workflow scopes）

## 仓库快照

- 纯 C++20 / Qt6 / QGIS / GDAL；CMake ≥3.20；测试 Catch2（`tests/` 1136 文件，`tests/CMakeLists.txt` ~16.5k 行，720 个 add_executable）。
- 构建规范：`-j2` 上限（`CMAKE_BUILD_PARALLEL_LEVEL=2`），宿主互斥锁 `/tmp/exp-rs-host-build.lock`（与并行轨 t01–t04 共享）。
- 近期已合并：#1724–#1731（r25/r26，2026-10-31）——**不重复实现**。
- 2026-11-01 多路审核产出 #1732–#1806（本轨候选 36 条，全部 open，正文引用 HEAD=BASE_SHA）。

## 并行轨（PR 冲突面）

| 轨 | worktree | 领域 |
|---|---|---|
| r27-t01 | exp-rs-r27-t01-20261102T132443Z | science numeric quality（src/operators 等）|
| r27-t02 | exp-rs-r27-t02-20261102T132440Z | data-io runtime reliability |
| r27-t03 | exp-rs-r27-t03-20261102T132444Z | agent-security sdk/plugins（含 src/agent_loop）|
| r27-t04 | exp-rs-r27-t04-20261102T162559Z | qt-qgis workbench ui（src/app/src/gui）|

本轨独占：src/workflow、src/experiment、src/experiment_studio、src/grader、src/teaching、src/teaching_admin、src/verify、src/verification、src/faultlab、scripts、packaging、.github/workflows、tests（本轨新增/维护部分）。

## 关键环境事实

- 本机：16 核 / 62 GB RAM，configure+build 需 flock 串行。
- `tests/CMakeLists.txt` 为共享受保护资源：本轨仅末尾 append-only 块（`# ── BEGIN/END R27-T05 ──` 标识）。
- Linux CI Tier1/Tier2 因 #1735（`INFO << QJsonArray` 编译错误）当前整体编译失败 → 一切测试验证的前置。
