# R9 Baseline（2026-10-12 实时查询）

- origin/master HEAD：`8a08642760e2e8ffd2c4b965fc4683c01d3e4758`（= 本分支 BASE_SHA，merge-base 相等）
- 远端分支：仅 `master`；开放 PR：0
- 本 worktree：`C:\Users\wangj.KEVIN\projects\exp-rs-r9-agent-runtime`，分支 `hardening/r9-agent-execution-reliability`，创建时工作树干净
- 开放 Issue：20 个（#1447–#1468 审计簇，非连续）；B 方向相关：#1447 #1448 #1449 #1454 #1455 #1459 #1460 #1462 #1463
- 近期已合并（全部已含于 BASE）：#1469（review wave P0/P1, 2026-10-11）、#1470（agentops/workflow closure）、#1471（runtime boundaries R8）、#1472（science）、#1473（data/experiment）、#1474（ui/teaching）
- PR #1469 有 6 条未解决 review threads（作者 chatgpt-codex-connector，均非 outdated）：
  - WRo: mcp_server.cpp recording 先启动后拒绝 —— **代码已修**（3305166e7，prepare/commit/rollback）
  - WRq: model_runtime.cpp 同 key 并发 load 假拒绝 —— 未修（WP-D）
  - WRu: plugin_registry.cpp 快照无界 —— 未修（WP-C）
  - WRx: ipc_channel.cpp 锁内回放死锁 —— 未修（WP-A）
  - WR2: external_process.cpp 前斜杠 rooted 判定 —— 未修（WP-B）
  - WRz: radiometric_state.cpp 冒号误判 provider URI —— **非 B 方向**（src/core，A 拥有，转交记录）
- PR #1470/#1471：0 review threads。PR #1474：5 条未解决 threads 全在非 B 文件（faultlab/app/teaching_admin）。

## 构建环境

- Windows 10.0.26200 x64，MSVC 2022 Community 14.38.33130（cl.exe x64）+ Ninja（与主 worktree build-dev 一致）
- 本 worktree 独立 build-dev；CMAKE_BUILD_PARALLEL_LEVEL=2；CTEST_PARALLEL_LEVEL=1；QT_QPA_PLATFORM=offscreen

## 审计方法

- Subagent ①（Explore）：GraphQL 拉 #1469/#1470/#1471/#1474 review threads + 逐文件核对 5 个 B1 项
- Subagent ②（Explore）：9 个 Issue × 43 子项逐条对照 master 源码分类 ALREADY_FIXED/REMAINING/OOS
- 主 Agent 复核：每个 WP 开工前亲自读源文件确认结论（不盲信 subagent）
