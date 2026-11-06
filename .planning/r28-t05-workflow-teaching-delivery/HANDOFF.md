# HANDOFF — R28-T05 cross-track interface notes

## For T03 (planner owner)

- Commit d7e2788639 edits `src/planner/planning_context.cpp` (your dir): parse side now
  refuses non-finite/non-positive `resolution_m` with `invalid_field`; write side emits
  only finite positive values. Issue #1804/WE-26 is workflow-experiment-scoped but the
  field authority lives in your module — the change is 12 lines, fail-closed, with a
  regression in `tests/test_r28_t05_placeholder_metrics.cpp` (planning context refuses
  non-positive resolution_m). All `data/planner/golden_scenarios/*.json` use 10.0 —
  no fixture impact. Object/absorb at will; the behavior (refuse garbage resolutions)
  matches your vocab discipline elsewhere in the file.

## For T02 (task_center / task engine owner)

- No edits from this track. #1779/WE-20 (multi-parent ${task.parent} ambiguity) was
  already fixed on master in your module (task_center.cpp:1561/1813) — triaged
  FIXED_OPEN, verified by source read; baseline suite runs will re-confirm.
- The #1812-noted TaskCenter/ExecutionPlane cancellation cluster remains yours; this
  track's WP04 evidence (execution-count oracles, #614 tests) stays green-side.

## For T04 (workbench/UI owner)

- Open PR #1813 touches `tests/CMakeLists.txt` (test registrations for UI suites) —
  the shared-file governance point. This track's registration block is append-only at
  the file tail with explicit R28-T05 markers; merge order either way resolves cleanly.

## For T01 (algorithms/math)

- `src/experiment/evaluation.{h,cpp}` ConfusionMatrix is now sparse-backed with
  identical public metric semantics; if you maintain any KAT (known-answer test)
  matrices for metrics, values are unchanged for every dense input (verified by
  test_experiment_evaluation + test_confusion_matrix in the regression run).
