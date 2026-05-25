# P0 Dependency Closure Log

## Iteration Summary

| Pass | Top Error | Fix |
|------|-----------|-----|
| 1 | `scripts/process_function_template.py` missing | Copy `qgis_ref/scripts/` |
| 2 | `fatal error: QtCrypto` | Add `/usr/include/qt6/Qca-qt6/QtCrypto` via `target_include_directories` |
| 3 | `casMerge` not in `QgsAuthCertUtils` | `configure_file` ran before `set(HAVE_AUTH TRUE)` → moved after options |
| 4 | `spatialindex/SpatialIndex.h` missing | `WITH_INTERNAL_SPATIALINDEX=ON`, add `external/spatialindex/include` to includes |
| 5 | `lazperf/vlr.hpp` missing | Add `external/lazperf` to includes |
| 6 | `qgsvirtualpointcloudprovider.h` missing | Add `src/core/providers/{vpc,copc,ept}` to target includes |
| 7 | `vector_tile.pb.h` (vectortile sources) | Comment out vectortile `QGIS_CORE_SRCS` entries (`# ANTIGRAVITY: excluded P0`) |
| 8 | Built `.a` instead of `.so` | `set(LIBRARY_TYPE SHARED)` |

## Final Result

- **Link status:** SUCCESS — `libqgis_core.so` (112 MB)
- **Compiled .cpp references in CMakeLists:** 1023
- **Removed from SRCS:** 22 vectortile sources (spec §5)
- **Build date:** 2026-05-25
- **Wall-clock to link:** ~1 day (well within 10-day box)

## P1-P4 Recalibration

Realized closure: ~1000 C++ files compiled successfully.
Effort estimates from spec §16 remain on track — no revision needed.
P1 (expression engine + broader pybind11 surface) can proceed as planned.
