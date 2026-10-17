## Summary

R15 Track D hardening of the Pi extension -> MCP JSON-RPC -> Tool Catalog
-> SDK/plugins -> Workflow/TaskCenter chain. Existing modules only; no new
framework, no product layer. Ten reproduced defects fixed with
RED -> GREEN x2 evidence; two independent review rounds (read-only
sub-agents, not the implementer).

## Fixes (see .planning/r15-d/ORACLES.md for full evidence)

- **F1** `pi/mcp_bridge.ts`: a failed-start generation (spawn ENOENT,
  hanging initialize) stayed registered as `alive` with a dead child —
  every later request wrote into a destroyed stream with no respawn.
  `discardFailedStart` clears state, rejects pending, kills with SIGKILL
  fallback, counts the fast crash; the launch error is preserved.
- **F2** `src/agent/mcp_server.cpp`: clean stdin EOF emitted a bogus
  `-32700 line too long` (id:null) on every normal shutdown.
- **F3** `src/workflow/workflow_definition.cpp` + `workflow_checkpoint.cpp`:
  non-array/non-object `gates`/`inputs` or wrong-typed `fromStepId`/
  `fromPort`/`toPort` threw `Json::LogicError` or were silently dropped —
  now typed errors naming step+field; checkpoint load converts deep-parser
  exceptions into a typed refusal instead of crashing the recovery sweep.
- **F4** `src/agent/tool_catalog/workspace_containment.h`: Windows
  directory junctions (no privilege needed) escaped the MCP workspace
  sandbox — `QFileInfo::canonicalFilePath()` never resolves them. Now
  `std::filesystem::canonical` on Windows, fail closed.
- **F5** `src/agent_loop/session_seams.cpp`:
  `DataStateSnapshot::fromJson` coerced wrong-typed leaves (`slot:1` ->
  `"1"`) or threw (`asBool` on a string) — typed leaves now validated.
- **F6** `src/sdk/exprs/plugin_capabilities.h` +
  `plugin_host_worker_main.cpp`: worker write-gate went silently OFF when
  the access parse errored (e.g. unexpansible `${workspace}` root) —
  fail-open with writes declared. `writeRootGateActive` fails closed.
- **F7** `src/agent_ops/task_center_executor.cpp`: unguarded
  `isMember("artifacts")` threw `Json::LogicError` on non-object
  resultPayload (missed by merged #1509).
- **F8** `src/workflow/workflow_checkpoint.cpp` (review-found): the
  authority-temp sweep never ran (field-count bug) AND probed the wrong
  run's lock for dotted runIds — could delete a live writer's in-flight
  temp; rng>LLONG_MAX misclassified half of all temps. Shared
  `isAuthorityTempName` also stops staged temps from being listed/loaded
  as checkpoints and keeps committed `checkpoint_x.tmp.json` visible.
- **F9** `src/agent/mcp_server.cpp`: a stdin line of EXACTLY 4 MiB was
  falsely sentinel'd (boundary off-by-one; the Pi outbound cap is the
  same 4 MiB); persistent badbit spun sentinels forever.
- **F10** `src/workflow/workflow_definition.cpp`: `gates.require`/`hint`
  get the same fail-closed leaf checks as `inputs.*`.
- Bridge cleanups (review): shared `terminateChild` kill ladder
  (unref'd SIGKILL everywhere incl. the stale-child respawn path),
  per-generation `stderrTail` reset, inbound desync cap now counts UTF-8
  bytes like the outbound side.

## Dedup / ownership

- #1505/#1509/#1512 were merged before the base; their files treated as
  ground truth — F7 is a NEW site #1509 missed, not a re-fix.
- Open PRs #1513 (geodata), #1514 (science-processing), #1515 (model
  runtime): disjoint file sets; only shared `.gitignore` tail.
- Protected paths untouched: src/app/**, cartography/mapspec/layout_tools,
  src/python/isolated/**, science_context broker/adapter/router, the four
  #1509 agent_ops files (except task_center_executor.cpp which #1509 did
  not change).

## Test evidence (local, `-j2` build / `-j1` tests, offscreen)

Targeted builds clean; every fix's suite ran x2:
`test_mcp_server` 30 pass/2 skip/0 fail x2+; `test_workflow_recovery`
14/15 x2 (1 pre-existing WorkflowRunLock pid failure, unchanged from
baseline); `test_workflow_execution_plane` 8/8 x2;
`test_agent_ops_task_center_executor` 28/28 x2;
`test_plugin_capabilities` 14/14 x2; `test_agent_loop_seams` 10/10 x2;
`test_exprs_ipc` 28/28; `test_workflow_durability_13` 9/9;
`test_execution_plane_9` 17/18+1 opt-in skip;
plugin suites green; `node --test pi/test/` 37/37 x2 (WSL).
`git diff --check` clean. Full matrix: .planning/r15-d/TEST_RESULTS.md

## Independent review

Round 1 (Standards + Spec, read-only sub-agents): 1 P1 (dead/unsafe
authority-temp sweep — became F8), 4 P2, ~10 P3 — all fixed or accepted
with recorded rationale. Round 2: no P0-P2; 3 P3 fixed (stale-child bare
kill -> terminateChild, comment accuracy, test-mirror fidelity); one
documented residual (runId ending `.<digits>.<digits>.<digits>.tmp` is
name-indistinguishable from a staged temp).

## Known limitations (honest, not blocking)

- Native Windows `node --test` cannot exec the `.mjs` fake server
  (EFTYPE/no shebang exec) — live-child Pi coverage is POSIX-verified;
  the Windows suite runs a spawn canary and skips honestly.
- Windows symlink fixtures skip without symlink privilege (junction
  coverage uses `mklink /J`, no privilege needed).
- Pre-existing, reproduced on unmodified binaries: r14-C cancel test
  process segfault at teardown (cases pass, rc 139); WorkflowRunLock
  ownerInfoLine pid case; POSIX-only plugin runtime fixtures.
- NOT VERIFIED: hardware/GPU lanes, non-Windows junction equivalent.
- `/teamwork-preview` and `/goal-loop` are Antigravity commands that do
  not exist in the executing host — the equivalent Oracle-Ledger loop and
  independent sub-agent reviews were used and documented in
  .planning/r15-d/LEDGER.md.

Generated with [Devin](https://devin.ai)
