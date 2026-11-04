# PR_BRANCH_MATRIX — R28-T05

Measured 2026-11-04T21:07Z from GitHub API (paginated; raw NDJSON in raw/).

## Open PRs (1)

| PR | Title | Head | Track overlap | Collision risk for T05 |
|---|---|---|---|---|
| #1813 | fix(ui): workbench UI polish pass + line-by-line review follow-ups | fix/ui-review-2026-11-05 | T04 (UI) | **Medium**: touches `tests/CMakeLists.txt` + test files (test_data_manager_panel, test_experiment_studio_dock, test_preferences_dialog, test_theme_selector_parity, test_workbench_full_shell_lifecycle). T05 owns shared test registration — re-fetch and re-merge before PR. |

## Remote agent branches (all 0 ahead of master ⇒ fully merged)

R23 a–e, R24 a–c, R25 A–E, R26 ×3, R27 t01–t05 — nothing unmerged pushed.

## Local-only R28 parallel work (invisible diffs, unknown status)

- `exp-rs-r28-t01-baseline` (detached at 50cc3255c5, read-only baseline worktree)
- `exp-rs-r28-t01-science-20261104T180334Z` (branch agent/r28-t01-science-…)
- `exp-rs-r28-t02-20261104T180429Z` (branch agent/r28-t02-data-runtime-…)
- `exp-rs-r28-t03-harness-sdk-20261104T183352Z` (branch agent/r28-t03-harness-sdk-…)
- `exp-rs-r28-t04-workbench-cartography-20261104T210902Z` (appeared during recon)

Rules applied: no checkout/stash/reset of these; T05 does not wait on them; shared-file edits limited to append-only track-tagged blocks; re-fetch + re-review at merge gate.

## Recently merged PRs of record (reviewed)

- #1812 (merged 2026-11-04T15:16Z): swarm review fixes; 100 files; recorded 77/9776 residual failures + snapshot drift blocked on OpenCV toolchain.
- #1809 (merged 2026-11-03T16:18Z): R27-T05 closed loop, 26 issues claimed; 59 files.
