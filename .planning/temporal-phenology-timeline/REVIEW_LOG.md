# REVIEW_LOG — temporal-phenology-timeline (D16 · Phase 6 dual-axis review)

Reviewers: 2 of ≤3 read-only subagents (no recursion), dispatched 2026-09-15
against `bf4b667043` (21 commits, +7085 lines over baseline `007e70cff6`).
Reviewer 2 independently re-derived the Whittaker band assembly (n=3/4/5),
verified the incomplete-beta F p-value against numerical integration of the F
density (7 significant digits), and recomputed the Gilbert chain.

## Findings → disposition

### Standards axis

| # | Severity | Finding | Disposition |
|---|---|---|---|
| S-P0-1 | P0 | BestPixel admitted Q=0 (fully-clouded) observations (−1 sentinel) | **Fixed** + all-clouded-window NaN test (both policies) |
| S-P1-1 | P1 | Same-signature kernel duplication across shared/static libs (ODR/interposition) | **Fixed**: smoothing seam moved to nested `sicnu::temporal::d16` (D-160-4 revised) |
| S-P1-2 | P1 | `std::clamp` UB in scrubber at width < 10 | **Fixed** (bounded lo/hi) |
| S-P1-3 | P1 | trend_analysis contract promised NaN outputs, impl returned zeros | **Fixed** + NaN contract test |
| S-P1-4 | P1 | GeoError could escape readChunk (complex dtypes, budget) | **Fixed**: complex dtype refused at open; read failures are typed non-contributions |
| S-P1-5 | P1 | Agent climatology NaN poisoning → silent z=0 | **Fixed**: skip non-finite climatology months; non-finite target month → structured rejection + test |
| S-P1-6 | P1 | `sicnu::gui` / `sicnu::agent` vs repo naming conventions | **Accepted deviation**, documented (D-160-11) — spec-mandated names |
| S-P2-1..12 | P2 | dead `rssAtSplit`; minSegment doc; refit-failure hygiene; doyOf wrap comment; toPixel O(n²) + sentinel; STARFM doc/`numClasses`; CrossingTruth edge 708→352; GDAL PUBLIC redundancy; iterations doc; SG divergence doc; play() unit comment; DECISIONS source drift | **Fixed** (all except two accepted-and-documented: `numClasses` spec field kept, wall-clock margins kept) |

### Spec axis

| # | Severity | Finding | Disposition |
|---|---|---|---|
| X-P0 | — | **P0: none** — core numeric chains independently re-derived | — |
| X-P1-1 | P1 | LOS cross-year +365 double-count on the absolute axis | **Fixed** + cross-year season test (los = eos − sos exact) |
| X-P1-2 | P1 | BestPixel Q=0 admission (same as S-P0-1) | **Fixed** (same change) |
| X-P1-3 | P1 | STARFM documented fallback not implemented (dead branch) | **Fixed** per header contract + heterogeneous-window fallback test |
| X-P2-1..8 | P2 | oracle edge constant; minSegment doc; deepest→nearest valley; MAD even median; magnitude wording; trend allocation comment; seasonal-MK caveat; curvature-extreme wording | **Fixed** (wording/correctness) + trend_inspect now reports the seasonality caveat |

## Verdict

- P0 = 0 (1 found → fixed with regression test)
- P1 = 0 (7 unique found → 6 fixed with tests/contracts, 1 documented deviation)
- P2 = 0 open (16 raised → 14 fixed, 2 documented as accepted)
- Reviewer-confirmed erratum rulings: D-160-5 (Gilbert S=42/Var=124.0/Z=3.6818)
  and D-160-6 (exact F p-value over asymptotic MOSUM tables) both upheld.

Post-remediation acceptance gate: `ctest -R "test_d16_|test_whittaker|test_bfast|
test_phenology|test_virtual_cube" -j1` → **100% tests passed out of 10**
(see EVIDENCE.md for the full log excerpt and RSS audit).
