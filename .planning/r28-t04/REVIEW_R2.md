# R2 Review — second independent reviewer, fresh context (2026-11-05)

Verified R1 fixes genuinely correct (esp. P1-1: only read of m_pendingSessionId is the capture itself; the pinning test is non-tautological). New findings → dispositions (fix commit follows this report):

| ID | Severity | Finding | Disposition |
|---|---|---|---|
| F-1 | P2 | cross-type stop-then-resubmit race: a cancelled-but-armed single job's late terminal could clear a live pipeline's flags/captured session (and vice versa) — same wrong-session-commit consequence class as R1 P1-1, reachable from two production Stop paths | **FIXED** — each submission supersedes the other type's armed tracking (single-job arm clears m_activePipelineId; pipeline arm clears m_pendingTaskId), so only one tracked work item exists at a time |
| F-2 | P3 | A3 change accidentally dropped the generic layer-remove confirm (dead `names`/`detail` block) | **FIXED** — generic confirm restored, chained before the edit-buffer question |
| F-3 | P3 | F7 test server leaked its QThread wrapper; silent wait timeout | **FIXED** — deleteLater-on-finish + terminate last resort |
| F-4 | P3 | dead refactor leftover in refreshRowsFromSession | **FIXED** — removed |
| F-5 | P3 | P1-1's data-attribution half untested (reverting the capture would still pass) | **FIXED** — `completedStepIds()` test seam + new case: late Completed attributes to session A, session B's same-named step stays incomplete |
| F-6 | P3 (pre-existing) | stale `m_pendingLoadToMap` seeds pipeline auto-loads | **FIXED** in the reworked arm blocks (pipeline arm clears it) |

R2 also independently confirmed: test evidence clean (30 final logs all-pass, case counts match committed sources, no wrong-binary/0-test risk), cross-track hygiene clean (append-only CMake block, gitignore whitelist only, all paths within ownership).

**Verdict: no unresolved blockers after the fixes above.** Post-fix full matrix: 15/15 suites ×2 all green (workflow suite now 7 cases / 366 assertions).
