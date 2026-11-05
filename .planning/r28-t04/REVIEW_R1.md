# R1 Review — independent agent, fresh context (2026-11-05)

Reviewer: general-purpose subagent, full diff 50cc3255c5..326c3d1668^ (10 commits), instructed to judge from source, not commit messages. Full verbatim report in the session log; findings and dispositions:

## Findings → dispositions (fix commit 326c3d1668)
| ID | Severity | Finding | Disposition |
|---|---|---|---|
| P1-1 | P1 | `m_pendingSessionId` cleared before `sessionQ` computed → late single-job capture dead code; late results committed into the live session; `panelShowsThisJob` session check vacuous | **FIXED** — computation hoisted above the clear; two new test cases pin it (late-capture with applyHintStyle spy; stale-result invalidation through the real success path) |
| P2-1 | P2 | submitOperatorJob: nested submit during synchronous zombie-reap onDone clobbers the newer job's registration | **FIXED** — busy re-check after successful reap |
| P2-2 | P2 | cp11 parity case title overstated; fully transparent render passes unconditional checks | **FIXED** — case renamed to what it enforces; strict gate also asserts non-transparent render |
| P2-3 | P2 | ImportBatchDialog double-preflights every source on the GUI thread | **FIXED** — rows from the session's own per-item state (one pass) |
| P2-4 | P2 | setSuccess unguarded against user switch-away | **FIXED** — pipeline + single-job success join panelShowsThisJob |
| P3-1 | P3 | invalid-layer branch doesn't supersede in-flight probe (stale verdict) | **FIXED** — generation bump |
| P3-3 | P3 | edit-menu Select twin inconsistent with toolbar gate | **FIXED** — objectName + list inclusion |
| P3-4 | P3 | resumable items displayed by invoking action's kind | **FIXED** — own-kind fallback |
| P3-7 | P3 | reap path leaks zombie's per-job lifetime connections | **FIXED** — tracked + deleteLater in finishOperatorJob |
| P3-2 | P3 | clone()-probe: exotic layouts that can't roundtrip become unloadable; 3× serialization per load | NOTED — safe failure direction; performance bounded by template size; follow-up if exotic layouts surface |
| P3-5 | P3 | test-honesty gaps (watchdog timer path not exercised; banner promised late-capture pins) | PARTIAL — the two promised pins now exist (P1-1 cases); watchdog QTimer path still only direct-reap tested (deterministic construct, noted) |
| P3-6 | P3 | double-click emits nodeSelected twice (idempotent, wasteful); cross-canvas desync class unchanged | NOTED |
| P3-8 | P3 | columns=2 repair fix pinned only indirectly | NOTED — e2e furniture-less case would trip the guard if reverted (RED by construction) |

## Verified sound by R1 (quote-worthy for the PR)
Atlas coverage plumbing (compile recreates layouts — no stale custom property), F18 teardown thread-safety, F7 hop, F12 probe, watchdog exactly-once funnel, API compatibility (additive), WP07 shape, runUpToNode reorder rationale.

## Verdict
Blocking: 1 (P1-1) → fixed in 326c3d1668. All P2 fixed. P3: 5 fixed, 3 noted with rationale.
