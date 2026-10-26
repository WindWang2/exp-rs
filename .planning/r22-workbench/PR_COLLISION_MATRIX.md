# R22-05 PR COLLISION MATRIX（每次远端变动后重估）

基线 911e0b17a3 上的 open PR 文件交集（gh pr list --json files，2026-10-25 盘点）：

| PR | 与本轨白名单的交集 | 处置 |
|---|---|---|
| **#1677 (R21-D)** | `src/app/main_window*.{cpp,h}`、`src/app/main_window_vector.cpp`、`src/app/qgis_app_facade.*`、`src/app/qgsattributetabledialog.cpp`、`src/app/qgsguivectorlayertools.cpp`、`src/agent/cartography/*`（4 文件）、`src/agent/layout_tools/*`（3 文件）、`src/agent/mapspec/mapspec_compiler.cpp`、`tests/test_cartography_production_11.cpp`、`tests/test_layout_tools.cpp`、`tests/test_r14_D_vector_edit_honesty.cpp`、`docs/cartography/mapspec-reference.md`、`docs/USER_GUIDE.md`、`.gitignore` | **本轨对这些文件一律只读**。E1 双窗口测试与 E4 覆盖恢复依赖 #1677 符号 → BLOCKED_ON_#1677（等合并后接手）。E4 剩余可做部分只落在本轨独占文件（见 TASKS） |
| #1676 (R21-A) | 无直接交集（science 数值域） | 只读 |
| #1675 (R21-C) | 无直接交集（Pi/MCP/workflow 域） | 只读 |
| #1674 (R21-E) | 无直接交集（teaching/verification 域） | 只读 |
| #1673 (R21-B) | 无直接交集（data/model-runtime 域） | 只读 |
| R22 兄弟轨（data/inference/science，本地 worktree） | 无交集（各自域） | 只读 |

## Push 前复查点

1. 重新 `gh pr list --state open --json files` 对照本轨 diff 文件列表逐文件核对。
2. 若 #1677 在本轨 push 前合并：重估 E1/E4 BLOCKED 项是否转化为本轨可实施（但本 PR 不强制吸收，留后续波次）。
3. `git fetch origin && git rev-parse origin/master` 确认基线未移动；若移动，评估 rebase 需求。

## Push 前复查（2026-10-26 12:0x，LIVE）

- `gh api commits/master` = `911e0b17a3...`（未移动；本地 fetch 一次 SSL 瞬断，按任务书以 API 为准并已记录）。
- **#1673-#1676 已被关闭（未合并）**；#1677 仍 open（本轨 BLOCKED 项维持）。
- 新 open：#1708/#1709/#1710/#1711/#1712（R22 infra/inference/data/teaching/science）。
- 逐文件比对（GitHub files API vs 本轨 24 个非 planning 文件）：与全部 10 个 open PR **零交集**（含 tests/CMakeLists.txt——5 个 R22 PR 均未触碰该文件）。
