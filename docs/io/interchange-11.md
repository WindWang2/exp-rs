# Geospatial I/O, COG & Interchange 11.0 — Guide

Platform 11.0 deepens the I/O foundation along four axes without duplicating
any existing authority: **provenance sidecars** (finalize manifest),
**dataset-level resume** (staging ledger), **explicit COG production knobs**
(option planner), and **capability-gated interchange** (vector routing,
subdataset inventory, validated metadata write-back, boundary path guards).

All new code lives in `src/geospatial/io/` (library) and the `io:*` operator
family; the atomic publish pipeline, `ResourceUri`, `FormatRegistry`,
`validateCog` and the canonical metadata model remain the only authorities.

## Finalize manifest (`io/finalize_manifest`)

Every atomically published dataset can carry a provenance sidecar
`<name>.sicnu-manifest.json`:

```json
{ "schema_version": 1, "producer": "io:make_cog", "driver": "GTiff",
  "shape": {"width": 1024, "height": 700, "band_count": 1, "dtype": "Byte"},
  "crs": "EPSG:32648", "creation_options": ["COMPRESS=DEFLATE", "..."],
  "dataset_sha256": "<64 hex>", "dataset_bytes": 123456,
  "finalized_utc": "2026-09-16T05:41:03Z" }
```

* the digest is a streamed SHA-256 (1 MiB chunks, O(1) memory) of the exact
  bytes to publish; the manifest rides the group transaction (sidecars
  publish FIRST, main LAST), so a published dataset never lacks its manifest;
* `verifyDataset(path)` (operator `io:verify_dataset`) recomputes the digest
  and re-checks the declared shape through the canonical inspector. A missing
  manifest fails verification — `allowMissingManifest` reports the absence as
  `manifest_missing_allowed` but never turns it green;
* `verifyDataset` is the oracle for the fail-closed contract: a flipped byte
  → `digest_mismatch`, a shape drift → `shape_mismatch`, a corrupt manifest →
  `manifest_invalid`.

## Staging ledger, attach & sweep (`io/stage_ledger`)

`recordStaged()` journals a staged transaction next to its TARGET
(`<name>.sicnu-stage-ledger.json`): runId, producer, staged path, declared
driver/shape. After a crash:

* `attachExisting(finalPath)` is the resume trust boundary: the staged file
  must still exist, must OPEN in GDAL, and driver/shape must match the
  journal (`driver_mismatch`/`shape_mismatch`/`staged_unopenable`/...).
* `finalizeAttached()` runs digest → manifest sidecar → fsync → atomic group
  publish → mark `finalized`. `discardAttached()` cancels: staging discarded,
  journal removed.
* `sweepOrphans(directory, remove)` reports `".tmp"` staging files, their
  sidecars and stale ledgers/manifests with redacted display paths; removal
  is explicit opt-in (`remove=true`).

This is a library seam for the execution/agent runtime — it is NOT a
scheduler, and chunk-level resume stays in `src/runtime/chunk` (G01 domain).

## COG production options (`io/cog_options`)

`io:make_cog` gains `blocksize` (power of two, 128..4096), `overviews`
(default true; `false` emits `OVERVIEWS=NONE` and the pre-publish validator
then refuses images beyond the small-image allowance — fail-closed),
`deterministic` (`NUM_THREADS=1` + pinned DEFLATE `deflateLevel`; byte-
identical output **within one GDAL/libtiff build**) and `creationOptions`.

The planner merges preset + knobs with REPLACE semantics: the COG driver
reads creation options first-match-wins, so an appended duplicate `-co` is a
dead letter — the planner replaces preset values in place and annotates every
key's provenance (`preset` / `override` / `caller`) in the returned
`cog_plan`. NoData/alpha are not planner options: they travel from the source
dataset only (declared-only policy).

## Capability-gated interchange (`io/vector_interchange`, `io/subdatasets`,
`io/metadata_patch`)

* **Vector routing**: `io:convert_format` routes by capability
  (GDAL `DCAP_VECTOR` + `DCAP_CREATE`) instead of a hard-coded driver-name
  list — GeoParquet/CSV route correctly where the build supports them, and a
  declared-but-unwritable vector driver fails with a typed reason
  (`driver_missing` / `not_vector` / `not_create_capable`) instead of
  silently re-routing into the raster kernel.
  `vectorInterchangeCapabilities()` exposes the full report.
* **Subdatasets**: `io:subdatasets` inventories HDF/NetCDF/VRT subdatasets
  (cap 64, honest `truncated` flag, `ResourceUri` classification, redacted
  display) and — with `select` — projects one entry into the canonical
  metadata model. Non-selector strings are refused at `inspectSubdataset`.
* **Metadata write-back**: `io:metadata_patch` applies whitelist fields only
  (`scale`, `offset`, `unit`, `nodata`, `role`, `wavelength_nm`, `fwhm_nm`,
  `color_interpretation` per band; `sensor`, `platform`, `product_id`,
  `processing_level`, `acquisition_time` as dataset stamps). Bad batches are
  refused BEFORE the dataset is opened for update; drivers without update
  support (or read-only media) get a typed refusal — never a staged full
  rewrite of a huge file (crash-window trade-off is documented in
  DECISIONS D-004). Read-back runs through a fresh read-only open, and a
  finalize manifest is refreshed (digest + `patches[]` history) so
  `io:verify_dataset` stays truthful afterwards.

## Boundary path guards (`io/param_guard`)

`io:*` operators validate path parameters at the boundary through the
`ResourceUri` authority: unclassifiable input is refused with the parser's
reason, remote write targets are a typed refusal (`remote_write_offline_policy`),
read-only projection kinds (`vrt://`, STAC) cannot be write targets, output
directories must already exist (no implicit directory creation), and every
error surface carries the credential-redacted `display()` form.

## Tests

`test_io_param_guard`, `test_io_finalize_manifest`, `test_io_stage_ledger`,
`test_io_cog_options`, `test_io_vector_interchange`,
`test_io_subdataset_inventory`, `test_io_metadata_patch` (plus the extended
`test_io_operators`) pin every contract above with known-answer oracles
(FIPS 180-4 vectors, byte-flip tampering, crash-shape attach failures,
deterministic double-create digests) — oracles are always independent of the
implementation under test.
