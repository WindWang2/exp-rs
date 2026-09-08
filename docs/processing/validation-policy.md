# Processing: Scientific Validation & Numerical Tolerance Policy

> Owner: `src/processing/algorithms/**` + `src/operators/rs/**` (Scientific
> Algorithms & Processing Foundation 4.0). This page is the authority for how
> numeric correctness of processing kernels is validated and which tolerance
> each contract carries. Companion pages:
> [nodata-and-statistics.md](nodata-and-statistics.md),
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md).

## 1. Tolerance grades

Every numeric contract in the processing layer carries one of three grades.
The grade is a property of the *implementation contract*, never of the test's
convenience (ADR 0124 defines the operator-facing determinism grades built on
the same distinction):

| Grade | Meaning | Typical assertion | Examples |
|---|---|---|---|
| **exact** | Integer/deterministic-discrete outputs; identical bits for identical inputs | `REQUIRE(v == expected)` | class maps, masks (`rs:qa_mask`, `rs:recode`, `rs:threshold_raster`), break indices |
| **bit-exact float** | Fixed-order float evaluation, no parallel reduction; deterministic down to the last bit | `REQUIRE(v == Approx(expected).margin(1e-12))` against hand-derived closed forms, or `compareRastersBitExact` between two execution paths of the same kernel | OLS fits, Sen slope medians, normalized differences, PPI counts |
| **tolerance** | Iterative or order-sensitive float algorithm with a locked relative bound | `REQUIRE(v == Approx(expected).epsilon(1e-5..1e-3))` with the bound stated in the test | Whittaker banded solve (currently asserted with behavioral bounds at 1e-4 margins in `tests/test_temporal_fit.cpp`; a formal ε-lock is pending — see `temporal_fit.h`), refined-Lee speckle, IR-MAD iterations |

Rules:

1. A test may never widen a tolerance to make a regression pass. Widening is a
   scientific contract change and needs the contract table above updated in the
   same PR.
2. Exact/bit-exact assertions are used *only* where the implementation truly
   guarantees them (single-threaded, fixed order, no fast-math reassociation).
   Streaming code that must stay identical to a serial reference is pinned
   against it — bit-exactly via `tests/raster_bit_compare.h`
   (`compareRastersBitExact`, e.g. the spectral-index streaming path) or,
   where the kernel's grade is tolerance, with NaN-aware toleranced
   comparisons (e.g. the change-detection streaming atoms, 1e-6).
3. Hand-derived fixtures state their derivation in a comment (see the
   `sqrt(3)` RMSE case in `tests/test_temporal_fit.cpp` or the `6/15 = 40%`
   case in `tests/test_change_detection.cpp`). A reviewer must be able to
   re-derive the expected value from the comment alone.

## 2. Required fixture taxonomy (per family)

For every major algorithm family (optical preprocessing, raster math, indices,
classification, OBIA, spectral analysis, change detection, SAR, temporal,
feature engineering, terrain, fusion, import, inference) the test suite must
cover, where applicable to the family:

1. **Hand-derived tiny fixtures** — literal arrays with reviewer-derivable
   expectations.
2. **Synthetic known-answer fixtures** — generated patterns with analytic
   answers (tilted DEMs for terrain, simplex vertices for unmixing/PPI,
   planted breaks for trend segmentation).
3. **NaN / declared NoData / ±Inf cases** — missing values are part of every
   contract, not an afterthought (see [nodata-and-statistics.md](nodata-and-statistics.md)).
4. **Non-square / tile-boundary cases** — dimensions that do not align with the
   256-px streaming tile (e.g. 3×5, 7×4) so edge tiles are exercised.
5. **Scale/offset cases** — declared `SICNU_NUMERIC_SCALE`/radiometric-state
   metadata vs. undeclared inputs.
6. **Invalid-grid / radiometry refusals** — mismatched CRS, size, or band
   structure produce typed `RSOperatorError` refusals, never silent
   combination (see [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md)).
7. **Cancellation / partial-output cleanup** — cancelled runs leave no output
   file behind (TemporalOutputGuard / `abandon()` RAII contracts).
8. **Streaming-vs-reference equivalence** — the streamed path is compared to a
   serial reference bit-exactly (or within the kernel's stated tolerance).
9. **Determinism/repeatability** — repeated runs are identical; grade-declared
   tolerance algorithms are stable within the locked bound.
10. **Realistic miniature GDAL E2E** — operator-level run through real
    GeoTIFF I/O at least once per operator family.

## 3. Current family coverage snapshot

Maintained in the epic planning dossier; the durable summary:

- Temporal: kernel numeric references with locked tolerances
  (`tests/test_temporal_fit.cpp` — including the kernel-level #759 NaN-gap
  RMSE regression and Sen/Mann-Kendall hand-derived cases), operator-level
  E2E for the family (`tests/test_temporal_algorithms.cpp`).
- Change detection: hand-derived atoms, streaming-vs-full-frame equivalence
  (NaN-aware, 1e-6/1e-7; IR-MAD 1e-3/1e-4) across 256-px tile boundaries,
  grid-refusal contract, threshold valid-observation counting.
- SAR: hand-derived calibration/texture/terrain formulas, IEEE NaN/domain-edge
  contracts for pure conversions, operator-level NaN + typed refusal coverage.
- Spectral: hand-derived indices, SAM/SID known answers, RX anomaly,
  continuum removal; endmember PPI streaming-vs-kernel agreement.
- Classification/OBIA: pipeline E2E + accuracy metrics; grid refusals at the
  operator boundary.
- Foundation 5.0 additions (per family): shared primitives closed forms
  (`test_primitives5`), topographic-correction known-answer E2E +
  illumination closed forms (`test_topographic_correction`), index-family
  hand formulas (`test_spectral_indices`), spectral-derivative closed forms
  + index-space refusal (`test_spectral_derivative`), matched-filter/ACE
  identity-background cosines + planted-target E2E
  (`test_spectral_detection`), dual-pol and terrain-geometry closed forms +
  E2E (`test_sar_foundation5`), temporal CUSUM/EWMA/seasonal-MK hand series
  (`test_temporal_algorithms`), terrain curvature/hillshade/flow analytic
  surfaces (`test_terrain_foundation5`), and raster-spatial operator E2E
  (`test_raster_spatial5`).

Known thin areas are tracked as issues, not silently tolerated.

## 4. What belongs in this framework

A PR that changes a numeric result, a NoData/denominator convention, an output
dtype policy, or a grid-compatibility behavior must add or update the fixture
that pins it. If a reviewer cannot tell which fixture fails when the contract
breaks, the contract is not yet validated.
