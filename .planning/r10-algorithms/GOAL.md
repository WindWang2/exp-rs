# GOAL — R10-A: remote-sensing algorithm / operator / Processing execution-chain quality

Improve the scientific-computation trustworthiness of the EXISTING remote-sensing
algorithms, operators and Processing execution chain, and the shared execution
path used by the future RS AI agent. NO new algorithm categories, NO second
runtime, NO new framework.

## Oracle (objective acceptance)

1. `BASELINE.md`, `ALGORITHM_PARITY_MATRIX.md`, `PR_ISSUE_REVIEW_MATRIX.md`, `OWNERSHIP.md`
   complete; every high-risk algorithm touched has reproducible numeric input/expected-output/test.
2. All STILL_REPRODUCIBLE & reachable P0/P1 in scope get minimal fixes with RED→GREEN evidence;
   no fabricated passes (blocked items listed with traceable evidence).
3. Each core algorithm family in the inventory gets ≥1 positive and ≥1 failure-boundary test;
   relevant CTest runs twice with zero new failures.
4. Any performance work carries measured baseline + after (median/p95/RSS), same build/env.
5. No overlap with #1475/#1476/#1477 in-flight paths; no new frameworks/modules.
6. Two independent review rounds (reviewers never implemented what they review); P0/P1 zero.
7. PR submitted from this worktree branch to master with full evidence body; PR URL recorded.

## Work packages

A1 parity inventory (registry vs manifest vs tests) → ALGORITHM_PARITY_MATRIX.md
A2 classification & accuracy-assessment contracts
A3 segmentation/OBIA invariants
A4 hyperspectral numeric contracts
A5 GDAL/QGIS operation semantics (fail-closed, provenance)
A6 existing temporal operators — black-box/integration only (workspace file excluded)
A7 Processing wrapper execution-chain consistency (registry→validate→stage→publish→provenance)
A8 hot-path performance (measured bottleneck only)
A9 test coverage gaps in-scope (boundary/invalid/missing/cancel/multiband/Unicode/Windows)
A10 docs/manifest consistency for existing algorithms

## Iteration protocol

goal-loop: read ledger → real gap only → one focused change → local verify → ledger → re-compare Oracle.
Escalation 3/5/8 per skill. All records under `.planning/r10-algorithms/`.
