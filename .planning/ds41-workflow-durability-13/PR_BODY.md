# Workflow Durability & Thread-Safety 13.0 — artifact identity, run-id grammar, coordinator affinity, recovery stress

Branch `agent/ds41-workflow-durability-13`, base `master`. Baseline SHA: **`79adfe78a16b9419eef180cf9e6e5739658621a2`** (origin/master at worktree creation, re-fetched before PR).

## Dedup (live pre-read)

`git fetch origin --prune` at start; open PRs at the time: #1135 (temporal phenology), #1136 (offline labs), #1137 (geospatial maintenance), #1138 (plugin SDK lifecycle) — none touches the workflow/durability domain. The direct predecessor **#1132 (flash-workflow-engine-12)** merged the durable-IR2 work and documented the four limitations this track closes; they were re-verified by reading master's code, not assumed. `overlap_scan.py` before the PR: the only file-level overlaps with open PRs are `.gitignore` (append-only planning allow-list, 3 PRs) and the tail of `tests/CMakeLists.txt` (append-only test blocks, 2 PRs) — both textual, both far from this branch's hunks. No re-implementation of anything in flight.

## Scope

**In (workflow/checkpoint/provenance/coordinator + their tests):**
- `src/workflow/pipeline_run_coordinator.{h,cpp}` — artifact identity modes, cancel-aware whole-file hashing, affinity-marshalled mutators, destructor drain, D17 provenance attempt layout, publish fault points.
- `src/workflow/workflow_run.{h,cpp}` — lineage envelope (`attempt`, `resumeOf`), serialization version 2 with legacy-1 acceptance.
- `src/workflow/workflow_checkpoint.{h,cpp}` — content-aware checkpoint election.
- `src/workflow/workflow_run_coordinator.{h,cpp}` — two lineage seams only (resumeOf stamping, attempt increment).
- Tests: extended `tests/test_workflow_checkpoint_cache.cpp` (D17 lane), new light lane `tests/test_workflow_durability_13.cpp` (Engine-2.0, no qgis chain).
- `docs/adr/0166-workflow-durability-13.md`, `.planning/ds41-workflow-durability-13/`, append-only `tests/CMakeLists.txt` / `.gitignore`.

**Non-goals:** Mission Workbench, TaskCenter scheduler internals, model runtime, temporal algorithms, GUI (all read-only). No rebuild of the heavy qgis/task_center chain in this track.

## Design (full rationale in ADR 0166 and `.planning/ds41-workflow-durability-13/DECISIONS.md`)

