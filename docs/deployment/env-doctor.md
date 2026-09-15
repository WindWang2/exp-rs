# env-doctor — first-run environment self-check

**Status**: contract · **Introduced by**: Deployment 11.0 (F19) · **Surface**:
`sicnu_geo_rs_cli env-doctor [--json]` (also embedded in the offline bundle's
`--check-runtime` build step and `VERIFY.ps1 -Runtime`).

## Problem statement

A classroom or field machine fails in ways the application previously could
not explain: a GDAL build without GPKG support, a missing `proj.db`, an
unwritable temp directory, Chinese paths that break on the wrong codepage, a
platform plugin directory the windeployqt run never produced. Before
env-doctor, every one of these surfaced as a loader dialog, a raw GDAL error,
or a silent CRS assertion failure. The missing-DLL case in particular was
diagnosed by the Windows loader with no pointer to the actual missing item.

## What it checks (and what it never does)

`env-doctor` is **report-only** — it never enables or disables anything
(the offline gate, for instance, is reported, never toggled). Probes:

| check | severity on failure | notes |
| --- | --- | --- |
| `gdal.version` | — | registered GDAL release of the running process |
| `gdal.drivers` / `gdal.drivers.required` | error | the required-driver set is injected per deployment (default: GTiff, GPKG, GeoJSON, ESRI Shapefile, MEM, VRT); every missing driver is named |
| `gdal.data_dir` | error if set-but-missing | unset is `info` (GDAL ≥3 auto-locates) |
| `proj.db` | error | scans PROJ_DATA/PROJ_LIB/GDAL-sibling/system candidates; the report names **every probed path** |
| `proj.crs.resolve` | error | operational EPSG:4326 import + WKT export — fails when proj.db is missing/corrupt, independent of file checks |
| `runtime.data.dir` | warning | mirrors `resolveRuntimeDataPath` (SICNU_DATA_DIR → marker walk from exe dir/cwd) |
| `fs.temp` | error | create/write/read-back/delete probe with cleanup on every failure path |
| `fs.unicode` | error | Chinese-named directory + file roundtrip under temp (中文路径) |
| `offline.state` | info | engaged? source? GDAL network deny present? |
| `qt.version` / `qt.platform.plugins` | ok / error | runtime qVersion; platforms dir discovery naming every probed candidate |
| `ssl.backend` | warning | QLibrary loads of libssl/OpenSSL candidates; absence only degrades TLS-optional features |

## Exit contract

| verdict | exit | meaning |
| --- | --- | --- |
| healthy | 0 | only ok/info findings |
| degraded | 2 (`ValidationFailure`) | ≥1 warning (e.g. no SSL runtime) |
| broken | 2 (`ValidationFailure`) | ≥1 error (e.g. proj.db missing) |
| usage | 6 (`InvalidInput`) | malformed arguments (standard CLI routing) |

`data.verdict` in the JSON envelope distinguishes degraded vs broken;
`data.checks[]` carries per-finding `{check, severity, message, detail?,
diagnostic?}` inside the `exp.env.report.v1` envelope.

## Diagnostic ids

Every failing check maps to a curated id in `data/help/diagnostics.json`
(family `env`) — the id travels verbatim in the finding's `diagnostic` field
and in the text report's trailing parenthesis. The ids are part of the
contract; adding a check means adding its id to the catalog in the same
change (enforced by `tests/test_env_doctor.cpp`).

## Deployment integration

- **At build time (Windows)**: `scripts\build_offline_bundle.cmd --check-runtime`
  runs the *bundled* CLI's env-doctor after assembly — fail-closed, so a
  broken DLL/plugin/PROJ closure is caught before the bundle ships.
- **On the target machine (Windows)**: `VERIFY.ps1 -Runtime` (VERIFY.cmd keeps
  the integrity-only contract).
- **On the target machine (Linux/macOS)**: run the shipped verifier with
  `VERIFY.sh`, then `bin/sicnu_geo_rs_cli env-doctor`.
- **In the lab bundle**: the CLI's probes use only bundle-local state
  (`PROJ_DATA`/`GDAL_DATA` are set by `RUN.cmd` relative to the bundle root).

## Limitations (honest)

- `ssl.backend` proves loadability of an SSL library, not a working TLS
  session; offline labs never need one.
- The Linux portable bundle relies on host libraries for Qt/GDAL runtime
  (`dependencies.json` in the bundle records shipped vs host-required);
  env-doctor verifies the *runtime-resolved* state of whichever libraries the
  loader actually picked — on a target machine, that is the authority.
- On macOS the geospatial probes behave as on Linux; `platform` reports
  `macos`. macOS is not exercised by the F19 track (no host available).
