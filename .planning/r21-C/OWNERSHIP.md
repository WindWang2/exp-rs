# R21-C OWNERSHIP — 文件所有权与提交边界

## 本轨写区（任务书 §5）
`pi/**`、`src/agent/**`（排除 `src/agent/cartography/**`、`src/agent/layout_tools/**`）、`src/agent_loop/**`、`src/agent_ops/**`、`src/sdk/**`、`src/workflow/**`、`src/planner/**`、`src/preflight/**`、`src/science_context/**`、`src/plugins/**`、`src/cli/**`（仅 Agent/MCP/workflow/基础参数契约）、`data/processing/algorithm_meta/**`（最小 sidecar 改动）、对应测试、`.planning/r21-C/**`。

## 明确不动
`src/analysis/**`、`src/processing/algorithms/**`、`src/operators/runtime/**`、`src/data/**`、`src/dataset/**`、`src/geospatial/**`、`src/app/**`、`src/gui/**`、`src/agent/cartography/**`、`src/agent/layout_tools/**`、`src/teaching/**`、`src/verification/**`、`src/runtime/**`、`src/python/**`、`src/experiment/**`、`src/native/**`、`src/help/**`、`src/faultlab/**`、`src/lab_pack/**`、`src/explain/**`、`src/contracts/**`、`src/jobs/**`、`src/recipes/**`、`src/grader/**`、`src/verify*/**`、`src/stubs/**`。

## 共享灰区
- 根 `CMakeLists.txt`、`tests/CMakeLists.txt`、`data` 公共注册表、公共 ADR：仅 append-only 最小改动；提交前 `git diff origin/master...HEAD --name-only` 复核。
- `src/cli/**`：本轨只碰参数契约/unknown-flag 拒绝/数值 ok 门/审批标志通道（W-A/W-B/W-D9）；`catalog export` 的数据语义不碰。
- 五轨并行现场：R21-A/B 已在建（BASELINE 记录）；提交前重新 fetch（如网络恢复）+ 重查 open PR。

## 本轨计划触碰的生产文件（初版，随波次更新）
- src/cli/cli_commands.cpp（W-A pipeline/workflow/catalog-export/data leftover；W-B --max-items）
- src/cli/cli_agent_ops_commands.cpp（W-D9 --approve 令牌通道）
- src/plugins/host/plugin_host_session.cpp（D5）、plugins 诊断码族相关（W8-red 视重跑结果）
- src/workflow/workflow_run_lock.cpp、src/preflight/capability_mirror.cpp、src/sdk plugin 读取器（D6）
- 对应 tests/test_cli_*、tests/test_plugin_*、tests/test_workflow_* 等同名测试
