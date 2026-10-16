# Ownership Ledger: R13-J Security and Concurrency Hardening

## Module & File Boundaries
This track focuses on critical P0/P1 security vulnerabilities and multi-threading concurrency defects identified during the whole-repository audit:

### Geospatial Remote & URI Security (DEF-GEO-01, DEF-GEO-02, DEF-GEO-03)
- `src/geospatial/remote/offline_gate.cpp` (SSRF DNS resolution to block private/loopback/cloud-metadata hosts)
- `src/geospatial/remote/offline_gate.h`
- `src/geospatial/remote/range_cache.cpp` (Enforce redirectGuard across GDAL /vsimem/ cache fetches)
- `src/geospatial/util/resource_uri.cpp` (Percent-decode tokens in `resolveAgainst` to block `%2e%2e` directory traversal)
- `tests/test_io_remote_validator.cpp`
- `tests/test_io_remote_range.cpp`
- `tests/test_io_uri.cpp`

### Core Temporal Engine Concurrency (DEF-COR-01)
- `src/core/temporal_cube.h`
- `src/core/temporal_cube.cpp` (Concurrency lock on `mLru` & `mIndex`; shared ownership of cached `Tile` items)
- `tests/test_temporal_core.cpp`

### Isolated Python Worker Lifecycle & Concurrency (DEF-RNT-01, DEF-RNT-02)
- `src/python/isolated/python_worker_process_pool.h`
- `src/python/isolated/python_worker_process_pool.cpp` (Synchronized worker node lookup and safe socket drain)
- `src/python/isolated/python_plugin_adapter.cpp` (Avoid use-after-free by ID lookup via pool)
- `tests/test_python_worker_process_pool.cpp` (or isolated worker tests)

### GPU Model Session Pool Resource Eviction (DEF-RNT-03)
- `src/runtime/gpu/gpu_plane.cpp` (Evict released sessions on VRAM pressure)
- `tests/test_gpu_plane.cpp`

### Science Context Broker Multi-Threading (DEF-AGT-01)
- `src/science_context/broker.h`
- `src/science_context/broker.cpp` (Guarding recipe store, active asset sets, and cache with mutex)
- `tests/test_science_context_broker.cpp`

## Prohibited Cross-Module Edits
- No modifications to Cartography / MapSpec (Track R13-G, already merged).
- No modifications to SQLite durability journal (Track R12-B in PR #1504) except pure geospatial security helpers.
