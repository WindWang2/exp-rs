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
   mask, SAR incidence geometry). The refusal is a typed `RSOperatorError`
   whose message names the blocking issue (dNBR), or the kernel logs the
   issue and fails the conversion which the operator reports as a typed
   error (SAR incidence). The fix is the same either way: pre-align via the
   grid-harmonization seam (ADR 0091).
2. **Two unreferenced rasters** (no CRS **and** no geotransform on either
   side) fall back to the dimension check and pass as compatible — they
   carry no contradicting geolocation. Rasters without CRS but with
   geotransforms still go through the pixel-size/origin/extent checks and
   can be refused. This is the documented exception, not a loophole: a user
   who georeferences one of the two inputs gets the refusal immediately.
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
2. **The magnitude rule is a dataset-level contract, never a per-tile guess
   (#801, Foundation 6.0)**: for rasters without declared metadata, the
   numeric domain is resolved ONCE PER RASTER before streaming — from a
   bounded decimated probe (`processing/contracts/scientific_contracts.h`,
   `domainFromMaxAbsSample`, threshold `max|v| > 5`), logged with its
   evidence and reported in the operator result (`numeric_domain`). The
   decision can never vary between tiles (the old per-block heuristic
   striped the output at tile boundaries where a block's max fell below the
   threshold). Streaming kernels take the explicit-regime variants
   (`eviUnit`/`eviDn`/…); the legacy auto forms decide from the whole buffer
   they receive and must never be called from streaming loops. New kernels
   must NOT add their own magnitude heuristics — they take an explicit
   regime resolved by the caller. It can still misfire on pathological
   scenes (an all-dark DN scene); the fix is to declare metadata.
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
positional default, with a logged warning on any fallback. Because the
positional defaults always resolve for the documented role vocabulary, an
explicit `band_role` that matches no metadata currently falls back
positionally (with the warning) rather than refusing — the typed refusal
branch exists at the operator seam for role ids the resolver cannot place at
all. Pin roles with `SICNU_BAND_ROLE` metadata or an explicit `band`
parameter when the positional convention is not enough.

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

## Categorical resampling across seams (Scientific Algorithms 7.0)

Two seams resample rasters, and their categorical policies are **declared
different on purpose** (audited 2026-09-10, no silent inconsistency):

- `rs:resample` / `rs:align` are the scientific seam: a categorical raster
  (declared or detected) with an interpolated kernel is a **typed refusal**
  (`rs_grid_operators.cpp`), tested in `tests/test_grid_operators.cpp`.
  Pass `resample=near` (or `mode`) explicitly to proceed.
- `gdal:warp` and the gdal_tools CLI wrappers are the QGIS-compatibility
  seam: GDAL itself silently honours the requested kernel, so the wrapper
  **downgrades to nearest with a logged warning** when the source declares
  categorical (`gdal_operator_utils.h`) — refusing there would break
  QGIS-style workflows that rely on GDAL semantics.

Rule of thumb: scientific products resample through `rs:resample`; the
`gdal:` family is a compatibility surface and its categorical downgrade is
always logged. Both behaviours are pinned by tests and neither may change
without touching this page.
