# Issue Deduplication Matrix

Baseline SHA: 19843d1b6910c9207c7e5c97863a873db679368e
Audit date: 2026-08-15
Search method: `gh issue list --state all --limit 250 --json number,title,state`, then per-finding keyword + root-cause search.

| Finding | Verdict | Decision | Linked issue |
|---|---|---|---|
| F-001 Linear near-collinear singularity | NEW | Submit | — |
| F-002 Projective rank-deficient silent | NEW | Submit | — |
| F-003 hasInverse threshold scale-blind | NEW | Submit | — |
| F-004 .points v2 y-negation/CRS-mix | NEW | Submit | — |
| F-005 GCP list mixed-CRS | NEW | Submit | — |
| F-006 transformedDestinationPoint exception | NEW | Submit (or merge into F-005) | — |
| F-007 Helmert no normalization | NEW | Submit (debatable; upstream-equivalent) | — |
| F-008 Helmert non-symmetric matrix | FALSE_POSITIVE | do not file | — |
| F-009 RPC RPC_HEIGHT/RPC_DEM additive | NEW | Submit | — |
| F-010 Uncaught exception in warp path | NEW | Submit | — |
| F-011 fit() swallows exception | NEW | Submit | — |
| F-012 warp task leak on close mid-warp | DUPLICATE | do not file (subset of #238) | #238 |
| F-013 Pixel-level split inflates OA/Kappa | NEW | Submit | — |
| F-014 Predict-only unscaled silent (GUI) | NEW | Submit | — |
| F-015 Hard-coded seed 42 | NEW | Submit | — |
| F-016 Per-tile NoData/ignore still predicted | NEW | Submit | — |
| F-017 testSplit=0.0 silent 5% | NOT_REPRODUCIBLE | do not file | — |
| F-018 KMeans predict hot path 8-9x | NEW | Submit (different root cause from #200, #201) | — |
| F-019 RF supportsProbabilities mismatch | NEW | Submit | — |
| F-020 ROI silent recompute on project reload | NEW | Submit | — |
| F-021 Sieve O(C·W·H) | DUPLICATE | do not file — root cause matches #176 | #176 |
| F-022 Spectral curves 1x1 RasterIO | NEW | Submit | — |
| F-023 Operator predict-only unscaled silent | NEW | Submit (same root cause as F-014 from headless angle; merge) | — |

## Merge decisions
- **F-014 + F-023** — same root cause (silent unscaled predict when sidecar missing/corrupt), different angles (GUI vs headless). Merge into one issue with two observed surfaces.
- **F-006** — display-only impact, low severity; merge into F-005 (CRS homogenization) is possible but the bugs are independent. Submit separately but mark cross-reference.

## Falsified / Rejected
- F-008 (Helmert non-symmetric): false positive — matrix is row-permuted M^T M, mathematically correct.
- F-017 (testSplit=0.0 silent 5%): not reproducible — current callers all guard against this path.

## Already filed at this SHA
- F-021 → #176
- F-012 → #238 (warp-task leak on rejected submission; close-mid-warp is a different but related trigger; do not file a duplicate)
