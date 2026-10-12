# fix(r9): agent execution reliability — IPC replay, program resolution, session coalescing, catalog seam, worker hand-out

Direction-B hardening of the agent/execution runtime, plugin SDK and isolated Python worker. Six work packages, each closed with a failing reproduction **first** and a regression case after. No framework rebuild, no new product surface, no second state center.

## Base / Head

- **base**: `master` @ `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` (no new upstream commits at PR time — `git fetch origin --prune` + `git merge origin/master` reported *Already up to date*)
- **head**: `hardening/r9-agent-execution-reliability` @ `e830e8a0d`
- 7 commits, 33 files, +1823 / −303

## Work packages

| WP | Problem (root cause) | Fix | Regression |
|---|---|---|---|
| A | `IpcChannel::setEventSink` replayed queued events **while holding the state mutex**, so a sink that re-entered any channel API (`close()`, `protocolFailure()`, `request()`) self-deadlocked the non-recursive lock. | Replay moved outside the lock behind an explicit `EventDeliveryPhase{NotInstalled,Draining,Live}` + `mEventOrderCv`; the reader parks newer events until the drain ends. | Re-entrancy case (deadlocks under the old code — caught by the case's own bounded watchdog) + ordering case pinning queued-before-live delivery. |
| B | Windows `argv[0]` resolution ran **twice** (probe vs spawn), so `/tools/worker.exe` was validated at `<wd>/tools/worker.exe` while `CreateProcessW` anchored it on the **drive root**. Bare names also ignored `%PATHEXT%`. | One shared resolver for probe and spawn; PATHEXT order for directly-executable extensions (`.com`/`.exe`); `.cmd`/`.bat`-only hits refused with a typed diagnostic naming the shim. | Rooted anchoring (refused pre-spawn), relative-with-separators anchoring (spawns), `.COM` discovery, typed `.cmd` refusal. |
| C | `PluginRegistry::record()` pushed one **registry-retained** snapshot per call and never evicted → unbounded growth for any polling caller. | Returns a caller-owned `shared_ptr`; the registry retains nothing. The #943 guarantee (snapshot survives the `refresh()` that replaces `mRecords`) still holds. | Bounded-snapshot case (64 queries → gauge 0) + cross-`refresh()` validity case. |
| D | Same-key concurrent model loads each took their **own VRAM admission** → the second was falsely refused "device unavailable" while the identical session was loading; both paid a weight parse. | In-flight table: later same-key acquires ride the loader's outcome (one ticket, one load, typed failure broadcast, no silent retry). A failed device resolution checks for a compatible in-flight load before the eviction valve. | No-false-refusal, single-load, failure-broadcast cases (GPU + CPU). |
| E | `TaskCenter::m_catalog` was a raw pointer; a host that destroyed its `DataManager` without `shutdown()`/`setCatalog(nullptr)` left a dangling seam dereferenced by the lock-free warm/fingerprint snapshots. | `QPointer` + a `destroyed()` guard held as a **connection handle**, so `shutdown()` severs it too (deriving the disconnect from `m_catalog` let a stale catalog clear the *new* seam). | Seam-inert-after-misuse, rebind-live-catalog, and `shutdown → setCatalog(B) → stale destruction` ordering cases. |
| F | `acquireWorker`'s check-then-mark was unsynchronized (two callers could leave with one node) and its wait was a fixed ~1 s spin; the adapter wrapped it in a nested poll event loop. | Scan-and-mark under a node mutex; wait bounded by the caller's own timeout; nested poll loop removed. | Bounded-wait case + 40-round × 4-thread race detector on a single-node pool asserting zero double hand-outs. |

## Evidence — local build & test (MSVC 2022 14.38 + Ninja, Debug, `-j2`, `QT_QPA_PLATFORM=offscreen`)

Each suite below was run **twice** after the final code state; both passes are identical.

| Suite | Result |
|---|---|
| `test_exprs_ipc` | 26 cases / 148 assertions — **PASS** |
| `test_exprs_external_process_win` | 12 cases / 60 assertions — **PASS** |
| `test_exprs_plugin_loader` | 37 cases / 552 assertions — **PASS** |
| `test_model_runtime_stress` | 6 cases / 68 assertions — **PASS** |
| `test_execution_plane_9` (full) | 18 cases — 17 passed, 1 skipped, 0 failed |
| `test_runtime_python_channel_r4` `*1462*` | 2 cases / 48 assertions — **PASS** |
| `test_plugin_lifecycle_halffail_r4` | 9 cases / 69 assertions — **PASS** |
| `test_plugin_channel_parity_r4` | 3 cases / 10 assertions — **PASS** |
| `test_plugin_pollution_gate_r4` | 3 cases / 19 assertions — **PASS** |
| `test_plugin_lifecycle_unload_order_r4` | 4 cases / 34 assertions — **PASS** |

Every WP was verified **RED before GREEN**: the new cases fail on the pre-fix code (WP-A: watchdog-detected deadlock; WP-B: three assertion failures; WP-C: `retainedRecordSnapshots() == 64`; WP-D: three failures) — captured in `.planning/r9-agent-execution-reliability/GOAL_LOOP_LEDGER.md`.

## Independent review — two rounds, reviewer never implemented any patch

- **Round 1**: 0 P0 / **2 P1** / 16 P2. Both P1s were real and are fixed:
  1. WP-D: the corpse-recycle path used a cache iterator taken *before* the coalescing `cv.wait` (which releases the mutex) → `shared_ptr` use-after-free under corpse + concurrent same-key + eviction. Fixed by re-resolving after the wait.
  2. WP-E: `shutdown()` nulled the seam without severing the `destroyed()` guard, so a stale catalog dying after `shutdown()` + `setCatalog(B)` cleared **B's** seam (fingerprints silently degraded). Fixed with a connection handle + a new regression case.
  All actionable P2s were fixed too (corpse gate on the ride path, dead parameter, flight-leak scope guard, redundant lock, missing null check, dead includes/branches, overclaiming comments, unbounded test spins).
- **Round 2 (delta)**: **0 P0 / 0 P1** / 5 P2. One was a defect the WP-C signature change introduced — a range-for bound directly to `record(...)->diagnostics.items()` destroyed the owning temporary and walked freed memory (silently disabling an assertion); fixed with a named holder. The rest are documented follow-ups (TOCTOU note, two coverage gaps) or are recorded in the CHANGELOG.

## Issue / PR / review-thread matrix

Full matrix: `.planning/r9-agent-execution-reliability/PR_ISSUE_REVIEW_MATRIX.md`. Summary of the 20 open issues (#1447–#1468) audited against master:

- **ALREADY_FIXED_IN_MASTER** (verified with file:line evidence, **not redone**): all B-side sub-items of #1447, #1448, #1449, #1454 P1/committer/logBuffer/worker-affinity, all of #1455, all of #1459, #1460 P1 + batch-runner, #1462 P1 + rebind + unknown-method + duplicate-registration + sidecar, all of #1463.
- **Fixed by this PR**: `#1454-5` (WP-E), `#1460-2` (WP-B), `#1462-7` (WP-F).
- **PR #1469 unresolved review threads** addressed here: `WRq` (model-runtime coalescing → WP-D), `WRu` (record snapshots → WP-C), `WRx` (IPC replay → WP-A), `WR2` (rooted path → WP-B). `WRo` was already fixed in master by `3305166e7` (verified, not redone); `WRz` is `src/core/radiometric_state.cpp`, **out of this direction's ownership** — flagged for the owning track, not touched.

## Behavior changes to note

- **PATHEXT resolution order**: with the default `.COM;.EXE;…`, a directory holding both `foo.com` and `foo.exe` now resolves to `foo.com` — the same order `CreateProcessW` itself applies. Disclosed in `CHANGELOG.md`.
- **`.cmd`/`.bat` shims** are refused with a typed diagnostic instead of "not found in PATH" (this spawn contract never invokes a shell).

## Risks / unverified scope

- **Windows-only lanes**: this PR was built and tested on Windows/MSVC only. The Linux and macOS lanes are **NOT_VERIFIED** here (no machine). The changes are platform-neutral except WP-B, which is entirely inside `#ifdef _WIN32`.
- **Pre-existing failures on this host, reproduced on master at the same rate — not regressions**: `test_plugin_host_process` (26/30 fail), `test_plugin_host_boundary_r4` (3/8 fail), `test_plugins_runtime_host` (5/6 fail on master; this branch adds one passing case) — all from the fixture DLL's `LOAD_WITH_ALTERED_SEARCH_PATH` dependency resolution in the test infrastructure; and `test_runtime_python_channel_r4`'s `/bin/sh` fixtures (7/9 fail on master) — this branch's two new cases are cross-platform and pass.
- **Pre-existing flake** `test_execution_plane_9 [ep9][observability]` (`runLocks held: 0`): root cause is a missing happens-before between `WorkflowRunCoordinator::onTaskUpdated`'s `releaseRunOwnership` (worker thread) and `drainPersists()` (main thread). Isolated runs: **master 9/10 pass, this branch 9/10 pass** — same rate, not introduced here; left as a documented follow-up because the #1448/#1450 persistence ordering is a sensitive path outside this wave's scope.
- **Residual (documented in code, not fixed)**: `PythonWorkerProcessPool`'s `isRestarting`/`crashBudgetLeft`/`restartCount` are still written outside the node mutex (signal handlers must stay callback-free under the lock); the header records the residual window and the follow-up options.
- **GPU**: no physical GPU was used. WP-D's coalescing is verified through the existing hardware-pin test seam with a fake provider; **no real-card claim is made**.
- **Not waited on**: no GitHub Actions / CI / CD was created, modified, or waited on. All evidence above is local.

## Files owned / untouched

Touched only direction-B files (`src/sdk/exprs`, `src/operators/runtime`, `src/processing/framework/task_center.*`, `src/python/isolated`, `tests/…` for the suites above, `CHANGELOG.md`, `.planning/r9-agent-execution-reliability/`). No changes to `src/core/**`, root CMake, `tests/CMakeLists.txt`, or any A/C-direction file. `src/geospatial/**` was not touched.
