# End-to-End Examples (Foundation 5.0)

Every example below is exercised by a test in this repository — the doc and
the code cannot drift without a failing test.

## Example A — Sentinel-2: probe → metadata → band selection → window read

1. **Probe** the product directory:
   `data probe <...SAFE>` → `product.kind = sentinel2_safe`, `is_cog` per band
   (structural), format certification.
2. **Enumerate** constituents:
   `data product describe <...SAFE>` → R10m/R20m/R60m groups as distinct
   assets; `B04 → red (665 nm, 10 m)`, `B08 → nir (842 nm, 10 m)`.
3. **Window read** a selected band pair through `RasterReader::readWindow`
   (stored values; scale/offset only through the explicit helper).
4. **Register** the imported bands through the DataManager import flow
   (`CollectionImportService` probe → commit) — the asset layer owns identity,
   the foundation owns semantics.

Proven by: `tests/test_io_product_registry.cpp` (steps 1–2),
`tests/test_io_raster_contract.cpp` (step 3).

## Example B — Sentinel-1: probe → polarization assets → operator-ready input

1. `data probe <...SAFE>` → `sentinel1_safe`.
2. `data product describe` → measurement tiffs carry native band names
   `vv`/`vh` (canonical SAR role vocabulary), annotation XML and calibration
   references are listed as separate constituents.
3. Hand the selected measurement asset (plus annotation/calibration
   references) to a Track A operator — the calibration math itself lives in
   Track A; the foundation supplies correct product semantics.

Proven by: `tests/test_io_product_registry.cpp` ("Sentinel-1 SAFE enumerates
measurements, annotations and calibration").

## Example C — COG over local HTTP: one window, no full download

1. `probeRemote(url)` → reachable, size, `accepts_ranges` (bounded fetch:
   ≤ `maxProbeBytes` reach this process even against a hostile origin).
2. `RasterReader::open("/vsicurl/<url>")`, `readWindow(64×64)` at the origin.
3. Byte accounting (test-local HTTP fixture) proves the window read serves
   **far less than half** of the 4 MB payload — the no-silent-full-download
   gate.
4. Rangeless origins are detected by the probe layer (`accepts_ranges =
   false`) so callers can warn before any pixel access.

Proven by: `tests/test_io_remote_range.cpp`.

## Example D — NetCDF: enumerate → choose variable → time slice

1. `MultidimView::open(path)` — dimensions/variables/attributes enumerated
   lazily (no array data read).
2. Select a variable; fix every non-spatial dimension by name except time.
3. `readTemporalOrLevelSlice(variable, "time", index, maxCells)` — the slice
   is bounded by the cell budget and returns an explicit rows×cols grid.
4. CF honesty: axes without a readable coordinate variable stay "unlabeled
   indices" — the layer never invents timestamps (declared units are
   carried; coordinate-value matching is a documented follow-up).

Proven by: `tests/test_io_multidim.cpp` (driver-gated).

## Example E — export: COG → validate → reopen → compare

1. `makeCog(input, target, LosslessScientific)` — staged → created through
   the COG driver → validated → atomically published.
2. `validateCog(target)` — structural verdict (tiling, overviews,
   compression, predictor).
3. Reopen and compare canonical metadata (CRS, size, bands, NoData,
   scale/offset, band roles) — an export that loses declared metadata fails
   the round-trip matrix, not the user.

Proven by: `tests/test_io_cog.cpp`, `tests/test_io_roundtrip_matrix.cpp`,
`tests/test_io_fidelity.cpp`.

## CLI quick reference

```
sicnu_geo_rs_cli data probe <dataset>            # what is this? (format/product/COG)
sicnu_geo_rs_cli data capabilities <dataset>     # what can it do?
sicnu_geo_rs_cli data product describe <path>    # sensor product semantics + completeness
sicnu_geo_rs_cli data stac <item.json>           # STAC item → canonical preview
sicnu_geo_rs_cli data inspect <dataset> [--stats]
sicnu_geo_rs_cli data doctor  <dataset> [--stats]
```
