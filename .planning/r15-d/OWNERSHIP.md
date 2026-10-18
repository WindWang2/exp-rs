# R15-D Ownership

Allowed write set (prompt): `pi/**`, `src/agent/**` (excluding cartography/mapspec/layout_tools), `src/agent_loop/**`, `src/agent_ops/**`, `src/workflow/**`, `src/sdk/**`, `src/plugins/**`.

Files changed by this track (verified against `git diff --stat origin/master...HEAD` at the end):

| Path | Work package |
|---|---|
| pi/mcp_bridge.ts, pi/test/* | D03 |
| src/workflow/workflow_definition.cpp, src/workflow/workflow_checkpoint.cpp | D06 / D07 |
| src/agent/mcp_server.cpp | D02 |
| src/agent/tool_catalog/workspace_containment.h | D05-adjacent (sandbox) / D10 |
| src/agent_loop/session_seams.cpp | D12 / D02-class typed boundary |
| src/sdk/exprs/plugin_capabilities.*, src/plugins/host/plugin_host_worker_main.cpp | D05 |
| tests/** (existing files only) | all |

Protected / not touched: `src/app/**`, `src/agent/{cartography,mapspec,layout_tools}/**`, `src/python/isolated/**` (merged #1505, read-only reference), every file in open PRs #1513/#1514.