1. **Artifact identity.** `sha256fl` (SHA-256 over `[8B LE size][first ≤1 MiB][last ≤1 MiB]`) stays byte-compatible; `sha256full` streams the whole file in 1 MiB chunks with a per-chunk cancel poll. `ArtifactIdentityMode {Fast, Full, Auto}`; **Auto is the default** and chooses Full exactly when the artifact exceeds the fast window — precisely where `sha256fl` has a blind spot. The recorded tag selects the resume verification algorithm, so legacy and mixed checkpoints need no migration and unknown tags fail closed. A Merkle tree was rejected as a larger format closing no additional hole.
2. **Run-id grammar / lineage.** `WorkflowRun` gains an explicit envelope: `attempt` (1-based) and `resumeOf` (names the run a temporary resume submission continues). User identity stays in `runId`; system lineage moves out of the filename. Serialization version 2; version 1 still loads (`attempt=1`, no `resumeOf`), any other version is refused by name, and a corrupt envelope is refused. `electCheckpoints` groups by the **declared** lineage: a user-named `foo_resume` is standalone and is no longer quarantined by run `foo`'s election; a crash-leftover resume ghost (generated runId, `resumeOf` set) groups under its original and the **complete lineage wins** over the derivative; a finalized ghost is inert; legacy and corrupt payloads keep the historical `_resume` suffix rule (the migration path for pre-envelope ghosts).
3. **Coordinator affinity.** Every public entry point marshals onto the affinity thread through one shared pattern (the accessors' existing `Qt::BlockingQueuedConnection` + inline fast path). `requestCancel` is two-phase: a lock-free atomic trip from any thread (so a whole-file hash aborts within one chunk), then the status-mutating body on the owner. The destructor trips the flag, then drains on the owner (pool clear + `waitForDone` + `removePostedEvents`) behind a `shuttingDown` guard.
4. **D17 provenance layout.** Attempt 1 keeps `provenance_<runId>.json`; attempt N>1 moves to `attempt-<N>/provenance_<runId>.json`. A valid runId cannot contain `/`, so user identity (filename) and system lineage (path segment) can never collide — no single-segment suffix could promise that.

## Test results (actual, this machine)

Environment: Linux, `QT_QPA_PLATFORM=offscreen`, `-j2` builds / `-j1` runs, targeted lanes only (load ~23 from sibling sessions).

- **`test_workflow_checkpoint_cache` (D17 lane): 42/42 test cases, 803 assertions — green.**
- **`test_workflow_durability_13` (new Engine-2.0 lane): 9/9 test cases, 188 assertions — green.**
- Both lanes green **twice consecutively** after the review fixes (final gate script in `.planning/ds41-workflow-durability-13/final_gate.sh`).
- Light regression families green: `test_workflow_ir_v2`, `test_workflow_composition`, `test_d17_workflow_pipeline_e2e`.
- `sicnu_workflow` library (every real consumer of the changed sources) builds clean.
- The heavy Engine-2.0 TU `workflow_run_coordinator.cpp` passes a `-fsyntax-only` check with the project's real compile flags.
- **Gate potency (mutation checks, each reverted afterwards):** election reduced to the legacy filename rule → the user-named `*_resume` and ghost-election tests fail; a "full" hash with a middle blind spot → the mid-file tamper test fails (`0 == 1` executed); mid-hash cancel classified `Failed` instead of `Cancelled` → the cancel test fails; the P1 fix removed → the idle-cancel regression test fails. No vacuous passes.

## Review disposition

Independent reviewer (separate agent, read `origin/master...HEAD` itself): first pass **0 P0 / 1 P1 / 0 P2 / 6 P3 — SHIP-WITH-FIXES**; second pass after fixes: clean, SHIP.

- **P1 (fixed):** an idle `requestCancel` left the cancel atomic set, so the next `resumeFromCheckpoint` aborted with "verification cancelled" (`startRun` cleared the flag, resume did not). `resumeOnAffinity` now clears a stale flag right after the active-run gate; a cancel arriving *during* the resume still trips the flag via phase 1 and aborts at the loop checks. Regression test reuses the same coordinator and was verified to fail without the fix.
- **P3 (fixed):** the election now reads `cap+1` instead of size-then-`readAll` (no TOCTOU bypass of the 16 MiB bound); the header documents that a foreign-thread destruction completes only while the affinity thread services its event loop.
- **P3 (accepted, reasons):** legacy-ghost poisoning via a *planted corrupt* `checkpoint_foo_resume.json` is pre-existing master behavior intentionally preserved as the legacy migration path (D5); the provenance layout change for attempt N>1 has no in-repo consumer (`provenancePath()` is test-only) — flagged as a known limitation below; a value-returning marshal returns a default-constructed result on marshal failure (dying object), documented in code; test timing caveats (512 MiB cancel fixture, mtime restoration) are acknowledged in-code with wide margins.

## Resource discipline

`-j2` max, targeted builds only; no qgis_core/qgis_gui/task_center rebuild; all oracles loopback/offline; no CI wait.

## Known limitations (kept, documented)

1. A planted **corrupt** `checkpoint_foo_resume.json` still follows the legacy suffix rule and can quarantine a valid `checkpoint_foo.json` — pre-existing behavior, intentionally preserved so pre-envelope ghosts remain recognizable (D5).
2. **External tooling** that globs `provenance_<runId>.attempt<N>.json` must switch to `attempt-<N>/provenance_<runId>.json`; no in-repo consumer exists.
3. Engine-2.0 checkpoints written by this build carry version 2; **older builds refuse them by name** (fail closed, by design) — no silent reinterpretation in either direction.
4. A value-returning marshal onto a dying coordinator yields a default-constructed result (documented).
5. A foreign-thread destruction completes only while the affinity thread services its event loop (documented in the header).
6. Election parses each checkpoint payload (bounded 16 MiB each) at recovery startup instead of a filename-only scan — bounded in practice by archive pruning.
7. The heavy Engine-2.0 suites (`test_workflow_recovery`, `test_workflow_engine_v2`, `test_workflow_run_coordinator`, `test_workflow_resume_provenance`) were not rebuilt locally (the qgis/task_center link chain is out of proportion for the added evidence); their contracts are covered by the light lanes, the library build and the single-TU syntax check.

## Conflict hotspots

`.gitignore` (append-only planning allow-list — 3 open PRs) and the tail of `tests/CMakeLists.txt` (append-only test blocks — 2 open PRs). This branch's hunks are at distinct locations; expected conflicts are textual.
