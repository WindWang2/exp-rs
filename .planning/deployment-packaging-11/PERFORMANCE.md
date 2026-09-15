# PERFORMANCE — resource model and bounds

## Build
- Hard caps per envelope: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `-j2` (drop to `-j1` if RSS > 70%
  of 64 GB ≈ 45 GB or load explodes — monitoring script logs every 60 s).
- Strategy D1: copied build-dev; expected rebuild ≈ 192 (master drift, CLI closure) + ~10
  track TUs + test binaries. Log: EVIDENCE Phase 6.

## Tests
- `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`; targeted `-R` first.
- New tests are all bounded-logical-scale (fixture bundles < 1 MB; env probes touch only
  temp dirs); no wall-clock assertions anywhere (envelope rule).

## Bundle size
- Ceiling stays declared in the manifest (`size_ceiling_mb`, default 250); smoke asserts it.
- `dependencies.json` adds one JSON file; conformance fixtures < 100 KB total.

## Runtime diagnostics cost
- env-doctor is O(drivers + probes): one GDAL driver enumeration, one EPSG:4326 OSR
  roundtrip, one temp-file create/delete per writability probe, QLibrary loads for SSL
  candidates. No dataset I/O, no network, bounded time (no sleeps).
