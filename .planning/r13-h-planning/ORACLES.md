# R13-H Verification Oracles & Acceptance Gates

## Oracle O1 | ScientificGoal Fail-Closed Parameter Validation (P0/P1)
- **Condition**: Any unknown goal kind, negative minScenes/maxGapDays, non-finite / NaN numbers, strings masquerading as numbers, missing envelope, or exceeding size limits (`kMaxIdChars`, `kMaxTextChars`, `kMaxAcceptanceCriteria`) must return `false` with a typed error prefix (`invalid_document`, `unsupported_version`, `invalid_field`, `out_of_bounds`).
- **Forbidden**: Never silently coerce or default invalid input into an executable goal.

## Oracle O2 | Deterministic Candidate Ranking & Replay Stability (P1)
- **Condition**: Replaying `planScientificWork` 100 times with permuted provider candidate inputs must produce byte-identical plan JSON and identical SHA-256 fingerprints.
- **Rule**: Primary sort key = `costClassRank` ascending; secondary key = `operatorId` ascending (lexicographical).
- **Forbidden**: Non-deterministic tie-breaking or skipping safety gates.

## Oracle O3 | Rule Spine DAG & Mandatory Gate Topology (P0/P1)
- **Condition**: The stage spine `import -> alignment/calibration -> analyze -> verify -> publish` must enforce:
  1. Grid/CRS conflict strictly requires alignment stage or raises typed blocking question if unbridgeable.
  2. Domain gap requires calibration stage or raises typed blocking question.
  3. Acceptance criteria must project to verify stage checks.
  4. DAG must be strictly acyclic with no self-dependencies.

## Oracle O4 | Plan IR Projection Soundness (P1)
- **Condition**: `projectPlanToIR` must emit valid workflow nodes with strictly matching capability ports and types.
- **Forbidden**: Dangling node inputs, missing output bindings, or bypassed IR validation.

## Oracle O5 | Asset State Precondition Integrity (P0)
- **Condition**: Only `ready` assets (hard facts) can satisfy step input preconditions. `unknown`, `stale`, or `failed` assets must produce typed `PlanOpenQuestion` with `blocking = true`.
- **Forbidden**: Stale or unverified assets masquerading as ready hard facts.

## Oracle O6 | Provider Failure Isolation & Resource Bounding (P1)
- **Condition**: A provider returning an empty list, malformed metadata, or throwing exceptions must be handled gracefully: planner records an infeasible plan with typed open questions; no process crash, no unbounded allocations.

## Oracle O7 | RepairPolicy Action Whitelist & Risk Invariants (P0)
- **Condition**: Only registered whitelist actions (`kCapabilityRef`, `kDeclarativeTransform`, `kDecision`) are permissible. Actions with risk exceeding allowed threshold must be rejected or marked as requiring human decision.
- **Forbidden**: Automatic execution of science-changing repairs without explicit decision gating.

## Oracle O8 | RepairPlanner Payoff Determinism & Hash Invariance (P1)
- **Condition**: `RepairPlanner` candidate plans must have deterministic sorting (`costRankFromClass` -> risk -> candidate index -> action id) and byte-identical plan hashes across 100 replays.

## Oracle O9 | Repair Schema & Patch Integrity (P1)
- **Condition**: Repair plans must reject unsupported patch kinds, validate before/after preconditions, and maintain non-destructive rollback steps.

## Oracle O10 | ScienceContext Cache Key Typed Failure Cause Binding (Issue #1465) (P1)
- **Condition**: The cache key material MUST include sorted & deduplicated typed failure causes (`failureDigest`). If an asset's resolution status transitions from `resolver_unavailable` to `asset_not_found`, the cache key MUST change, preventing stale cache replay.
- **Verification**: Dedicated test case verifying cache miss when failure cause changes for the same asset ref.

## Oracle O11 | Capability & Recipe Router Determinism (P1)
- **Condition**: Router queries against the published catalog must return deterministic versions and fail closed on unknown capabilities or recipes.

## Oracle O12 | Context Budget Enforcement & Non-Truncation of Blocking Hazards (P1)
- **Condition**: Budget limits (`maxBytes`, `maxRecipes`, `maxCapabilities`, `maxOpenQuestions`) must be strictly honored without discarding `blocking = true` questions or critical security warnings.

## Oracle O13 | Suitability Goal Non-Double / Wrong Type Fail-Closed (Issue #1466) (P1)
- **Condition**: Integral suitability goal fields (e.g., `min_samples`) with string values (e.g. `"200"`) must fail closed with a typed error rather than silently defaulting.

## Oracle O14 | Bounded Search Performance Gate (P2)
- **Condition**: Planning with 50, 500, and 5000 capabilities must maintain bounded CPU time (linear/log-linear) and constant peak RSS overhead, with no O(N^2) explosion.
