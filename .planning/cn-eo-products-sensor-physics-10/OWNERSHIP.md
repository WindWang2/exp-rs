# OWNERSHIP — cn-eo-products-sensor-physics-10

## This track owns (write)

| Path | Scope |
| --- | --- |
| `src/geospatial/products/` | sensor profile registry, CN metadata/adapter extensions, generation dispatch |
| `data/products/` | band_roles (migrated/extended), sensor_profiles (new) |
| `src/operators/rs/rs_gaofen_import_operator.*`, `rs_zy3_import_operator.*`, `rs_hj_import_operator.*`, `rs_cn_import_operator.h` | rebase onto ImportPlan service |
| new `src/operators/rs/rs_*_import_operator.*` for new families | new files only |
| `data/processing/algorithm_meta/capability/rs-*import*.json` | append-only capability files |
| `docs/products/`, `docs/adr/` | new ADR + product docs |
| `tests/test_cn_products.cpp`, new `tests/test_cn_*.cpp` | extend/new |
| `src/app/` product import dialog | narrow integration only if needed |

## Other tracks' authority (read-only)

| Path | Owner |
| --- | --- |
| `src/operators/rs/` SAR/temporal operator kernels | SAR/temporal tracks |
| `src/processing/algorithms/satellite_products.*` | shared kernel — consume `stackToGeoTiff`, modify only with narrow-seam justification |
| `src/processing/algorithms/spectral_library.*` + `data/spectral/` | spectral-library track (#955) — consume `SensorProfile`; additive cross-links only |
| `src/agent/` capability knowledge loaders | agent-capability track (#950) — data files are ours, loader code is theirs |
| `src/geospatial/raster/`, `util/`, `metadata/` | I/O foundation tracks |

## Shared files (conflict-sensitive)

| File | Discipline |
| --- | --- |
| `src/operators/rs/rs_operators_init.cpp` | append-only registration lines |
| `src/geospatial/products/product_adapters.h/.cpp` (`ProductKind` enum) | append-only enum values; comment markers |
| `data/products/band_roles/*.json` | keep loading; migrate content into registry without breaking old keys |
| `tests/CMakeLists.txt` + app CMakeLists | append-only target entries |
| `.gitignore` | this track's whitelist block only |
| `docs/adr/` | new numbered files only |
