# OWNERSHIP — Track H

## Allowed to modify (core)

- `src/geospatial/remote/range_cache.{h,cpp}` — admission lifetime, cancel-aware backoff,
  Stat TTL/policy path, telemetry fields.
- `src/geospatial/remote/range_cache_disk.{h,cpp}` — only if WP4 needs a counter (avoid).
- `src/geospatial/remote/remote_source_validator.{h,cpp}` — only if Stat revalidation
  needs an accessor (prefer existing API).
- `src/geospatial/fabric/mirror.{h,cpp}` — orphan-scan name handling, refusal counters,
  report fields (additive).
- `src/geospatial/util/atomic_fs.{h,cpp}` — only a needed interface addition
  (e.g. safe filename→UTF-8 helper or wide remove) — keep surface minimal.
- `tests/test_io_range_cache.cpp`, `tests/test_io_mirror_maintenance.cpp` — extend in place.
- `tests/test_io_microbench.cpp` — only if a new gate is needed (avoid; keep ms-free).
- `tests/support/http_range_server.{h,cpp}` — additive fault arms only (e.g. a
  Retry-After-style or counted arm) if a test needs it.
- `tests/CMakeLists.txt` — append-only block(s).
- `.gitignore` (planning whitelist, appended at tail), `.goal-loop-ledger.md` (append),
  `.planning/ds41-geospatial-maintenance-13/*.md`.

## Forbidden / do-not-touch

- `src/data/providers/**`, DataManager, TaskCenter, model runtime, app/CLI/agent surfaces —
  explicitly out of scope per the track card.
- `src/geospatial/remote/http_fetch.*` — the 12.0 notes mark it as another track's file
  (#1110's checked-ofstream pattern); WP1 must not need it (retry lives in range_cache).
- `src/geospatial/stac/**` — in ownership per the card but no change expected (STAC
  concurrency shipped in 12.0); touch only if a review finding forces it.
- Any parallel-domain file PR #1135 touches (operators, capability metadata).
- No new CLI/MCP/agent surface (same discipline as 12.0 D-1201).

## Shared hotspots

- `tests/CMakeLists.txt`: append at tail only.
- `.goal-loop-ledger.md`: append at tail only.
- `range_cache.cpp` admission/TTL region: this track owns it; no competing PR known.

## Resource discipline

- `CMAKE_BUILD_PARALLEL_LEVEL`/`CTEST_PARALLEL_LEVEL` ≤ 2, default 1; targeted targets only
  (`test_io_range_cache`, `test_io_mirror_maintenance`, `test_io_microbench`,
  `test_io_remote_validator`, plus `Sicnu::Geospatial` dep chain — never full QGIS/OTB).
- `QT_QPA_PLATFORM=offscreen`; loopback fixtures only; no public network.
- One build coordinator at a time (this main agent); subagents read-only.
