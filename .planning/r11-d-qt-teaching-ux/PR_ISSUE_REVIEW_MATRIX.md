# PR & Issue Review Matrix — Direction D

## Status Key
- `ALREADY_IN_MASTER`: Merged into master prior to base SHA `8a08642760`.
- `IN_OPEN_PR`: Addressed in an active PR (#1475, #1476, #1477, #1478). Avoid duplicate work.
- `STILL_BROKEN`: Confirmed broken in current code, targeted by Direction D.
- `NEEDS_REPRO`: Requires reproducible test case before fixing.
- `OUT_OF_SCOPE`: Belongs to another track or subsystem.
- `BLOCKED`: Blocked on external dependency or hardware.

## Matrix

| Issue / Topic | Component | Status | Evidence / Notes |
|---|---|---|---|
| **#1465 (P2)**: `terminology_provider.cpp:179` dedup suffix | `src/help/terminology_provider.cpp` | **STILL_BROKEN** | Dedup suffix `_2` computed into `used` set, but registration logic used bare unsuffixed id or overwrite occurred; relatedIds dangle. |
| **#1465 (P2)**: `terminology_provider.cpp:170` fail-open | `src/help/terminology_provider.cpp` | **STILL_BROKEN** | Resource open failure silently returns empty result without logging typed failure; deferred P2 in #1478. |
| **#1466 (P2)**: `lab_session_state.cpp` non-atomic writes | `src/teaching/lab_session_state.cpp` | `ALREADY_IN_MASTER` | Resolved in commit `d7600c90b2` via platform sidecar authority. |
| **#1466 (P2)**: script adapter capture cap & Windows Python | `src/teaching_admin/script_adapters.cpp` | `IN_OPEN_PR` (#1476) | Addressed in PR #1476. Quarantined. |
| **#1466 (P2)**: batch assessment discovery skips | `src/teaching_admin/batch_assessment.cpp` | `IN_OPEN_PR` (#1478) | Addressed in PR #1478. Quarantined. |
| **#1478 Residual**: Experiment Studio table performance | `src/app/experiment_studio/experiment_studio_dock.cpp` | **STILL_BROKEN** | 1000+ rows allocates thousands of `QTableWidgetItem` objects on heap during UI update. |
| **#1478 Residual**: Ribbon Vector Editing tab gating | `src/app/shell/ribbon_controller.cpp`, `src/app/main_window_vector.cpp` | **STILL_BROKEN** | Vector editing tools enabled when layer not editable; `copyFeatures` / `cutFeatures` / `pasteFeatures` / `selectAll` silently return without feedback. |
| **#1478 Residual**: Pipeline file atomic save | `src/app/workflow/pipeline_editor_dock.cpp` | **STILL_BROKEN** | Save directly writes to target file; crash or disk full can truncate existing file. Needs atomic temp-then-rename publish. |
