# OWNERSHIP — R24-C

## Write scope (this track)
- `src/agent/**`, `src/agent_loop/**`, `src/agent_ops/**`, `src/workflow/**`
- `src/app/**` (project-owned GUI; NOT vendored QGIS ported files beyond the minimal listed below)
- `src/ui/**`, `src/help/**`, `pi/**`
- `src/recipes/**` only if a change is strictly required by this track (none planned)
- New tests: `tests/test_r24_c_*.cpp` + one append-only `# BEGIN R24-C … # END R24-C` block in `tests/CMakeLists.txt`
- Edits to pre-existing tests ONLY for: #1558 tautology fixes (test_e2e_open_issues.cpp) and #1699 clock-gate conversions (8 files listed in ISSUE_TRIAGE L3) — both explicitly assigned to C by merged-PR handoffs / r23-E cross-track table; verified no open PR touches them (0 open PRs at baseline).
- `.planning/zcode-r24-C-harness-workbench/**`

## Read-only (hard)
- `src/processing/algorithms/**` (A), `src/geospatial/**`, `src/operators/runtime/**`, `src/experiment/**` (B)
- `src/sdk/**`, `src/plugins/**`, `src/python/**`, `src/teaching/**`, `src/teaching_admin/**` (grading backend — #1695-5 goes to CROSS_TRACK_FINDINGS, not fixed here), `src/lab/**`, `src/jobs/**` (touched by #1714; only read), `src/core/`+`src/gui/` QGIS vendor, root `CMakeLists.txt`, `src/native/**`, `src/verify*/**, src/verification/**`
- QGIS vendored/ported sources under src/app (qgs*): may touch ONLY the layout designer honesty fix (qgslayoutdesignerdialog.* is the project's ported designer shell — confirmed C-owned surface in prior C-track PRs #1677/#1713 which edited exactly these files).
- `.goal-loop-ledger.md` (repo-shared): never written by this track; track ledger = `.planning/zcode-r24-C-harness-workbench/LOOP_LEDGER.md`.

## Shared append-only surface
- `tests/CMakeLists.txt`: append one R24-C block at end with unique prefix; never touch sibling blocks; run `scripts/check_merge_registration_integrity.py --require-refs` before push.
