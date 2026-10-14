# R13-H Architecture Decisions & Ledger (DECISIONS.md)

## D1 | Autonomous Execution Mode Protocol
- Decision: Per user prompt, execute fully autonomously without requiring interactive user approval. Document all tradeoffs, assumptions, and defaults here in `.planning/r13-h-planning/DECISIONS.md`.

## D2 | Worktree & Build Isolation
- Decision: Use dedicated worktree `/home/kevin/projects/rs-studio/exp-rs-r13-h-scientific-planner` on branch `hardening/r13-h-scientific-planning` based on BASE_SHA `892295685e3606f3ac54c959954dde626508cc17` (origin/master).
- Build directory: `build-r13-h` using Ninja generator.
- No modifications to other active worktrees (`exp-rs-r13-g-cartography`, etc.).

## D3 | Concurrency & Resource Discipline
- Decision: Strict enforcement of `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, and `QT_QPA_PLATFORM=offscreen`.
- All compilation, linking, and heavy tests must hold `/tmp/exp-rs-agent-locks/global-build.lock` via `flock`.

## D4 | Status of Issue #1465 (Broker Cache Key Typed Failure Cause)
- Decision: Code review reveals commit `9052cb5d82` introduced `keyMat.failureDigest` in `src/science_context/broker.cpp:270-278`. However, edge cases must be hardened and verified:
  1. Empty vs populated failure digest.
  2. Transition between different error codes with identical asset keys.
  3. Ensure no stale cache entries survive failure state changes.
  We will add an exhaustive regression test suite in `tests/test_science_context_broker.cpp`.

## D5 | Status of Issue #1466 (Suitability Goal Bounded Integral Type Safety)
- Decision: `src/suitability/suitability_goal.cpp:55-72` already implemented `boundedIntegral`. We keep `src/suitability/**` read-only as required by OWNERSHIP.md and confirm zero regression via `tests/test_suitability_adversarial.cpp`.

## D6 | Hardening JSON Parsing against Json::LogicError (#1447)
- Decision: In `src/planner/json_util.h:90`, `checkEnvelope` calls `doc["kind"].asString()` without first checking `doc["kind"].isString()`. When `doc["kind"]` is an integer or non-string, jsoncpp throws `Json::LogicError`, violating the fail-closed exception-free boundary.
- Fix: Add `!doc["kind"].isString()` check before `asString()`.

## D7 | ScientificGoal In-Memory Validation Completeness
- Decision: In `src/planner/scientific_goal.cpp`, `validateScientificGoal()` must validate all fields of in-memory goals:
  - Temporal scope bounds: `minScenes >= 0`, `maxGapDays >= 0`, string lengths <= `kMaxTextChars`.
  - Acceptance criteria bounds: non-empty `criterionId` (<= 64 chars), non-empty `check` (<= 512 chars), `target` length <= 512 chars, and duplicate `criterionId` rejection.
  - Quantity string length <= `kMaxTextChars`.

## D8 | Deterministic Candidate Sorting in PlannerCore
- Decision: In `src/planner/planner_core.cpp:38-48`, `rankCandidates` uses `costClassRank` then `operatorId`. Ensure tie-breaking is strictly deterministic and resilient to duplicate candidate entries from buggy providers.

## D9 | Repair Candidate Cost Ranking Normalization
- Decision: In `src/repair_planner/repair_planner.cpp`, `candidateLess` previously sorted `a.cost.rank < b.cost.rank` directly. Because missing or invalid cost classes produce rank 0, unknown costs were incorrectly sorted ahead of valid light/medium/heavy costs. We normalize non-positive cost ranks (`cost.rank <= 0`) to penalty rank 999 so they sort last.

## D10 | Repair Candidate Deduplication
- Decision: In `src/repair_planner/repair_planner.cpp`, `planRepairsForFindings` now deduplicates candidates by `(operatorId, ruleId, actionKey)` after stable sorting. This retains the best-ranked candidate while eliminating redundant identical operations from provider duplicates.

## D11 | ScienceContext Typed Error Defaulting & Cache Key Hardening (Issue #1465)
- Decision: In `src/science_context/live_asset_resolver.cpp:50-54`, when `anyFacts` is false and `errorCode` is empty, default `errorCode` to `"asset_not_found"`. In `broker.cpp:170`, ensure empty `resolved.error` falls back to `"asset_not_found"`. This guarantees `failureDigest` reliably tracks error code changes and prevents empty failure markers (`asset_resolve_failed:`).

## D12 | Context Budget Critical Question Preservation
- Decision: In `src/science_context/context_budget.cpp`, `trimQuestions` partitioned open questions with `std::stable_partition` using `isCriticalOrBlockingQuestion` before truncation. This ensures non-blocking informational questions are trimmed first and critical blocking questions (e.g., `asset_resolve_failed:...`, `[blocking]`) are preserved.

## D13 | Fail-Closed Type Guards for JsonCpp (#1447)
- Decision: Guard all `.asString()` calls across `capability_router.cpp`, `recipe_router.cpp`, `bundle.cpp`, and `broker.cpp` with `.isString()` or a dedicated `readString` helper. Non-string inputs fail closed instead of throwing unhandled `Json::LogicError`.

