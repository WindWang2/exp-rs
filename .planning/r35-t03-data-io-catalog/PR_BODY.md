## Problem

R35-T03 static read of the data/IO/catalog path (base 120677e6) found five real, user-observable gaps that this PR closes:

1. **Non-finite query bounds pass validation** — `CatalogQuery::validate`/`AssetQuery::validate` compare NaN/±Inf with `>`/`<`, which is always false, so a NaN bbox or NaN/Inf cloud ceiling passed admission and reached every backend scan.
2. **Instrument filtering disagreed between backends** — `recordMatchesQuery` matched instruments by SUBSTRING over the comma-joined enrichment string while `itemMatchesInstruments` matches exact tokens: querying `MSI` matched a `MSI2` record on the in-memory backend but never on local/remote STAC; multi-word names broke the other way.
3. **Catalog JSON reads were bounded by a stat, not the read** — `readJsonFile` only refused when `VSIStatL` reported >16 MiB up front, then read to EOF: a growing file, or a transport whose stat under-reports, materialized unbounded bytes before JSON parsing.
4. **`VSIReadDirRecursive` materialized whole trees before truncating** — `openCatalogService`'s plain-directory scan enumerated the entire tree into memory and only then dropped entries past `localMaxFiles`/`localMaxDepth`; symlink loops recursed unboundedly at the VSI layer.
5. **Far-edge raster windows still overflow int** — `clampWindowToRaster`/`planTileWalk`/`TilePlan::slice`/`readBlock` computed `xOff + width`, `width + tileWidth - 1`, `tileX * tileWidth` in int (UB for `{xOff=2, width=INT_MAX}`); `validateWindow` got the int64 fix in #1703-1 but these four did not. `RasterReader::open` also re-opened the path via `inspectRaster` for metadata — a replacement between the two opens mixed generation-A pixels with generation-B metadata.
6. **`DatasetManifest::fromJson` folded wrong-typed fields into defaults** — `toArray/toObject/toInteger` defaults turned a string `entries` into "no entries", a negative revision into "revision 0 (not pinned)", a string `schema_version` into "version 0".

## What changed

- `CatalogQuery::validate`/`AssetQuery::validate`: NaN/±Inf bbox values and non-finite `cloudCoverMax` are typed `GeoError(InvalidArgument)` before any IO; legal 6-value bbox and anti-meridian tolerance unchanged.
- `recordMatchesQuery`: instruments match exact tokens (comma-split, edge-trimmed, case-insensitive, interior spaces preserved) — the same predicate as `itemMatchesInstruments` on every backend.
- `readJsonFile`: the 16 MiB cap now binds the actual read (stat-sized buffer +1, growth/oversize typed refusals, read errors distinct from invalid JSON).
- Plain directory walk: per-directory `VSIReadDir` BFS (sorted, visited set, depth/file/directory-visit caps) instead of whole-tree `VSIReadDirRecursive`; truncation surfaces via `CatalogPage::truncatedByCap`; symlink loops bounded by the depth cap (previously unbounded VSI recursion).
- `clampWindowToRaster`/`planTileWalk`/`TilePlan::slice`/`readBlock`: int64 computation with representability narrowing; out-of-grid tile indices throw typed errors (all existing callers loop in-grid).
- New `inspectOpenRaster(handle, path, options)` in canonical_metadata; `RasterReader::open` derives metadata from its own open handle — single open, single generation.
- `DatasetManifest::fromJson`: strict typed decoding with stable `dataset.manifest_invalid`/`dataset.manifest_version` diagnostics; revision 0 stays the legal "not pinned"; values ≥ 2^63 refused (they wrap negative on the qint64 encode side).

## Evidence (local, final HEAD 62d4355, Debug, resource_guard double lock, -j4)

- New regressions (run twice, identical results): `test_r35_t03_data_io_catalog` **8/8 PASS** (CTest #60-67, 0.37s) + `test_r35_t03_dataset_manifest` **3/3 PASS** (CTest #265-267, 0.97s).
- Affected existing suites, all PASS at this HEAD:
  - `test_io_fabric_catalog` 11/11 (incl. plain-scan + instrument + containment walks)
  - `test_io_canonical_metadata` 5/5, `test_io_catalog_query` 9/9, `test_io_raster_contract` 16/16
  - `test_dataset_core` 20/20 (520 assertions), `test_dataset_version_migration` 4/4, `test_contract_fuzz_data` 3/3
  - `test_asset_catalog_index` 8/8, `test_workspace_catalog` 7/7, `test_adversarial_m3` 6/6
  - `test_r15_B_geodata_persistence` 19/20 — the one failing case is the pre-existing R4-03 SSRF order-dependent SIGABRT: **proven not from this change** by reverting all 7 changed files to base 120677e6, rebuilding, and reproducing the identical failure (2 cases / 1 failed); the case passes in isolation; this diff touches zero remote/network-policy code.
- Independent review R1 (fresh-context reviewer, full diff read): APPROVE_WITH_FIXES — 1xP2 + 11xP3; fixes in b3e4b7c34b/62d4355e28, no-change dispositions documented with evidence. R2 re-review bound to final HEAD 62d4355 (summary in PR).

## Boundaries / compatibility

- No API removals; `inspectRaster` unchanged for all existing callers; metadata identical for non-replaced files (same open flags, same default options).
- Directory-visit work bound now couples to `localMaxFiles` (documented in `CatalogServiceOptions`); a tree with more than `localMaxFiles` directories reports truncation honestly instead of walking forever.
- In-repo manifest producers all emit integral revisions — verified; strictness only affects hand-authored/corrupt payloads.
- Symlink walks follow links like the old scan (lexical containment, not a sandbox; documented).

Base: `120677e6` · R1-reviewed: `8d2bcec4` → fixes `b3e4b7c` → final head `62d4355`.
