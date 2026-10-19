# (title pending)

Generated: 2026-10-19T03:16:47Z

## Baseline

- base `origin/master` @ `a60c8831c9cc4e64701d17bde2ace67a74a9b7a0` (Merge pull request #1513 from WindWang2/hardening/r15-B-geodata-persistence)
- head `HEAD` @ `c9a3fd6aa4d7cedcd5b9a81e96c4f4a67b56e404` (docs(r18-c): planning evidence, ledger, test matrix, and two independent review records)
- commits in range: 12

## Scope

- sources (8): `src/agent/mcp_server.cpp`, `src/agent/tool_catalog/surface_registry.cpp`, `src/agent_loop/session_seams.cpp`, `src/agent_ops/task_center_executor.cpp`, `src/plugins/host/plugin_host_worker_main.cpp`, `src/workflow/workflow_checkpoint.cpp`, `src/workflow/workflow_definition.cpp`, `src/workflow/workflow_run_coordinator.cpp`
- public_headers (3): `src/agent/tool_catalog/surface_progress.h`, `src/agent/tool_catalog/workspace_containment.h`, `src/sdk/exprs/plugin_capabilities.h`
- tests (13): `tests/CMakeLists.txt`, `tests/test_agent_loop_seams.cpp`, `tests/test_agent_ops_task_center_executor.cpp`, `tests/test_mcp_server.cpp`, `tests/test_plugin_capabilities.cpp`, `tests/test_r14_C_mcp_workflow_cancel.cpp`, `tests/test_r18_c_progress_units.cpp`, `tests/test_r18_c_rpc_cancel_id_namespace.cpp`, `tests/test_r18_c_surface_pagination.cpp`, `tests/test_surface_parity.cpp`, `tests/test_surface_protocol.cpp`, `tests/test_workflow_execution_plane.cpp` …
- docs (12): `.planning/r18-c-agent/BASELINE.md`, `.planning/r18-c-agent/CROSS_TRACK_FINDINGS.md`, `.planning/r18-c-agent/DECISIONS.md`, `.planning/r18-c-agent/FINAL_REPORT.md`, `.planning/r18-c-agent/LOOP_LEDGER.md`, `.planning/r18-c-agent/ORACLES.md`, `.planning/r18-c-agent/OWNERSHIP.md`, `.planning/r18-c-agent/PERFORMANCE.md`, `.planning/r18-c-agent/REVIEW_R1.md`, `.planning/r18-c-agent/REVIEW_R2.md`, `.planning/r18-c-agent/TASKS.md`, `.planning/r18-c-agent/TEST_RESULTS.md`
- other (8): `.gitignore`, `pi/mcp_bridge.ts`, `pi/test/bridge_parity.test.mjs`, `pi/test/fake_mcp_server.mjs`, `pi/test/mcp_bridge.test.mjs`, `pi/test/no_drift.test.mjs`, `pi/test/r18_c_id_null_error_fanout.test.mjs`, `pi/test/r18_c_timeout_cancels_server_side.test.mjs`

## Non-goals

<!-- TODO: what this PR deliberately does not do -->

## Design

<!-- TODO: design notes and alternatives rejected -->

## Issue mapping

<!-- TODO: issue numbers this PR closes or relates to -->

## Tests

<!-- TODO: exact commands and their output, run twice -->

## Resources

<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->

## Review disposition

<!-- TODO: reviewer findings and their fix state -->

## Known limitations

<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->

## Conflict hotspots

overlap evidence for this diff's 44 paths:
- open_prs: 1
  - `agent/r18-b-model-runtime-perf` touches 2 of these paths
- merged_prs: 11
  - `hardening/r16-d-workbench-cartography` touches 1 of these paths
  - `hardening/r16-e-help-delivery` touches 1 of these paths
  - `hardening/r16-a-evidence-verification` touches 1 of these paths
  - `hardening/r16-c-teaching-classroom` touches 2 of these paths
  - `hardening/r16-b-scientific-decisions` touches 2 of these paths
  - `hardening/r15-E-desktop-teaching-help` touches 21 of these paths
  - `hardening/r15-D-pi-mcp-workflow` touches 21 of these paths
  - `hardening/r15-C-model-runtime` touches 2 of these paths
  - `hardening/r15-A-science-processing` touches 1 of these paths
  - `hardening/r15-B-geodata-persistence` touches 1 of these paths
  - `hardening/r14-D-qt-qgis-cartography` touches 1 of these paths
- remote_branches: 1
  - `origin/agent/r18-b-model-runtime-perf` touches 2 of these paths
- sibling_worktrees: 1
  - `refs/heads/agent/r18-e-teaching-evidence` touches 1 of these paths

---

## Diff stat

```
.gitignore                                         |   2 +
 .planning/r18-c-agent/BASELINE.md                  |  40 ++++
 .planning/r18-c-agent/CROSS_TRACK_FINDINGS.md      |  12 +
 .planning/r18-c-agent/DECISIONS.md                 |  31 +++
 .planning/r18-c-agent/FINAL_REPORT.md              |  33 +++
 .planning/r18-c-agent/LOOP_LEDGER.md               |  68 ++++++
 .planning/r18-c-agent/ORACLES.md                   |  15 ++
 .planning/r18-c-agent/OWNERSHIP.md                 |  30 +++
 .planning/r18-c-agent/PERFORMANCE.md               |   8 +
 .planning/r18-c-agent/REVIEW_R1.md                 |  24 ++
 .planning/r18-c-agent/REVIEW_R2.md                 |  21 ++
 .planning/r18-c-agent/TASKS.md                     |  34 +++
 .planning/r18-c-agent/TEST_RESULTS.md              |  47 ++++
 pi/mcp_bridge.ts                                   | 138 ++++++++----
 pi/test/bridge_parity.test.mjs                     |  19 +-
 pi/test/fake_mcp_server.mjs                        |  29 +++
 pi/test/mcp_bridge.test.mjs                        | 104 +++++++++
 pi/test/no_drift.test.mjs                          |   4 +-
 pi/test/r18_c_id_null_error_fanout.test.mjs        |  51 +++++
 pi/test/r18_c_timeout_cancels_server_side.test.mjs |  62 ++++++
 src/agent/mcp_server.cpp                           | 190 +++++++++++++---
 src/agent/tool_catalog/surface_progress.h          |  21 +-
 src/agent/tool_catalog/surface_registry.cpp        |  15 +-
 src/agent/tool_catalog/workspace_containment.h     |  36 ++-
 src/agent_loop/session_seams.cpp                   |  34 ++-
 src/agent_ops/task_center_executor.cpp             |   3 +-
 src/plugins/host/plugin_host_worker_main.cpp       |   5 +-
 src/sdk/exprs/plugin_capabilities.h                |  12 +
 src/workflow/workflow_checkpoint.cpp               |  93 ++++++--
 src/workflow/workflow_definition.cpp               |  45 +++-
 src/workflow/workflow_run_coordinator.cpp          |  11 +-
 tests/CMakeLists.txt                               | 102 +++++++++
 tests/test_agent_loop_seams.cpp                    |  67 ++++++
 tests/test_agent_ops_task_center_executor.cpp      |  25 +++
 tests/test_mcp_server.cpp                          | 210 +++++++++++++++++-
 tests/test_plugin_capabilities.cpp                 |  76 +++++++
 tests/test_r14_C_mcp_workflow_cancel.cpp           |   4 +-
 tests/test_r18_c_progress_units.cpp                | 236 ++++++++++++++++++++
 tests/test_r18_c_rpc_cancel_id_namespace.cpp       | 245 +++++++++++++++++++++
 tests/test_r18_c_surface_pagination.cpp            | 229 +++++++++++++++++++
 tests/test_surface_parity.cpp                      |  19 +-
 tests/test_surface_protocol.cpp                    |  20 +-
 tests/test_workflow_execution_plane.cpp            | 114 ++++++++++
 tests/test_workflow_recovery.cpp                   | 116 ++++++++++
 44 files changed, 2565 insertions(+), 135 deletions(-)
```

## Commits

```
c9a3fd6aa docs(r18-c): planning evidence, ledger, test matrix, and two independent review records
e3a2d4c9c fix(mcp): review R2 follow-ups — injective cancel keys, search_algorithms cursor, malformed-registration parity
7598a5d80 fix(mcp): review R1 — catalog branch keeps rich metadata; typed cursors everywhere (R18-C)
0adf9ee10 fix(workflow): surface a failed resume ghost-checkpoint unlink (R18-C C04)
b080ff13b fix(mcp): keep numeric and string JSON-RPC ids disjoint in cancel bookkeeping (R18-C C05-b)
dc865ae1e fix(mcp): tools/list cursor typing + get_tool_schema projection authority (R18-C C01)
5d8794096 fix(mcp): relay TaskCenter progress at its contract scale (0..1 fraction)
1a38b7e43 fix(mcp): restore #1516 StdinReader framing fixes (completes the C00 restoration)
f3223a925 fix(mcp): one discovery authority — normalized schemas across surfaces (R18-C C01)
bb7a92ab6 test(pi): pin id:null error-frame fan-out and recovery (R18-C C09)
d19135405 fix(pi): a request timeout must cancel server-side work + restore #1516 bridge content
569ad947e fix(agent,workflow,plugins): restore #1516 fixes silently reverted by merge 29596c913
```
