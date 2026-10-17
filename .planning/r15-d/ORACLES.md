# R15-D Oracles — defect list with RED->GREEN evidence

Each entry: old failing input, exact symptom (file/assert), new expectation,
evidence. All RED/GREEN logs live OUTSIDE the repo under
`projects/exp-rs-r15-d-scripts/logs/` (not committed).

## F1 (D03, P1) Pi bridge reports failed-start generation as alive

- RED: launch with a missing binary -> `bridge.alive() === true`, dead
  `child` retained, subsequent `call()` failed "MCP write failed" instead of
  retrying startup; a hanging `initialize` handshake never timed out.
- Fix (`pi/mcp_bridge.ts`): `discardFailedStart` clears child state, marks
  exited, rejects pending requests, kills the process (SIGKILL fallback),
  counts the fast crash; `startError ??=` preserves the launch error;
  startup timeout injectable. The outbound `Buffer.byteLength` guard and
  its CJK discriminating test are R14-C work (present at merge-base) —
  this track re-verified them, it does not claim them.
- GREEN: `node --test pi/test/` 37/37 twice under WSL (real spawn);
  the pre-existing CJK test `"地".repeat(3_000_000)` keeps the R14-C
  byte-length guard honest.
- LIMIT: native Windows `node --test` cannot exec the `.mjs` fake server
  (EFTYPE, no shebang) — live-child cases verified POSIX-only; the Windows
  suite runs a spawn canary and skips honestly.

## F2 (D02, P1) MCP stdin EOF reported as "line too long"

- RED: closing stdin after a complete newline-terminated frame entered the
  `fail()` branch and emitted the `__MCP_LINE_TOO_LONG__` sentinel on clean
  shutdown.
- Fix (`src/agent/mcp_server.cpp`): `eof() && gcount()==0` breaks before the
  overlong sentinel; overlong input still emits exactly one sentinel; a
  final unterminated frame is still delivered.
- GREEN: new `[stdio]` Catch2 cases in `tests/test_mcp_server.cpp`;
  empirical pipe probes (5 MiB line -> exactly one sentinel; CRLF ok;
  clean EOF -> clean shutdown).

## F3 (D06/D07, P1) Malformed workflow `gates`/`inputs` threw or were silently dropped

- RED: `gates: "x"` (non-array) / `inputs: [7]` (non-object element) /
  `fromStepId: 3` (non-string) either threw `Json::LogicError` out of
  `WorkflowDefinition::fromJson` or were silently ignored — no typed
  refusal. `WorkflowRun::fromJson` exceptions escaped
  `workflow_checkpoint` load.
- Fix (`workflow_definition.cpp`): present non-null `gates`/`inputs` must be
  arrays of objects; `fromStepId`/`fromPort`/`toPort` must be strings;
  errors name the step and field, return false. `workflow_checkpoint.cpp`
  wraps `WorkflowRun::fromJson` in try/catch -> typed checkpoint load failure.
- GREEN: `test_workflow_execution_plane` 8/8 x2; `test_workflow_recovery`
  13/14 x2 (1 pre-existing unrelated `WorkflowRunLock` pid failure).

## F4 (D05-adjacent / D10, P0-adjacent security) Windows junction escapes workspace sandbox

- RED: `mklink /J` junction inside workspace pointing outside —
  `surfacePathOutsideWorkspace()` returned false for the junction itself,
  an existing file through it, and a not-yet-existing tail (3/3 escape).
  `QFileInfo::canonicalFilePath()`/`isSymLink()` do not resolve junctions.
