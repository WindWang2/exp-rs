# Platform Matrix — Verification Platform 8.0 (task A)

Local-first, honest-by-default cross-platform matrix. Every row states what
is CLAIMED, what is EXECUTED locally, and where the evidence lives.

## Supported-combination claims (as built today)

| Combination | Claim source | Executed locally (this track) | Evidence |
|---|---|---|---|
| Linux / GCC / Release / GDAL 3.13 | CI tier1 (`ci.yml`), presets | attempted — GCC 16.2.1 exhibits flaky internal compiler errors (segfaults in the diagnostic path) on vendored QGIS TUs; NOT usable as this track's gate | recorded in this file + `.planning/verification-platform-8/PERFORMANCE.md` |
| Linux / Clang / Release / GDAL 3.13 | local `build-clang` precedent | YES — primary local lane for 8.0 | ladder JSON + READINESS report |
| Windows / MSVC / vcpkg deps | docs/verification/PLATFORM_EVIDENCE.md (7.0 dev workstation) | NO (host limitation) | prior-track evidence + the compile guards below narrow the residual risk |
| macOS / AppleClang / Homebrew GDAL 3.13 | PR #834 evidence | NO (host limitation) | gdal_compat.h keeps the 3.8→3.13 ladder centralized and testable |

## The three post-7.0 failure classes and their 8.0 guards

| Class | Incident | Guard (this track) |
|---|---|---|
| F1 missing standard-library includes (#833) | `diagnostic_report.h` lacked `<cstdint>`; `remote_source_validator.cpp` lacked `<iomanip>` — compiled only via transitive includes on one stdlib | L0 header self-containment probes: every nominated first-party leaf header compiles as the FIRST include of a cold TU (`sicnu_header_probes` target, generated in `tests/CMakeLists.txt`) |
| F2 GDAL version drift (#834) | `range_cache.cpp` VSI API ladder (Ubuntu 3.8 vs Homebrew 3.13) | `src/geospatial/util/gdal_compat.h` — ONE named-macro seam (`SICNU_GDAL_VSI_*`, `SICNU_GDAL_INT64_DATATYPES`); first-party geo code migrated onto it; `test_portability_contract` proves macro coherence + the version-selected handler installs/uninstalls on the linked GDAL |
| F3 platform-conditional rot (#835) | `_WIN32` branch broke the POSIX build (namespace structure) | L0/L1/L2 compile+run on the OTHER host compiler lane (Clang) before merge; the ladder's L0 lane is the cheap canary. (Static MSVC parse is out of local reach and stays documented as a caveat.) |

## GDAL capability breakpoints (gdal_compat.h)

| Macro | Minimum GDAL | Meaning |
|---|---|---|
| `SICNU_GDAL_INT64_DATATYPES` | 3.5 | `GDT_Int64`/`GDT_UInt64` exist |
| `SICNU_GDAL_VSI_REMOVE_HANDLER` | 3.9 | `VSIFileManager::RemoveHandler` |
| `SICNU_GDAL_VSI_HANDLE_ERR_API` | 3.10 | `ClearErr()`/`Error()` on `VSIVirtualHandle` |
| `SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR` | 3.12 | `Open`/`OpenStatic` return `VSIVirtualHandleUniquePtr` |
| `SICNU_GDAL_VSI_HANDLE_READ_BYTES` | 3.13 | handle `Read`/`Write` are byte-oriented |

Rule: first-party code NEVER writes a raw `GDAL_VERSION_NUM` breakpoint; it
includes `geospatial/util/gdal_compat.h` and asks for the named capability.
(The vendored QGIS tree keeps its own ladders — out of scope by ownership.)

## Feature-toggle surface

| Toggle | Default | Effect on verification |
|---|---|---|
| `ENABLE_TESTS` | ON | test targets exist |
| `SICNU_BUILD_OTB` | OFF | OTB provider lanes (ci-full only) |
| `SICNU_EMBED_PYTHON` | OFF | python worker lanes (ci-full only) |
| `ENABLE_SANITIZERS` | OFF | sanitizer-debug preset (ASan+UBSan on GCC/Clang) |
| `Qt6::Network` | found | HTTP model provider compiles vs stubs (`SICNU_WITH_HTTP_PROVIDER`) |

Absent optional features shrink the *evidence* set, never silently: the
readiness report lists any lane artifact that is not built as
`not-built`/`skipped` — a distinct verdict from `passed`.
