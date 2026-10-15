# R14-A Final Report — 遥感科学算法与基础处理链数值收敛

Track A of R14. Branch `hardening/r14-A-science-processing`, base origin/master `f72f0c117`
(verified unchanged by re-fetch before push). 16 commits. All production edits inside the
track's writable scope (src/operators/rs/**, src/processing/algorithms/**, src/analysis/**,
tests extending existing files, 3-line .gitignore whitelist per repo convention).

## Delivered fixes (each RED→GREEN×2, see TEST_RESULTS.md)

1. **temporal_index_series DN probe (#856 parity)** — undeclared negative fills no longer flip
   unit-reflectance scenes into the DN divisor (whole-series EVI/SAVI corruption).
2. **local RX diagonal zero-variance** — per-window degeneracy reported unscored per the header
   contract instead of aborting the whole raster with ComputationError.
3. **sam_classify per-band NoData wash** (+ refs washed against the same table) — heterogeneous
   declarations honored; sentinel-bearing refs rejected uniformly.
4. **continuum_removal per-band NoData wash + conditional NaN void tag** — band-k voids no
   longer enter the convex hull; #473's absent-NoData contract preserved.
5. **radiometric_qa NoData masking** — declared fills report FlagNotFinite (invalid data), not a
   false FlagNegative physics anomaly.
6. **image_fusion per-band read-boundary wash** (+ HPF halo, review F1) — every method (incl.
   HPF) voids pixels per each band's own declaration; NaN working sentinel with isfinite
   guards throughout; NaN band tags.
7. **OBIA/segment CSV writers stage-then-publish** (×4) — mid-run failure keeps the previous
   output; no truncated CSV at the final path.
8. **KMeans backend RNG pin** — reproducible centers/labels/remap across runs.

## Dedup

- #1501's rs fixes (sar per-band sentinel, band-select NoData propagation) verified present in
  BASE_SHA — not re-done.
- #1450 radiometric vocabulary, #1467 preflight provider URIs: verified FIXED_ON_MASTER; the
  vocabulary contract test ran green as baseline verification.
- #1467 temporal_cube catch-all: #1505's file list owns it (out of scope, read-only).
- Open PRs #1504/#1505: zero file overlap (their lists checked live).

## Verification summary

- 11 test binaries fully green at final state (~146k assertions), including test_rs_operators
  (9066/9066) and the science baseline targets (band_math, e2e_science_numerics,
  radiometric_vocabulary_contract).
- Independent review round 1: FAIL (2×P1) → both fixed + P3; round 2: see REVIEW.md.
- One self-inflicted regression caught by the verification loop itself (#473 tag pin) and closed
  before delivery; fixture-layer GTiff/VRT NoData pitfall documented and fixed.

## Residual / honest limits

- Fusion resample-then-mask: GDAL resampling may blend sentinel edges before the wash (exact
  matches only) — pre-existing, unchanged, documented.
- A7a CSV atomicity is pinned by a success-path contract test (no fault injection in-repo).
- sam_classify undeclared-band policy (NaN-only) still differs from spectral_similarity's
  (-9999 fallback): deliberately preserved to avoid altering existing outputs; recorded for a
  future policy round.
- No performance claims made (correctness/durability round); WP12 profiling deferred.
- Remote CI not waited on (per track rules); local builds/tests are the evidence.
