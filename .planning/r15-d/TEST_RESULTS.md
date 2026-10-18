# R15-D Test Results

Environment: Windows 11, MSVC 2022, Ninja, Qt 6.8.0, isolated Debug build
`build-r15-d/`; `CMAKE_BUILD_PARALLEL_LEVEL=2`, tests `-j1`,
`QT_QPA_PLATFORM=offscreen`, PROJ data pinned, vcpkg debug DLLs on PATH.
Pi tests: `node --test pi/test/` — real-spawn suite executed under WSL
(POSIX); native Windows cannot exec the `.mjs` fake server (EFTYPE), so
the Windows suite runs a spawn canary and honestly skips live-child cases.

Driver scripts live outside the repo at `../exp-rs-r15-d-scripts/`
(`build_target.cmd <target>`, `run_test.cmd <binary-or-ctest-regex>`);
logs under `../exp-rs-r15-d-scripts/logs/` (not committed).

## Per-fix evidence

| Fix | RED evidence | GREEN x2 |
|---|---|---|
| F1 Pi failed-start | `pi-bridge-RED.log`: alive===true after spawn ENOENT; handshake hang | `pi/test` 37/37 twice (WSL) incl. CJK byte-length guard |
| F2 MCP EOF sentinel | pipe probe: sentinel emitted on clean EOF | stdio cases in `test_mcp_server`; 5 MiB -> exactly 1 sentinel; clean EOF -> clean shutdown |
| F3 workflow gates/inputs | `run-red-exec-plane.log`, `run-red-recovery.log`: Json::LogicError / silent drop | `test_workflow_execution_plane` 8/8 x2; `test_workflow_recovery` 14/15 x2 (1 pre-existing WorkflowRunLock pid failure) |
| F4 junction escape | all 3 escape assertions failed on unmodified header | junction test 3/3 outside; sandbox suite 4 pass/2 skip/0 fail x3 |
| F5 snapshot leaf coercion | slot:1 -> "1"; resolved:"yes" -> LogicError | `test_agent_loop_seams` 10/10 x2 |
| F6 worker write-gate | gateprobe: ok=0 errors=1 roots=0 oldGate=0 | `test_plugin_capabilities` 14/14 x2 |
| F7 executor isMember | `TC_BND_PAY_01`: REQUIRE_NOTHROW failed — Json::LogicError "requires objectValue or nullValue" (27/28) | `test_agent_ops_task_center_executor` 28/28, 101 asserts, twice |

## Regression suites (post-change)

| Binary | Result | Notes |
|---|---|---|
| test_mcp_server | 30 pass / 0 fail / 2 skip, x5 | skips: $HOME premise + symlink privilege (documented) |
| test_exprs_ipc | 28/28 | |
| test_workflow_durability_13 | 9/9 | |
| test_r14_C_mcp_workflow_cancel | 2/2 cases; process rc 139 at teardown | pre-existing on unmodified binary |
| test_r14_C_plugin_registration | 3/3 | |
| test_agent_ops_core | 47/47 | |
| test_execution_plane_9 | 17/18, 1 opt-in stress skip | |
| test_agent_loop_seams | 10/10 x2 | |
| test_agent_ops_task_center_executor | 28/28 x2 | incl. new TC_BND_PAY_01 |
| test_plugin_capabilities | 14/14 x2 | |
| test_plugin_execution_barrier | 6/6 | |
| test_exprs_plugin_loader | 37/37 | |
| test_plugin_manifest | 11/11 | |
| test_plugin_ui_schema_host | 5/5 | |
| test_plugin_channel_parity_r4 | 3/3 | |
| test_surface_protocol_r4 / test_surface_protocol / test_surface_e2e | 11/11, 10/10, 1/1 | |
| `git diff --check` | clean | |

## Known pre-existing/environment failures (verified on unmodified binaries or unreachable by this diff)

- test_plugins_runtime_host 2/7 — POSIX-only fixtures (/bin/sh, rm -rf /tmp)
- test_plugin_host_allowlist 2/3 — .lnk/allowlist fixture
- test_surface_parity — QProcess startup timeout (environment)
- test_surface_snapshot — stale stored snapshot schema text
- test_workflow_pipeline_ui — Windows ACL/symlink fixture failures
- test_workflow_recovery WorkflowRunLock pid case — 1 case, unrelated
- NOT VERIFIED: hardware/GPU lanes, native-Windows Pi live-child spawn.

## Review-round additions (R1/R2 follow-ups)

| Change | Evidence |
|---|---|
| F8 authority temp-name family | `test_workflow_recovery` 14/15 x2 — new `[tmp-sweep]` case (UINT64_MAX rng sweep, dotted-runId live-lock, committed `.tmp` runId) passes; the 1 failure is the unchanged pre-existing pid case |
| F9 4 MiB boundary + badbit | `test_mcp_server` 30 pass/0 fail/2 skip x2 — new sections: exact-cap+\n delivered, exact-cap@EOF delivered, cap+1 -> exactly one sentinel |
| F10 gates.require/hint | `test_workflow_execution_plane` 8/8 x2 — badCases gains wrong-typed `require:5` / `hint:true` (R2 addition) |
| bridge R1/R2 cleanups | `node --test pi/test/` 37/37 x2 (WSL, LF-synced tree; fixtures src/agent/harness/* + tests/surface_diff_snapshot.json) |
