# dependency-doctor.md — configure-time dependency diagnosis

Two doctors exist, for two different phases. They do not overlap:

| phase | tool | audits |
|---|---|---|
| **build/configure** | `cmake/SicnuDepDoctor.cmake` (this file's subject) | toolchain/SDK discovery: missing package, wrong version, wrong discovery mode |
| **runtime (deployed bundle)** | `src/geospatial/doctor/env_doctor.*`, `sicnu_geo_rs_cli env-doctor` (Deployment 11.0) | a running install: GDAL drivers, proj.db, offline gate, bundle layout |

Run the runtime one against an install; read this page when *configure* fails
or needs to be reported.

## What configure now gives you

**1. Actionable failures.** A missing or too-old required dependency stops the
configure with the package name, the minimum, what was searched, and the exact
install command per platform (apt / pacman / dnf / brew / vcpkg):

```
Could NOT find GDAL (required by this project). Minimum version: 3.8.
  install:
    Ubuntu/Debian: apt install libgdal-dev
    macOS: brew install gdal
    Arch: pacman -S gdal
    vcpkg: vcpkg install gdal
```

**2. A paste-able dependency summary** at the end of a successful configure
(versions, discovery mode, resolved paths, vcpkg triplet, OTB and NetCDF
notes). Paste it verbatim into bug reports — it replaces the "what did you
have installed?" round trip:

```
-- === SICNU dependency summary ===
--   Qt6: 6.8.0 (config) .../lib/cmake/Qt6
--   GDAL: 3.12.4 (config) .../share/gdal
--   Protobuf: 33.4.0 (config) .../share/protobuf
--   vcpkg: x64-windows (installed: .../vcpkg_installed)
--   NetCDF: provided through GDAL drivers (runtime audit: sicnu_geo_rs_cli env-doctor)
-- === end dependency summary ===
```

**3. Feature probes** (`cmake/SicnuFeatureProbes.cmake`): optional APIs are
detected by *compiling* against the headers actually found, never by guessing
from version numbers. Results land in
`${CMAKE_BINARY_DIR}/sicnu_feature_probes.h` with every macro defined to 0/1,
so code can `#if SICNU_HAVE_GDAL_QUIET_ERROR_CTOR` without `#ifdef`
preambles. `tests/test_feature_probes` pins the contract and cross-checks the
GDAL quieting idiom and `std::format` at runtime.

## The gate

```sh
scripts/build.sh dep-fixture-test
```

configures two hermetic fixtures (no compiler beyond cmake itself, no Qt, no
network): a missing dependency must fail with install guidance, a stale
version must fail naming the minimum, a satisfied minimum must succeed and
print the summary, and a probe call through the doctor must behave exactly
like the direct `find_package` (the vcpkg toolchain's find_package override is
a macro; the doctor is a macro too, for the same scope reasons).

## Diagnosing by symptom

| symptom | first check |
|---|---|
| `Could NOT find X (required by this project)` | the message's install block — your platform's line is first |
| `SQLite::SQLite3 target should not have been defined at this point` | a find-module ran twice with different scopes; report it — the doctor must stay a macro |
| configure cannot find a vcpkg package | `doctor` shows the vcpkg installed tree; point `--base-cache` at a configured `CMakeCache.txt` |
| CI passes, local configure fails | diff the dependency summary against CI's: mode (config vs module), version, path |
