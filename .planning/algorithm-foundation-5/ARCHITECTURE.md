# Architecture Plan — where Foundation 5.0 code lives

## Placement rules (derived from baseline conventions)

1. **Kernels** (pure, GDAL-dataset-free where possible) live in
   `src/processing/algorithms/` next to their family (e.g. new
   `topographic_correction.cpp`, SAR additions in `sar/`, temporal in
   `temporal/`). Shared cross-family primitives go in new
   `src/processing/algorithms/primitives/` (histogram, percentile, window,
   morphology, connected_components, distance_transform) with a dedicated
   CMake object/static lib so operators and kernels both link it.
2. **Operators** live in `src/operators/rs/` one file per operator (or one
   file per tightly-coupled alias family, matching `rs_band_tools_operators`
   precedent), registered in `rs_operators_init.cpp`.
3. **Streaming**: any new operator over rasters uses
   `GdalBlockStream`/`GdalMultibandBlockStream` + `GdalStreamingOutput`
   (abandon() on failure) — memoryPolicy `Streaming`; declared in the
   operator header like `rs_change_primitives.h` does.
4. **Grid/radiometric preflight** via `gridFromDataset`/`compareGrids`; no
   inline size/CRS comparisons (review-rejected pattern per policy doc).
5. **NoData** via `nodata_utils.h`; outputs NaN-NoData for continuous bands.
6. **Classification backends** extend `src/analysis/classification/
   rs_classifier_backend*` factory — no ML code inside operators.
7. **Tests** in `tests/` (Catch2), named `test_<family>5.cpp` where a new
   file is needed; prefer extending existing family files when the fixture
   taxonomy is already there. Benchmarks extend `benchmarks/*.json` pattern.
8. **Docs**: every new operator family updates `docs/processing/`
   (validation-policy §3 snapshot, family-specific policy pages).

## Determinism & resources

- Determinism grades per ADR 0124 vocabulary: bit-exact for fixed-order
  streaming kernels; tolerance (locked ε) for iterative algorithms (ISODATA,
  ΓMAP, FCLS) — declared in operator metadata + pinned in tests.
- No O(raster) allocations; tile/blocked processing with bounded queues;
  cancel checks per tile via `RSOperatorContext` progress seam.
- Temporal O(T²) methods (breakpoints, seasonal MK variants) get explicit
  scene-count guards + complexity docs.

## New primitives lib sketch

```
src/processing/algorithms/primitives/
  histogram.h/.cpp        (binning, range policy, NaN/sentinel exclusion)
  percentile.h            (header-only quantile interpolation, method enum)
  window.h                (edge policy enum + halo size contract)
  morphology.h/.cpp       (erode/dilate/open/close, connectivity enum)
  connected_components.h/.cpp (two-pass union-find, label map)
  distance_transform.h/.cpp   (exact Euclidean; float; NaN/sentinel aware)
```

Consumers: threshold families (B/G/I), qa_mask cleanup (B), sieve/fill/clump
(G), zonal/focal stats (G), local extrema (G), terrain F.4/F.5, SAR dual-pol
local stats (D.2), classification post-processing (H).

## Range-Doppler additive seam (Milestone D.3)

`src/processing/algorithms/sar/sar_orbit.h`: state-vector struct + parser for
declared metadata (`SICNU_SAR_ORBIT_*` keys); operator-side typed refusal
when absent. Geometry subset implemented only for declared-metadata scenes;
extension contract documented in `docs/processing/sar-domain.md` (new page).
