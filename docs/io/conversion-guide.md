# Conversion & Export Guide

All conversions go through the **authoritative `io:*` operators** (or the
equivalent `sicnu::geo::convert` API). GDAL's own utility kernels do the math;
the foundation adds the CRS/NoData policy, staging, validation and atomic
publish. Never re-implement translate/warp/reproject at call sites.

## Operator reference

| Operator | Kernel | Determinism | Notes |
|---|---|---|---|
| `io:translate` | GDALTranslate | bit-exact | format/bands/size/CRS assignment |
| `io:warp` | GDALWarp | tolerance | `targetCrs` mandatory — never guessed |
| `io:reproject` | GDALWarp | tolerance | minimal CRS-explicit contract; `srcCrsOverride` is the only missing-CRS fallback |
| `io:clip` | GDALWarp (-te, near) | bit-exact | extent in source CRS; refuses CRS-less input without `srcCrsOverride` |
| `io:convert_format` | translate / streaming vector writer | bit-exact | raster+vector auto-detected |
| `io:build_overviews` | GDALBuildOverviews | tolerance | in-place, derived data only |
| `io:make_cog` | COG driver + validator | bit-exact | see cog-guide.md |
| `io:vector_convert` | streaming reader→writer | bit-exact | declared CRS transform, attribute filter, clip box, batched |
| `io:inspect` | canonical inspection | read-only | no full scan |
| `io:doctor` | structured diagnostics | read-only | severity-ranked findings |

## Atomicity

Every producing operator: stage beside target → write → flush → fsync →
validate → publish (rename / dataset-group move with main file last).
`cancel` (operator cancel or pipeline teardown) discards staging and leaves
any existing target untouched. Failure-path coverage: `tests/test_io_atomic_failures.cpp`.

## Examples

```json
{"operator": "io:make_cog",
 "params": {"input": "raw/product.tif", "output": "cog/product.tif",
            "preset": "lossless_scientific"}}

{"operator": "io:reproject",
 "params": {"input": "in.tif", "output": "out.tif",
            "targetCrs": "EPSG:32648", "resampling": "bilinear"}}

{"operator": "io:vector_convert",
 "params": {"input": "sites.gpkg", "output": "sites.geojson",
            "driver": "GeoJSON", "where": "zone = 2"}}
```

CLI equivalents for inspection:

```
sicnu_geo_rs_cli data inspect <dataset> [--stats] --json
sicnu_geo_rs_cli data doctor  <dataset> [--stats] --json
```

Exit codes follow the SDK contract (`0` ok, `2` invalid input / refused by
policy, `3` execution failure, `4` cancelled).

## Determinism grades (ADR 0124 vocabulary)

Pure copy/convert paths are `bit-exact`. Anything through GDALWarp's float
resampling is `tolerance` — repeated runs may differ within documented float
tolerance because GDAL parallelizes internally.
