# R18-C Ownership map

Write authorization for `git diff --name-only origin/master...HEAD` on this branch.
Re-run `overlap_scan.py` on every baseline change (Oracle-E).

## Exclusive write zones (this track)
- `pi/**` — Pi adapter shell + MCP bridge + node tests (external TypeScript exemption)
- `src/agent/harness/**`, `src/agent/tool_catalog/**`, `src/agent/contracts/**`, `src/agent/spatial_tools/**`, `src/agent/tools/**`, `src/agent/commands/**`, `src/agent/autonomy/**`
- `src/agent/mcp_server.cpp` / `src/agent/mcp_server.h`
- `src/agent_loop/**`
- `src/workflow/**`
- `src/planner/**`
- `src/repair_planner/**` — EXCEPT `repair_schema.cpp`/`repair_schema.h` (sibling worktree `hardening/planner-repair-capability-graph` owns those)
- `src/science_context/**`
- `src/sdk/exprs/**` (existing tool/protocol contract fixes only)
- `src/agent_ops/**` (task_center_executor seam belongs to the harness loop)
- Tests: `tests/test_mcp_server.cpp`, `tests/test_agent_loop_*.cpp`, `tests/test_agent_tool_catalog.cpp`, `tests/test_agent_workflow_executor.cpp`, `tests/test_r14_C_mcp_workflow_cancel.cpp`, `tests/test_repair_planner_*` (except `_schema`), `tests/test_surface_*.cpp`, `tests/test_harness_error.cpp`, `tests/test_scientific_planner*`; new files use prefix `tests/test_r18_c_*`; `pi/test/**`.

## Read-only / other tracks' zones
- R18-D: `src/agent/{cartography,mapspec,layout_tools,symbology}/**`, `src/app/**`
- R18-E: `src/verification/**`, `src/teaching/**` (note: actual dirs are `src/verify/**`, `src/grader/**`, `src/explain/**` per sibling worktree; treat all as R18-E)
- R18-B: `src/operators/runtime/**`
- R18-A: traditional algorithm sources (`src/analysis/**`, `src/processing/**` algorithm cores)
- PR #1513 zones: `src/geospatial/**`, `src/data/**`, `src/dataset/**`, `src/experiment/**`, `src/cli/**`, `src/lab_pack/**` — read-only; overlaps only via cross-track report
- Sibling worktree `hardening/planner-repair-capability-graph`: `src/preflight/**`, `src/repair_planner/repair_schema.*`, root `CMakeLists.txt`
- Sibling worktree `hardening/verifier-grader-explain-evidence`: `src/verify/**`, `src/grader/**`, `src/explain/**`
- Shared files default read-only: `tests/CMakeLists.txt`, root `CMakeLists.txt`, `src/agent/CMakeLists.txt`, `src/operators/CMakeLists.txt`, public manifests, root README. If a shared CMake registration is unavoidable: prove no alternative, minimal independent block, re-run overlap check before commit, flag separately in the PR body.

## Cross-track issues
Written to `CROSS_TRACK_FINDINGS.md` only; never fixed here.
