# REVIEW R1 — independent (fresh-context subagent, 2026-11-05)

Reviewer: general-purpose subagent, no implementation context; read the diff,
sources, tests and build evidence directly. Full report archived verbatim in
the agent transcript; findings summary:

## Verdicts
- A scientific/functional: mostly correct; sparse fromJson accepts negative counts (P1-1) + 2^63 boundary hole (P2-4); WE-19 epsilon absolute-only + vacuous test
- B lifecycle/data-loss: dry-run staging/mkpath side effects (P1-2); oversized checksums silently disables TOCTOU re-verify (P1-3)
- C security: gates intact; 2 of 3 checksums readers unbounded (P2-5)
- D API compat: clean (all ConfusionMatrix callers verified; no aggregate-init/memcmp users; no env-identity test dependencies)
- E perf: no O(n^2) on fixed path; ~11 redundant matrixSums in toJson (P3-10)
- F test quality: red evidence verified independently; WE-19 ulp test vacuous (P2-6); budget/grid/negative gaps untested (P3-14)
- G CMake: honest FATAL_ERROR breaks no legitimate configure (verified ci.yml lanes, lab profile, SicnuLabProfile never disable OpenCV)

## Findings fixed by this track (batch, see DECISIONS D8)
P1-1 sparse negative counts → refused (+ regression)
P1-2 dry-run writes (staging+mkpath) → hash-only dry-run stream, no mkpath (+ regressions: no dir, no .importing, would-relocate)
P1-3 oversized checksums → import refused with typed warning (+ regression w/ valid duplicate-line inflation)
P2-4 2^63 boundary → exclusive bound both formats (+ regression)
P2-5 third unbounded checksums read → reuse verifiedDigests
P2-6 vacuous ulp test + absolute epsilon → real ulp pair (0.3 vs 0.1+0.2) + relative epsilon (+ large-magnitude case)
P2-7 QHash-order serialization → sorted-key emission (+ determinism regression)
P3-12 dry-run conflict wording → aligned
P3-13 DECISIONS wording → amended (D9)

## Deferred (impact + rationale in DECISIONS D8)
P3-8 whitespace ids (pre-existing laxity), P3-9 empty-matrix guard (no caller path), P3-10 toJson sums hoist (perf-only), P3-11 .importing sweep (concurrency-unsafe)

## Verified CORRECT (15 items, not to redo in R2)
Algebraic equivalence of all rewritten metrics; sparse key safety; API compat incl. output_verifier/benchmark_runner; WE-18/24/26/29/30/34/09 implementations; WP07 store gating; CMake impact analysis; green/red evidence artifacts cross-checked (CTestCostData, LastTestsFailed logs).
