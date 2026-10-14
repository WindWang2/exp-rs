# hardening(r13-h): deterministic scientific planner, context and repair decision contracts

## Summary
Implements autonomous hardening track R13-H across `src/planner`, `src/repair_planner`, and `src/science_context`:
1. **JsonCpp Type Safety (#1447)**:
   - Hardened `checkEnvelope` in `src/planner/json_util.h` by checking `!doc["kind"].isString()` and `!doc["schema_version"].isString()` before `asString()`, eliminating untyped exception escapes (`Json::LogicError`).
   - Guarded `.asString()` and `.asBool()` calls in `capability_router.cpp`, `recipe_router.cpp`, and `broker.cpp`.
   - Added type-safe `readString` helper in `src/science_context/bundle.cpp` for resilient deserialization.
2. **ScientificGoal In-Memory Validation & Acceptance Criteria Deduplication (WP-01)**:
   - Added duplicate `criterionId` rejection in `readAcceptanceCriteria` and `validateScientificGoal`.
   - Enforced non-negative temporal bounds (`minScenes >= 0`, `maxGapDays >= 0`) and text bounds (`quantity`, `check`, `target` <= limits).
3. **Deterministic Candidate Selection & Deduplication (WP-02, WP-08)**:
   - In `planner_core.cpp`: deduplicated candidates by `(operatorId, costClass)` in `rankCandidates`.
   - In `repair_planner.cpp`: normalized unknown/invalid cost classes (`cost.rank <= 0`) to penalty rank 999 in `candidateLess` so unknown costs sort last; deduplicated repair candidates by `(operatorId, ruleId, actionKey)`.
4. **Context Cache Invalidation on Failure Reason Transition (Issue #1465, WP-10)**:
   - In `live_asset_resolver.cpp` and `broker.cpp`: defaulted missing asset error codes to `"asset_not_found"`.
   - Verified that `failureDigest` in `broker.cpp` properly re-keys cache on error transitions.
5. **Context Budget Critical Question Preservation (WP-12)**:
   - In `context_budget.cpp`: partitioned open questions with `std::stable_partition` to ensure non-blocking informational questions are trimmed first, preserving critical blocking questions (`asset_resolve_failed:...`, `[blocking]`).
6. **Plan IR Projection Fail-Closed Wiring (WP-04)**:
   - In `plan_ir_projection.cpp`: step input wiring fails closed when `assetRef` is missing or unindexed.

## Verification
- Added Catch2 test cases covering:
  - Non-string envelope kind and schema version rejection in `test_scientific_planner_schema.cpp`.
  - In-memory bounds and duplicate criterion ID rejection in `test_scientific_planner_schema.cpp`.
  - Unknown cost rank penalty and candidate deduplication in `test_repair_planner_completion.cpp`.
  - Cache failure digest re-keying on error transitions in `test_science_context_broker.cpp`.
  - Critical blocking question retention under budget limits in `test_science_context_broker.cpp`.
- Zero touches to read-only domains (`src/suitability`, `src/verify`, `src/faultlab`, `src/workflow`, `src/preflight`).
