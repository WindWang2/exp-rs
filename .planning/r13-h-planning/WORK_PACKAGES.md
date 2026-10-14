# R13-H Work Packages Specification & Execution Ledger

## WP-01 | ScientificGoal Parameters & Hard Boundaries
- **Scope**: `src/planner/scientific_goal.*`, `scientific_plan.*`, `json_util.h`
- **Focus**: Typed schema, enum validation, range bounds, nan/inf checks, empty array rejection, conflicting parameter handling, fail-closed on invalid goal structures.
- **Status**: COMPLETED.
- **Resolution**:
  - `src/planner/json_util.h`: Hardened `checkEnvelope` by checking `doc["kind"].isString()` and `doc["schema_version"].isString()` before calling `asString()`, eliminating untyped exception vectors (#1447).
  - `src/planner/scientific_goal.cpp`: In `readAcceptanceCriteria`, added duplicate `criterionId` detection and rejection with typed error. In `validateScientificGoal`, added checks for non-negative `temporalScope.minScenes`, `temporalScope.maxGapDays`, string lengths <= `kMaxTextChars`, and non-empty acceptance criteria fields.
- **Verification**: `tests/test_scientific_planner_schema.cpp` unit tests verifying fail-closed envelope and in-memory bounds.

## WP-02 | Deterministic PlannerCore & Candidate Selection
- **Scope**: `src/planner/planner_core.*`
- **Focus**: `firstVerifiableCapability`, `rankCandidates`, `costClass` stable tie-breaking, unknown source provider handling, duplicate candidate elimination.
- **Status**: COMPLETED.
- **Resolution**:
  - `src/planner/planner_core.cpp`: Updated `rankCandidates` to deduplicate candidates by `(operatorId, costClass)` while preserving stable sorting.
- **Verification**: Deterministic replay in `test_scientific_planner_core`.

## WP-03 | Rule Spine & Conditional Gate Integrity
- **Scope**: `src/planner/planner_rules.*`, `planner_constraints.*`
- **Focus**: Ensure DAG topology (import -> alignment/calibration -> analyze -> verify -> publish) enforces mandatory gates without cycles, self-dependencies, or bypassed constraints.
- **Status**: VERIFIED.
- **Resolution**: Verified existing rule chain invariants.

## WP-04 | Plan IR Projection & Workflow Alignment
- **Scope**: `src/planner/plan_ir_projection.*`
- **Focus**: Validate port mappings, parameter types, missing fields, invalid nodes, and capability translation to Workflow IR.
- **Status**: COMPLETED.
- **Resolution**: In `projectPlanToIr`, step input wiring fails closed when `assetRef` is unindexed or missing.
- **Verification**: Workflow IR projection tests in `test_scientific_planner_ir`.

## WP-05 | OpenQuestions & Scientific Fact Boundaries
- **Scope**: `src/planner/planning_context.*`, `planner_core.*`
- **Focus**: Ensure unavailable/unknown assets are never treated as ready facts, deterministic open questions sorting & dedup, blocking question enforcement.
- **Status**: COMPLETED.
- **Resolution**: Ensured questions maintain blocking status invariants.
- **Verification**: `tests/test_scientific_planner_core.cpp`.

## WP-06 | Planning Provider Result Isolation
- **Scope**: `src/planner/provider_interfaces.h`, `planner_core.*`
- **Focus**: Safe handling of provider failures, timeouts, empty lists, massive candidate lists, re-entrancy, memory leaks.
- **Status**: VERIFIED.
- **Resolution**: Verified mock provider test suites.

## WP-07 | RepairPolicy Action Safety Whitelist
- **Scope**: `src/repair_planner/repair_policy.*`, `repair_requirement.*`
- **Focus**: Action whitelist enforcement, risk-level calculation (science-changing vs shape-preserving), reject unverified auto-repairs, exact tool name matching.
- **Status**: VERIFIED.
- **Resolution**: Action whitelist strictly bounds autonomous repairs.

## WP-08 | RepairPlanner Candidate Payoff Ranking
- **Scope**: `src/repair_planner/repair_planner.*`
- **Focus**: `costRankFromClass`, plan ID/hash determinism, tie-breaking for equal risk, negative cost rejection, duplicate candidate handling.
- **Status**: COMPLETED.
- **Resolution**:
  - In `candidateLess`: normalized unknown or non-positive cost ranks (`cost.rank <= 0`) to penalty rank 999 so they sort after valid costs.
  - In `planRepairsForFindings`: deduplicated candidates by `(operatorId, ruleId, actionKey)` after stable sorting.
- **Verification**: `tests/test_repair_planner_completion.cpp` tests for unknown cost penalty ranking and candidate deduplication.

## WP-09 | Repair Plan Schema & State Sequences
- **Scope**: `src/repair_planner/repair_schema.*`, `repair_state.*`, `repair_view.*`
- **Focus**: JSON schema validation, patch before/after validation, typed error reporting, rollback suggestions.
- **Status**: COMPLETED.
- **Resolution**: Envelope checks guarded against non-string types.

## WP-10 | ScienceContext Cache Key & Typed Failure Causes
- **Scope**: `src/science_context/broker.*`, `context_cache.*`, `live_asset_resolver.*`
- **Focus**: Verify and harden cache key failure digestion (Issue #1465), prevent stale cache hits when underlying failure reason changes.
- **Status**: COMPLETED.
- **Resolution**:
  - In `live_asset_resolver.cpp:50-54`: when `anyFacts` is false and `errorCode` is empty, default `errorCode` to `"asset_not_found"`.
  - In `broker.cpp:170`: guard empty `resolved.error` with `"asset_not_found"`.
  - `failureDigest` computation verified to re-key when failure reason changes.
- **Verification**: Added `TEST_CASE( "broker cache key changes when failure reason changes" )` in `tests/test_science_context_broker.cpp`.

## WP-11 | Capability Router & Recipe Router
- **Scope**: `src/science_context/capability_router.*`, `recipe_router.*`
- **Focus**: Catalog queries, routing determinism, version stability, fail-closed on unknown capabilities.
- **Status**: COMPLETED.
- **Resolution**: Guarded all `.asString()` and `.asBool()` calls with type checks in `capability_router.cpp` and `recipe_router.cpp`.
- **Verification**: `test_science_context_live_authorities`.

## WP-12 | Budgeted Context Integration
- **Scope**: `src/science_context/context_budget.*`, `bundle.*`
- **Focus**: Respect byte/count limits, prevent truncation of critical risks/open questions, payload integrity.
- **Status**: COMPLETED.
- **Resolution**:
  - In `context_budget.cpp`: added `isCriticalOrBlockingQuestion` and `std::stable_partition` in `trimQuestions` so non-blocking informational questions are trimmed first, preserving critical blocking questions.
  - In `bundle.cpp`: added safe `readString` helper and guarded all fields against untyped JsonCpp exceptions (#1447).
- **Verification**: Added `TEST_CASE( "budget truncation preserves critical blocking questions" )` in `tests/test_science_context_broker.cpp`.

## WP-13 | State Observation & Asset Resolution Views
- **Scope**: `src/science_context/observability.*`, `observed_state.*`, `live_asset_resolver.*`
- **Focus**: Ready/Unknown/Failed state convergence, typed failure preservation, no false ready claims.
- **Status**: COMPLETED.
- **Resolution**: Guaranteed typed error preservation across all sources.

## WP-14 | Scientific Suitability Evaluation Contract Regression
- **Scope**: `src/suitability/**`
- **Focus**: Confirm #1466 integer goal fix (`boundedIntegral`), regression test suit with adversarial inputs.
- **Status**: COMPLETED (Read-only maintained).
- **Resolution**: Confirmed `boundedIntegral` implementation in `src/suitability/suitability_goal.cpp`.

## WP-15 | Plan Explanation & Evidence Read-Only Closed Loop
- **Scope**: `src/planner/*`, `src/science_context/*`
- **Focus**: Structured explanation generation (goal -> selection rationale -> constraints -> questions -> plan).
- **Status**: VERIFIED.
- **Resolution**: Maintained explanation structure.

## WP-16 | Planning Performance & Bounded Search
- **Scope**: `src/planner/**`, `src/repair_planner/**`
- **Focus**: Benchmarks for candidate scaling (50, 500, 5000) and asset scaling (10, 100, 1000). Ensure bounded complexity.
- **Status**: VERIFIED.
- **Resolution**: Strict caps enforced (`maxRequirements`, `maxCandidatesPerRequirement`, `maxTotalCandidates`).

