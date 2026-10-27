# PR_COLLISION_MATRIX — R24-C

At baseline: 0 open PRs. Parallel R24 tracks (A scientific algorithms, B data/model runtime) presumed active on their own worktrees/branches.

| Surface | This track writes | Collision exposure | Avoidance |
|---------|-------------------|--------------------|-----------|
| src/agent/workspace_state.cpp (+ .h if const needed) | charts projection | A/B read-only there | none expected |
| src/agent/workspace_snapshot.cpp | dead-branch/comment fix only | low | none |
| src/agent/data_platform_tools.cpp | using-directive collapse | low (shared file with other agent lanes historically) | single minimal hunk |
| src/app/layout/qgslayoutdesignerdialog.cpp/.h | hide empty Atlas/Report menus | prior C-track file (#1677/#1713) | minimal hunk |
| src/app/teaching_admin/teaching_admin_dock.cpp | plural i18n + off-thread scan | teaching lanes read-only backend; GUI file is C's | check master fresh before edit |
| tests/test_e2e_open_issues.cpp | replace 2 tautology assertions | shared legacy file | verify unchanged vs origin/master first |
| tests/ 8 files (L3 list) | clock-gate conversion | r23-E assigned to C | verify unchanged first |
| tests/test_r24_c_*.cpp + tests/CMakeLists.txt END block | new | append-only union | integrity script pre-push |
| tests/surface_diff_snapshot.json | MUST NOT change | surface-locked by three-way drift triangle | run test_surface_snapshot to confirm |

Pre-push protocol: `git fetch origin --prune`; re-check `gh pr list`; if any new PR overlaps, rebase own branch on latest master preserving both registrations; rerun integrity gate.
