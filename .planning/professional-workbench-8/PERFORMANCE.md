# PERFORMANCE — Professional Workbench 8.0

Environment: Linux 6.18 LTS x64, 16 cores (shared with two concurrent 8.0
track builds during measurement), 62 GB RAM, gcc Release `-O3 -DNDEBUG`,
offscreen QPA. Tests run sequentially with bounded fixtures.

## Measured (targeted suites, wall time)

| Suite | Cases | Assertions | Wall time |
|-------|-------|-----------|-----------|
| test_asset_catalog_index (incl. 200k-record scale case) | 8 | 327 | 1.4 s |
| test_asset_preview_service | 8 | 1104 | 0.9 s |
| test_context_facts_8 | 3 | 14 | 0.4 s |
| test_schema_form_4 | 11 | 80 | < 1 s |
| test_schema_form_builder_v2 (parity) | 8 | 48 | 0.3 s |

## Scale evidence (package D)

- `AssetCatalogIndex` with 200 000 light entries: substring filter pass and
  single-pass grouping each complete well under the 500 ms test bound (the
  full suite including GDAL fixture creation finishes in 1.4 s). Memory:
  one entry per asset (id + name + source + enums) — linear, tens of MB at
  200k.
- Panel rendering is bounded by `m_standaloneRowCap` (default 20 000 rows)
  with a truthful sentinel naming exact totals; collection children above
  `kLazyChildThreshold` (50) populate on first expand. The pre-8.0 behavior
  rebuilt every full snapshot row per refresh burst (O(all assets) snapshot
  copies); the index path performs O(assets) LIGHT comparisons plus
  O(rendered rows) widget creation per refresh.

## Bounds (package E)

- Raster preview reads: `RasterReader::readWindowResampled` with
  `OverviewPolicy::Nearest`; output bounded by the requested thumbnail size
  (≤ 1024 edge pixels) — pixel-buffer memory ≤ ~24 MiB per job at the
  largest edge (1024×1024×3 bands×8 B), independent of raster size.
- Vector previews: typed refusal above 200 000 features (pinned by test),
  render output bounded to the requested size.
- Concurrency: shared bounded `RsScanPool` (2 workers); preview jobs complete
  in ms–s and deliver via queued calls (generation guard drops stale
  results).
- Cache: LRU, 64 entries / 32 MiB by default, both bounds pinned by test.

## Known honesty notes

- One unexplained SIGABRT-in-suite event was observed once while three 8.0
  track builds ran concurrently; 14 consecutive clean runs afterward.
  Flagged in REVIEW_LOG for adversarial review; no reproducible defect found
  (the test-side defects that DID reproduce were fixed: stack-object
  deleteLater misuse and a dangling capture — see test file comments).