- Fix (`workspace_containment.h`): `canonicalExistingPath()` uses
  `std::filesystem::canonical` on Windows (resolves all reparse points,
  fail closed on error, normalizes `\?\`/`\?\UNC\`); non-Windows keeps
  Qt canonicalization.
- GREEN: junction regression test 3/3 escape assertions now report OUTSIDE;
  sandbox suite 4 pass / 2 honest skips / 0 fail, three full-binary runs.
- LIMIT: true-symlink fixture skipped on Windows without symlink privilege
  (`QFile::link` produced a `.lnk` shortcut) — documented skip, not hidden.

## F5 (D02/D12, P1) `DataStateSnapshot::fromJson` coerced malformed leaves

- RED: `slot: 1` (number) silently coerced to `"1"`; `resolved: "yes"`
  threw `Json::LogicError` from `asBool` — violating the optional+error
  contract.
- Fix (`session_seams.cpp`): every present typed leaf is checked before
  read; wrong types return a typed error naming the leaf; `facts`/`summary`
  stay free-form.
- GREEN: `test_agent_loop_seams` 10/10 twice.

## F6 (D05, P1) Plugin worker write-gate fail-open on access parse error

- RED: manifest declaring a write root that fails to expand in the worker
  (`${workspace}` unresolvable) -> parse errors + zero roots ->
  `policy.active = ok() && !roots.empty()` = false -> gate OFF with writes
  declared (scratch probe: `ok=0 errors=1 roots=0 gate=0`).
- Fix (`plugin_capabilities.h`): `writeRootGateActive()` = `!ok() ||
  !fsWriteRoots.empty()`; used by `plugin_host_worker_main.cpp`.
- GREEN: `test_plugin_capabilities` 14/14 x2 covering null/resolvable/
  unresolvable/non-write-error/read-only cases; `exprs_plugin_host_worker`
  relinks; plugin suites pass (2 pre-existing POSIX-only env failures in
  `test_plugins_runtime_host`, `test_plugin_host_allowlist` — change
  unreachable in those paths).

## F7 (D08-adjacent, P1) Unguarded `isMember` on non-object resultPayload

- RED: adapter completes a task with scalar `resultPayload` ->
  `TaskCenterExecutor::poll` threw `Json::LogicError`
  (RED run: "requires objectValue or nullValue", 27/28).
- Fix (`task_center_executor.cpp`): `isObject()` guard matching every other
  resultPayload reader.
- GREEN: `test_agent_ops_task_center_executor` 28/28, 101 asserts, twice.

## Audited, no defect found on current master (regression evidence only)

- D01 call chain Pi -> MCP -> Tool Catalog -> SDK/plugins -> workflow/
  TaskCenter: single bridge (`pi/mcp_bridge.ts`), no second transport.
- D02 handshake gate, explicit-null `id` vs notification, unknown-method
  typed refusal, bounded artifact chunks, 4 MiB line cap: verified.
- D04 tool IDs/dup registration: audited, no reproduction.
- D09 IPC bounded queue/backpressure: `test_exprs_ipc` 28/28.
- test_r14_C_mcp_workflow_cancel 2/2 passes but the PROCESS segfaults at
  teardown (rc 139) — reproduced on unmodified binary, pre-existing.

## F8 (D07, P1, found by independent review R1) authority temp-name family broken three ways

- RED (review-found, pre-existing "R6" code — verified by reading the parse
  against the producer `durable_sidecar.cpp` "<stem>.<pid>.<ctr>.<rng>.tmp<ext>"):
  (a) the `authorityTemps` recovery sweep counted "checkpoint_" as a field,
  so a dot-free runId yielded 4 fields < the required 5 — the sweep NEVER
  ran; (b) a dotted runId "a.b" probed the lock of run "b" — the WRONG run —
  and could delete a live writer's in-flight temp; (c) rng was checked with
  toLongLong which fails above LLONG_MAX, misclassifying ~half of real
  temps (mt19937_64 is uint64).
- Secondary: `listCheckpoints`' `*.json` glob admitted staged temps
  (recovery could double-load a run: temp + committed predecessor carry the
  same document runId), and the election's `.tmp.json` suffix exclusion
  hid a COMMITTED "checkpoint_x.tmp.json" (runId literally ending ".tmp").
- Fix (`workflow_checkpoint.cpp`): shared `isAuthorityTempName()` — strips
  the checkpoint_ prefix AND .tmp.json suffix, requires >=4 dot fields with
  the last three all-decimal-digits (pid non-zero); used by the sweep (with
  live-owner lock probe), the election exclusion and listCheckpoints.
- GREEN: new `[tmp-sweep]` case in `test_workflow_recovery.cpp` covers
  dead-run sweep (UINT64_MAX rng), dotted-runId live-lock preservation,
  committed ".tmp"-runId visibility, and post-release sweep.
- Residual (documented): a committed runId ending
  ".<digits>.<digits>.<digits>.tmp" is name-indistinguishable from a staged
  temp of the dotted prefix run — noted in the code comment.

## F9 (D02, P2, review R1) 4 MiB boundary line falsely rejected + badbit spin

- RED: `istream::getline(buf, kMaxMcpLine+1)` sets failbit when it fills the
  buffer without seeing the delimiter — so a line of EXACTLY 4 MiB + '
'
  (the largest legal line; the Pi bridge outbound cap is the same 4 MiB)
  was answered with the -32700 id:null sentinel. A persistent badbit also
  spun the loop emitting sentinels forever.
- Fix (`mcp_server.cpp`): when gcount()==kMaxMcpLine, clear() then peek():
  next=='
' consumes it and delivers the line; next==EOF delivers and
  stops; anything else is genuinely overlong. bad() breaks the loop.
- GREEN: new [stdio] sections pin exact-cap-then-newline, exact-cap-at-EOF,
  and cap+1 overlong (exactly one sentinel, service continues).

## F10 (D06, P2, review R1) gates.require/hint wrong-typed leaves silently dropped

- F3 made inputs.* leaves fail closed but left gates[].require/hint on the
  old silent-drop read — asymmetric strictness in the same hunk. Both are
  now typed errors naming step and field.
