# ISSUE TRIAGE — against baseline origin/master `132da5e998`

Re-verified on the baseline tree, not on historical line numbers
(goal §1.2). Status vocabulary: `still-valid` / `fixed-by-later-merge` /
`changed-root-cause` / `duplicate` / `cannot-reproduce` / `out-of-scope`.

## Owned by THIS track

| Issue | Title | Triage | Milestone | Verification |
|---|---|---|---|---|
| #875 | SpatialKFold `generateFolds` division by zero / UB (`blockSize` unvalidated) | **still-valid** (verified: `SplitConfig::validate` guards `SpatialBlock` only; `qint64(std::floor(centerX/0.0))` is UB; NaN block size passes `<= 0.0` for SpatialBlock too) | M0 | New regression tests: old code fails (fold-0 collapse / UB under UBSan), new code refuses with typed `dataset.split_*` diagnostic; full split config-validation matrix suite |
| #876 (data side) | resumeRun ghost run leak — ExperimentStore must not persist fabricated history | Partially-owned: the coordinator fix belongs to exec-concurrency-9; **this track owns the store-side invariant** (no fabricated terminal history, ghost rejection) | M3 | Store-level regression tests: ghost terminal event for unknown execution is a typed error / reported-not-closed (extends test_mlops8_bridge) |

## Out of scope for this track (owned elsewhere; listed to avoid duplication)

- #848, #853, #854, #855, #873 — processing/SAR/hydrology/contracts:
  scientific-algorithms-9 / exec-concurrency-9 remediation wave.
- #849, #857, #858, #859, #861, #882 — GUI/workbench/widgets: workbench /
  exec tracks (main worktree carries the uncommitted remediation).
- #850, #874 — geospatial I/O: geospatial-data-fabric-9.
- #851, #852, #860, #862, #876 (coordinator side) — TaskCenter /
  WorkflowRunCoordinator: exec-concurrency-9.
- #863–#865, #866, #867, #868, #877, #878 — cartography/mapspec/harness
  solver: cartography remediation (uncommitted main-worktree edits).
- #869, #870, #871, #881 — help/diagnostics catalogs: help/help-drift
  remediation (same wave).
- #872, #879, #880 — operator schema drift: operators remediation (same
  wave).

**Overlap guard**: if at PR time the exec track's remediation for #853–#882
has landed on master, this track rebases and confirms none of its own
commits duplicate those fixes (this track touches none of those files).

## Historical clues re-verified (goal §4)

- #875 → owned, above.
- execution ghost-run class → coordinator fix owned by exec track; store
  invariant owned here (M3).
- CLI pipeline auto-record follow-up from 8.0 → **verified still missing**
  on baseline (zero non-test callers wiring `rs_pipeline_runner` into
  `WorkflowExperimentMonitor`); scheduled M3/M5.
- resume recording args follow-up from 8.0 → **verified still missing**
  (`optInResume` has no CLI/MCP surface caller); scheduled M3.
