# OVERLAP MAP — parallel-track conflict avoidance

Active sibling tracks on this host (verified via `git worktree list` + branch
tips):

| Track | Branch / worktree | Areas touched | Overlap with us |
|---|---|---|---|
| execution-concurrency-9 | feat/execution-concurrency-lifecycle-9 @ 8f6293bceb | workflow, task center, data manager concurrency | none in src/geospatial; M8 hints are a new leaf they may later consume |
| scientific-algorithms-9 | feat/scientific-algorithms-9 @ 132da5e998 | processing algorithms | none |
| main worktree | master @ 8f6293bceb + ~35 uncommitted files | issues #853-#882 fixes across app/widgets/workflow/help/cartography + `src/geospatial/raster/raster_reader.cpp` + `src/operators/io/io_operators.cpp` | **potential**: their uncommitted work may fix #874 (raster_reader readMask) and touch io_operators. We implement our own #874 fix on OUR base. At final sync: if master gained a readMask fix, keep the more complete fix (band-precision compare) and merge tests. |
| geospatial-data-fabric-8 worktree | detached @ 821fcd4ee4 | merged already | none |

Rules this track follows:
1. Never write into other worktrees.
2. Shared-file edits delayed to milestone ends, minimal hunks (see OWNERSHIP).
3. If origin/master gains commits that touch src/geospatial, merge origin/master
   promptly and re-run the io test family.
