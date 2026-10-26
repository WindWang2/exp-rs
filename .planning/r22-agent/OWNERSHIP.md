# R22-04 (r22-agent) OWNERSHIP

## 本轨可写（白名单，任务书 §允许写入范围）

- `src/agent/**`（排除 cartography/ layout_tools/ mapspec/ output_verifier.*）
- `src/agent_loop/**`、`src/agent_ops/**`、`src/workflow/**`、`src/plugins/**`
- `src/sdk/exprs/**`、`src/python/isolated/**`、`pi/**`
- `src/cli/**`（仅现有 Agent/Workflow/插件命令）
- `src/jobs/**`
- tests：`tests/test_r22_agent_*`（新增）；对既有 `tests/test_mcp_server.cpp`、`tests/test_exprs_external_process.cpp`、`tests/test_r14_C_external_process_env.cpp`、`tests/test_job_engine.cpp`、`tests/test_python_plugin_host.cpp` 仅 append-only 增补 SECTION/TEST_CASE（先做 hunk 冲突判定）
- `tests/CMakeLists.txt`：仅本轨新测试目标的 append-only 注册块
- `.planning/r22-agent/**`

## 实际计划触碰文件（截至规划时）

| 文件 | Issue | 冲突面 |
|---|---|---|
| src/agent/tool_catalog/workspace_containment.h | #1704 | 无 PR 改动 |
| src/agent/mcp_server.cpp | #1693-1 | #1675 改 939-1190 行；本轨改 ~438-454（StdinReader），不同 hunk |
| src/sdk/exprs/external_process.cpp | #1690 | 无 |
| src/sdk/exprs/plugin_manifest.cpp | #1690 | 无 |
| src/python/isolated/python_plugin_adapter.cpp | #1691 | 无 |
| src/python/isolated/app_interface_bridge.{h,cpp} | #1691(兜底) | 无 |
| src/python/isolated/python_ipc_server.cpp | #1692 | 无 |
| src/python/scripts/worker_daemon.py | #1692 | 无（scripts 在 python 下，任务书白名单为 src/python/isolated/**——daemon 改动以 CROSS_TRACK 记录必要性；若判定越界则只做宿主侧修复） |
| src/jobs/job_types.h、job_engine.{h,cpp} | #1705/#1693-2 | 无 |
| tests/test_mcp_server.cpp 等（append） | 各项 | #1673/#1674/#1675 均有 tests/CMakeLists.txt 追加块——本轨只 append 新块 |

## 只读参考

#1675 diff 全量（/tmp/pr1675.diff）、#1673/#1674 文件清单、.planning/r21-C/*。
跨轨发现一律写 CROSS_TRACK_FINDINGS.md，不跨域编辑。
