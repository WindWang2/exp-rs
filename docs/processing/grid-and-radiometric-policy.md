# Processing: Grid Compatibility, Resampling & Radiometric Domain Policy

> Authority for how multi-input algorithms decide whether rasters may be
> combined, when resampling happens, and how radiometric domain/scale-offset
> metadata is consumed. Companions: [validation-policy.md](validation-policy.md),
> [nodata-and-statistics.md](nodata-and-statistics.md). ADRs: 0066 (grid
> compatibility), 0091 (reproject-with-reference), 0098 (shared grid builder),
> 0114 (radiometric state).

## 1. Grid compatibility — one service, no re-derivation

All multi-input operators preflight their inputs through the shared service:

- `sicnu::processing::gridFromDataset` (`src/processing/gdal/gdal_grid_compat.h`)
  builds a `sicnu::data::RasterGrid` (CRS, geotransform, size, per-band NoData).
- `sicnu::data::compareGrids` classifies the difference and returns typed
  issues with `blocking` severity and human-readable messages.

Policy:

1. **CRS mismatch / missing CRS on a referenced input is a blocking refusal**
   where pixels are combined arithmetically (change detection, dNBR, apply
   mask, SAR incidence geometry). The refusal is a typed `RSOperatorError`;
   the message names the blocking issue and the fix (pre-align via the
   grid-harmonization seam, ADR 0091).
2. **Two unreferenced rasters** (no CRS at all) fall back to the dimension
   check and pass as compatible — they carry no contradicting geolocation.
   This is the documented exception, not a loophole: a user who georeferences
   one of the two inputs gets the refusal immediately.
3. **Same-CRS grid differences** (size/origin/resolution) are auto-alignable
   where the operator documents it (apply mask's nearest-neighbor alignment);
   algorithms that cannot align refuse. Never introduce hidden resampling:
   if a kernel does not resample, the operator must refuse, not warp silently.
4. New multi-input operators use the service; re-deriving size/CRS comparisons
   inline is a review-rejected pattern (the dNBR dims-only check that silently
   combined CRS-mismatched pairs is the canonical regression).

## 2. Radiometric domain and scale/offset

1. **Declarative metadata wins.** Import stamps what it knows:
   `SICNU_RADIOMETRIC_STATE` (e.g. `reflectance`, `dn`) and
   `SICNU_NUMERIC_SCALE` (e.g. `10000.0`). Scale-sensitive kernels (EVI/SAVI)
   consume the declared scale at the operator seam — participating bands are
   divided by the declared scale before the kernel, so the kernel sees true
   [0,1] reflectance (`rs_spectral_index_operator.cpp`, #680).
2. **The sample heuristic is a documented fallback only**: for rasters without
   declared metadata, EVI/SAVI detect the DN regime by magnitude
   (`max|v| > 5` in `spectral_indices.cpp`). It can misfire on pathological
   scenes (an all-dark DN scene); the fix is to declare metadata, not to
   widen the heuristic. New kernels must NOT add their own magnitude
   heuristics — they take an explicit scale parameter resolved by the caller.
3. **Scale/offset application** goes through `MathUtils::linearScale`
   (gain·v + bias) — one owner of the arithmetic; calibration kernels may
   inline the formula where it is part of their published equation, but the
   metadata-driven normalization paths are shared.
4. **Stored pixels are never rewritten** by index computation: normalization
   for a kernel happens on the block read, outputs are in the index's native
   range, and input metadata stays untouched.

## 3. Band roles

Band resolution for role-aware operators is delegated to the shared temporal
resolver (`sicnu::temporal::resolveBand`, `findBandWithRole`): explicit band
parameter > `SICNU_BAND_ROLE` metadata > documented cross-fallback >
positional default, with a logged warning on any fallback. An *explicit*
`band_role` that resolves nowhere is a caller error (typed refusal), never a
silent fallback to another band.

## 4. Output publication

1. Outputs are written through the streaming RAII seams (`GdalStreamingOutput`
   with `abandon()` on failure, `TemporalOutputGuard` for the temporal family,
   `closeWithError` before `commit()`). An error or cancellation must not
   leave a partial file at the output path.
2. Continuous outputs carry NaN NoData on every band (see
   [nodata-and-statistics.md](nodata-and-statistics.md)); class/mask outputs
   use the family's documented encoding.
3. Provenance: temporal operators stamp acquisition metadata and the common
   radiometric state (`writeTemporalDatasetMetadata`,
   `SICNU_RADIOMETRIC_STATE`) so downstream steps can repeat the preflight.

## 5. Enforcement

- `tests/test_raster_grid_compat.cpp` — the service contract.
- `tests/test_change_detection.cpp` — dNBR grid-refusal contract; change
  detection preflight behavior.
- `tests/test_rs_operators.cpp` — apply-mask grid refusals, mosaic CRS/pixel
  size refusals, shared-grid checks.
- `tests/test_qa_mask.cpp`, `tests/test_rs_operators.cpp` — band-role
  resolution.
