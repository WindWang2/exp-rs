# R16-B-T04 Goal Loop Ledger

Host note: same as T03 — ZCode host, native worktree/oracle mechanism, no fabricated command output; capacity spent on the full audit + verification, not on token burn.

## Work packages

| WP | Type | Oracle | State |
| --- | --- | --- | --- |
| WP-T04-1 | audit evidence | every named lead re-verified on master source; table in `BASELINE.md` | done |
| WP-T04-2 | verification | run `test_preflight*`, `test_scientific_planner_*`, `test_suitability_*`, `test_repair_planner_*`, `test_workflow_repair*` families on the integration branch after the T03 broker API change; all green ×2 | pending integration |
| WP-T04-3 | budget inventory | `PERFORMANCE.md`: existing caps with file:line (kMaxScenes, PlanLimits, PreflightBudgets, mirror caps, ack/budget contracts) | done at doc time |

## Decision log

- **No code change**: no confirmed defect in ownership; manufacturing one would violate the task's honesty constraints. The cross-track risk (broker `capabilityFacts()` signature change in T03) is verified here: `grep` shows no `capabilityFacts()` consumer inside `src/suitability|preflight|planner|repair_planner`; `tests/test_preflight_authority_invalidation.cpp` uses only `setCapabilityFacts` (7 occurrences) — compile-safe under the T03 change. Verified by running the family in WP-T04-2.

## Phase 2 log

(no RED→GREEN cycle in this track — nothing to fix; see Decision log)
