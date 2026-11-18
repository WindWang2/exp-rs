# PATH_OWNERSHIP — R35-T03
Write set (this track, all inside own worktree):
- src/geospatial/fabric/catalog_service.{h,cpp}
- src/geospatial/catalog/asset_query.cpp
- src/geospatial/raster/raster_reader.cpp
- src/geospatial/metadata/canonical_metadata.{h,cpp}
- src/dataset/dataset_manifest.cpp
- tests/test_r35_t03_data_io_catalog.cpp, tests/test_r35_t03_dataset_manifest.cpp
- tests/CMakeLists.txt (two minimal target appends only)
Active PR reservation set at start: EMPTY (0 open PRs at 120677e6; re-checked before push).
Excluded (T04): src/data/execution_fingerprint.*, OutputCommitter, src/geospatial/util/atomic_fs.* untouched.
Excluded (T06): any UI/preview/import UI, main_window_*, global TS/staging.
Untouched shared authorities: platform/portable.h, durable_sidecar.*, atomic_fs.* — only consumed.

## Reservation set update (2026-11-19, pre-push refresh)
- NEW open PR #2149 (R35-T06, i18n): files = .gitignore, .planning/r35-t06-desktop-workflows/**,
  resources/translations/sicnu_zh_CN.ts. ZERO overlap with this track's write set. Recorded;
  no re-planning needed. master still 120677e6 (no rebase).
