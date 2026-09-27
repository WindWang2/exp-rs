# EVIDENCE — Track 10: Workflow Durability R4

All evidence generated on the isolated worktree `exp-rs-workflow-durability-r4`
(`hardening/r4-workflow-durability` @ base `15e5c66b5`), fresh build dir
`build-wf4` (Debug, Ninja, `ENABLE_TESTS=ON`, `CMAKE_BUILD_PARALLEL_LEVEL=2`,
`ninja -j2`, `ctest -j1`, never waited on CI).

## 1. Crash-recovery matrix (≥5 injection points — final count: 7)

| # | Injection point | Mechanism (real process?) | Test case | Result |
|---|---|---|---|---|
| IP-1 | lock held, kill holder | `workflow_crash_helper hold-lock` + SIGKILL (REAL) | `IP-1: SIGKILL of the lock holder releases the flock with no half state` | PASS ×2 |
| IP-2 | mid-node-execution death | `run-block-at` + SIGKILL during executor (REAL) | `IP-2: SIGKILL mid-node commits exactly the journal set...` | PASS ×2 |
| IP-2b | later commit boundary (multi-step lineage) | `run-block-at` 3-step + SIGKILL (REAL) | `IP-2b: kill at a later commit boundary...` | PASS ×2 |
| IP-3 | torn checkpoint (tmp, no rename) | `torn-save` + SIGKILL before rename (REAL) | `IP-3: death between tmp write and rename...` | PASS ×2 |
| IP-4a | hard death pre-commit (output w/o journal commit) | `run-exit-at` → `_Exit(70)` in executor, gated on the previous step's commit (REAL) | `IP-4a: hard death before a step's commit...` | PASS ×2 |
| IP-4b | post-last-commit, pre-finalize | production-writer crafted disk + PRODUCTION recovery/resume path (crafted side, documented in PLAN §WP-B and ADR 0177 §1) | `IP-4b: death after the last commit...` | PASS ×2 |
| IP-5a | cancel propagation, kill mid/late | `cancel-mid` + SIGKILL around cancelRun (REAL) | `IP-5a: death during cancel propagation...` | PASS ×2 |
| IP-5b | death after cancel fully propagated | `cancel-done` → `_Exit(70)` after terminal persist (REAL) | `IP-5b: death after cancel fully propagated...` | PASS ×2 |

Plus the WP-D edge second pass (same file): empty-DAG refusal (the found
wedge defect + fix), single-node DAG archive path, guarded transition table,
missing/readonly lock directory. Light lane: corrupt-checkpoint structured
skip; lock mkpath/readonly cases.

## 2. Lock / cancel / cache case census (≥15)

New files (2):
- `test_workflow_run_lock_r4.cpp` — 5 cases: per-ofd conflict, metadata-garbage ownership,
  publish-fault durability (×2 fault points), tmp-orphan sweep + live-writer protection,
  Cancelling reconcile, corrupt-checkpoint structured skip.
- `test_workflow_crash_recovery_r4.cpp` — 8 cases (matrix above; IP-1 doubles as a lock case).

Extended files (3):
- `test_workflow_cancel.cpp` — +3: coordinator cancel lands Canceled with prefix committed;
  cancel cannot resurrect a terminal run; concurrent resume refused while live, resumes after.
- `test_workflow_incremental_cache.cpp` — +2: upstream re-run strands downstream entry;
  same-revision content-digest rewrite moves the key.
- `test_workflow_cache_e2e.cpp` — +2: cancelled step never seeds the execution cache;
  one run, two persisted views (checkpoint committed set == completed-task view).

Count: 5 + 8 + 3 + 2 + 2 = **20 lock/cancel/cache cases** across **2 new + 3 extended** files
(after the WP-D edge pass: matrix carries 10 cases, light lane 7 — every one countable in the
oracle ctest output under the `test_workflow_crash_recovery_r4::` /
`test_workflow_run_lock_r4::` prefixes).

## 3. Verification runs

### 3.1 Baseline red/green (Phase 0)

- Command: `ctest -R "workflow|durability|run_lock|lowering" -j1` in `build-wf4`
- Master link-graph breakage: 301 test executables unbuildable at `15e5c66b5`
  (one unresolved `sicnu_agent_loop` symbol in `libsicnu_agent.so`; #1335's
  domain). 22 of them sit in this lane; band-aided (tests/CMakeLists.txt only)
  so the lane is measurable. 279 out-of-lane breakages left exactly as master
  has them.
- Baseline (pre-track cases, HOME-isolated): **30/30 passed, 0 failed,
  113.9s** (`baseline-ctest.log`).
- Environment caveat discovered en route: the four `McpServer run_workflow`
  cases accumulate state in the SHARED default checkpoint dir
  (`~/.rs_studio/checkpoints`, lock files are never unlinked by design) and
  degenerate once that directory is polluted — crash-at-exit / hang,
  reproducing on unmodified master binaries with a polluted HOME and passing
  with a fresh one. Not caused by this track (this track's lanes never touch
  the default dir); excluded from the lane runs via `-E "^McpServer"` and
  documented here instead.

### 3.2 This track's union lane, double run (Oracle #1)

- Lane: `ctest -R "workflow|durability|run_lock|lowering" -E "^McpServer" -j1`,
  fresh isolated HOME per session, 43→38+13 = 38 cases (30 baseline + 13
  track cases minus the 4 excluded mcp cases overlap → 38 total, of which 26
  lines per run are the track's 13 cases × 2).
- Run 1: **38/38 passed, exit 0** (`oracle-run1.log`)
- Run 2: **38/38 passed, exit 0** (`oracle-run2.log`)
- Net new failures vs baseline: **0**

Direct-binary verification (Catch2, additionally to ctest):
- `test_workflow_crash_recovery_r4`: 10/10, 106 assertions — twice
- `test_workflow_run_lock_r4`: 7/7, 85 assertions
- `test_workflow_cancel`: 4/4, 34 assertions (re-run after the coordinator fix)
- `test_workflow_incremental_cache`: 8/8, 38 assertions
- `test_workflow_cache_e2e`: 14/14, 319 assertions (pre-existing CLI crash
  E2E untouched and green)

## 4. Honest-coverage boundary

* IP-4b is the only non-real-process window: the death-between-persists window
  (microseconds wide, in-process) is constructed through the production writer
  and resumed through the production recovery path; its real-process siblings
  (IP-4a, IP-5a/b) bracket the same commit logic from both sides.
* Every matrix assertion reconciles against disk truth (raw checkpoint JSON or
  file existence), never against the recovery code's own state.
* Kill timing is content-based (barrier files + settle polls), never sleeps.
