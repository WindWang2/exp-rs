# ADR 0091: Grid Harmonization via `gdal:reproject` Reference Alignment

## Context

The P0 Grid Compatibility and Alignment Service (ADR-0065 / A4) validates
pixel grids but the "automatically execute the appropriate alignment
operation within workflows" half was missing: multi-date change detection,
fusion, and band math require co-registered grids, and users had to compute a
reference raster's CRS / pixel size / extent by hand and feed them into
`gdal:reproject`. The audit also found `tests/test_gdal_ortho_operators.cpp`
(orthorectification / reproject / clip / polygonize coverage) was an orphan
file registered in no CMake target — its tests were never running.

## Decision

- `gdal:reproject` gains an optional `reference` raster parameter: when
  provided, the operator derives the target CRS, pixel size, and extent from
  the reference's geotransform/projection and passes `-t_srs`, `-tr`, `-te` to
  the in-process GDALWarp path, so the output lands exactly on the reference
  grid (rotation-aware bounding box; double precision formatting for large map
  coordinates). `dstCrs` becomes optional in reference mode (warning if both
  are given and differ); the legacy `dstCrs`-only path is unchanged. The
  result gains `aligned: true`.
- The orphan `test_gdal_ortho_operators.cpp` is registered as a proper CTest
  target (`test_gdal_ortho_operators`) with the same link set as
  `test_rs_operators`, recovering its orthorectification / reproject / clip /
  polygonize coverage; a new test pins the reference-alignment path (output
  dimensions, geotransform, and CRS match the reference exactly).

## Consequences

- Grid harmonization is now an automatic workflow seam: align raster B to
  raster A before change detection / fusion by passing A as `reference` — no
  manual CRS/resolution/extent transcription.
- The recovered test suite surfaced a missing include and a missing Catch2
  header that had lain dormant; both fixed. All 12 GDAL operator test cases
  (116 assertions) now run and pass, including the pre-existing
  orthorectification GCP path.
