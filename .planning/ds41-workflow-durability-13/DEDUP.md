# DEDUP — ds41-workflow-durability-13

Checked 2026-09-21 against live `origin/master` (79adfe78a), all open PRs (#1135/#1136/#1137) and
the merged predecessor #1132 (flash-workflow-engine-12).

## Verdicts

1. **No open PR or merged PR implements any WP of this track.** #1135 (temporal), #1136 (labs),
   #1137 (geospatial maintenance) touch no workflow durability file. #1132 (the direct
   predecessor) deliberately left these four gaps open and documented them as Known limitations —
   they are requirements evidence for THIS track, not work to redo.

2. **Not redoing (already on master, verified by reading the code):**
   - D17 checkpoint envelope gate (`kind` + closed `version` set {1.0, 1.1}) — present.
   - Per-node artifact identity fields (size / mtime / `sha256fl:` fingerprint) + containment +
     resume-time re-verification — present (pipeline_run_coordinator.cpp).
   - Bounded checkpoint reads (16 MiB, `workflow_limits.h`) — present, both stacks.
   - Engine-2.0 atomic save (tmp+fsync+rename+dir-fsync), run locks (#727), corrupt-checkpoint
     skipping, `_resume` filename election — present but with the defects listed in BASELINE.
   - Strict state vocabulary / fail-closed D17 resume — present.

3. **Real gaps this track owns (each has a reproduction):**
   - G1 mid-file tamper blind spot of `sha256fl` for >2 MiB artifacts (BASELINE item 1).
   - G2 `_resume` filename-suffix election mis-grouping user-named runIds; no lineage envelope
     separating user identity from system attempt lineage; legacy checkpoints have no migration
     story for the new envelope (BASELINE items 2, 3).
   - G3 D17 provenance filename collision between user runId and attempt suffix (BASELINE item 4).
   - G4 coordinator mutator affinity holes + unbounded cancel marshal + destructor race
     (BASELINE item 5).
   - G5 no recovery-stress oracles: crash at atomic-publish boundaries, cancel during a long
     artifact hash, restart recovery, corrupt provenance, concurrent reader + owner mutation.

4. **Parallel-track boundaries (unchanged from predecessor):** Mission Workbench, TaskCenter
   scheduler internals, model runtime, temporal algorithms are read-only. Engine-2.0
   `workflow_run_coordinator.cpp` is edited only at the two lineage seams (ghost stamping +
   attempt increment); everything else in it is untouched.

5. **Pivot rule:** if a live overlap appears mid-track (checked at each milestone), shrink to the
   remaining gap and record the pivot here. None so far.
