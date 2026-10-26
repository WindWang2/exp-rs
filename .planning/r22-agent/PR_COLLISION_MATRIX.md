# R22-04 (r22-agent) PR_COLLISION_MATRIX

基点：master=911e0b17a3；复核时间 2026-10-26。每次远端变动后重评。

## 与 open PR 的文件交集

| PR | 交集文件 | 冲突评估 |
|---|---|---|
| #1675 (R21-C) | src/agent/mcp_server.cpp | #1675 改 dispatch 区（~939-1190：parameters 类型门、execution_id cancel 映射）。本轨改 StdinReader（~410-470）。**不同 hunk，git 可自动合并**；语义无交集（token 解析 vs 工具分发）。 |
| #1675 | tests/test_mcp_server.cpp | 双方均追加 TEST_CASE；append-only 不同文件尾/不同位置，风险低；merge 前重查。 |
| #1675 | src/plugins/host/plugin_host_session.*、src/workflow/workflow_run_lock.cpp、src/preflight/capability_mirror.cpp、src/cli/*、src/agent_loop/scientific_agent_session.cpp、pi/* | 本轨不改这些文件（规划中 worker_daemon.py 属 src/python/scripts，#1675 未触碰）。 |
| #1673 (R21-B) | tests/CMakeLists.txt | 双方 append 注册块（#1673 加 r21_b_end_to_end）。本轨只追加 test_r22_agent_* 块，物理冲突概率低。 |
| #1674 (R21-E) | src/agent/CMakeLists.txt、tests/CMakeLists.txt | #1674 改 agent/CMakeLists（output_verifier 相关）；本轨不改 src/agent/CMakeLists.txt（workspace_containment.h 为 header-only，无构建注册需求）。tests/CMakeLists 同上 append-only。 |
| #1676/#1677 (R21-A/D) | 无交集（science / qt-cartography 域） | — |

## 兄弟 R22 worktree（无远端分支、无 PR）

r22-data / r22-inference / r22-science / r22-workbench：文件域不重叠（data/inference/science/qt），监控其后续 push。

## merge-loss 守护

- push 前重跑 `gh pr list` + `git fetch origin master`，若 master 前进则 rebase 并重跑受影响测试族。
- #1675 若先合并：本轨 mcp_server.cpp hunk 无依赖（StdinReader 独立）；tests 追加需要语义重排不冲突。
