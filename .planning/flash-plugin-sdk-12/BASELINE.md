# BASELINE.md — flash-plugin-sdk-12

- 实时 master SHA: `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (docs(agents): record Platform 5.0 audit request), 2026-09-20 fetch 确认 origin/master 未漂移。
- 本地 master checkout 在 adf8f9895 (与 origin 一致)。
- 本 Track worktree: `../exp-rs-worktrees/flash-plugin-sdk-12`, branch `agent/flash-plugin-sdk-12`, HEAD=adf8f9895。

## PR 实时状态 (2026-09-20, gh)

- **open PR = 0**
- 最近 merged PR 抽样：#1115 fix deep-review wave-2 (#1097)、#1113 ops/workflow/agent (#1076-#1095)、#1112 app、#1111 packaging/processing、#1110 geospatial、#1109 agent symbology uaf、#1103 fix(plugin): harden worker trust boundary, IPC lifetime and typed JSON reads。
- 与本 Track 直接相关的历史 PR：
  - **#1103** (`fix/r2-plugin-sdk-trust` → master `d2a723ee2`): worker trust boundary / IPC lifetime / typed JSON。本 Track 在其契约上增量，不重造。
  - #1036/#1039/#1040/#1041（plugins）均已在 #1103/#1113 关闭。

## Issues 实时状态

- **open issue = 0**
- closed 中与本 Track 相关：#1036 (pipe fds leaked), #1039 (worker UI schema reaches widget build unchecked), #1040 (ui.invoke nesting), #1041 (POSIX child exit), #1080 (operator proxy retry off-by-one), #1095 (restart-timer UAF; SHM geometry keys)。

## 远端 agent/* fix/* 分支判定 (只读，不作基线)

| branch | 上游 PR | 判定 |
|---|---|---|
| agent/glm53-plugin-sdk-trust (71e7f9e97) | #1103 的另一实现 | **已 supersede**：cherry 显示 7 commits 全为 `+`，但 #1103 从 fix/r2-plugin-sdk-trust squash 合入 master `d2a723ee2`，同类问题已修。取历史证据。 |
| agent/glm53-desktop-lifecycle (a0db9f8323) | app #1037–#1056 批次 | **已 supersede**：app 侧 lifetime/georef 问题由 #1101/#1102/#1112 修复合入。 |
| agent/flash-* (data-transaction/geo-fabric/lab-foundry/mcp-containment/processing/workflow) | #1104/#1105/#1107/#1110/#1113 系列 | **历史残留**（fix/r2-* PR 已合入），不作基线。 |
| fix/ci-master-unblock, fix/r2-ci-protobuf-multimode, fix/review-issues-1033-1056 | #1108/#1076 batch | 残留。 |

## 测试资产（已存在）

- tests/test_plugin_manifest.cpp、test_plugin_capabilities.cpp、test_plugin_host.cpp、test_plugin_host_process.cpp、test_plugin_ui_schema.cpp、test_plugin_ui_schema_host.cpp、test_plugin_ui_placement.cpp、test_plugins_runtime_host.cpp、test_plugin_capabilities.cpp、test_plugin_execution_barrier.cpp、test_exprs_plugin_system.cpp、test_exprs_plugin_loader.cpp、test_python_plugin_*.cpp。
- 已覆盖 hostile：crash + bounded restart、hang→kill ladder、oversized response frame cap、concurrent requests/cancel、process-group grandchildren cleanup、worker-side workDir policy、ui.invoke E6010 host validation、deep schema refusal。
