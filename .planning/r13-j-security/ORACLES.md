# Verifiable Oracles: R13-J Security and Concurrency Hardening

## Oracles Matrix

| ID | Module / File | Failure Mode | Test Oracle (RED -> GREEN) | Success Criteria |
|---|---|---|---|---|
| `ORA-SEC-01` | `offline_gate.cpp` | Hostname resolving to 127.0.0.1 or 169.254.169.254 allowed as remote host | Pass `http://localhost/` or local DNS name to `isPrivateHost()` | Returns `true` (blocked); passes in `test_io_remote_validator` |
| `ORA-SEC-02` | `range_cache.cpp` | Redirect to private IP in range cache allowed | Range cache redirecting to `127.0.0.1` | Throws `GeoError` or triggers `redirectGuard` fail-closed |
| `ORA-SEC-03` | `resource_uri.cpp` | `%2e%2e` directory escape allowed in `resolveAgainst()` | Call `resolveAgainst("/base/dir", "%2e%2e/secret.txt")` | Returns `ResourceKind::Invalid` with escape parse reason |
| `ORA-CON-01` | `temporal_cube.cpp` | Concurrent `readChunk()` on `mLru`/`mIndex` causes heap corruption | Multi-threaded reading of chunk cache under high concurrency | Clean memory execution without TSAN/ASAN race or crash |
| `ORA-CON-02` | `python_worker_process_pool.cpp` & `python_plugin_adapter.cpp` | Drain race & raw pointer dangling UAF on worker restart | Terminate worker while adapter initiates call | Thread-safe failure result without heap-use-after-free |
| `ORA-RNT-01` | `gpu_plane.cpp` | Released session pool starvation under capacity limit | Allocate max sessions, release them, then allocate again | Successfully evicts released sessions and fulfills allocation |
| `ORA-CON-03` | `science_context/broker.cpp` | Multi-threaded recipe refresh and cache invalidation data race | Concurrent read/write on `ScienceContextBroker` | Zero data races, proper synchronization |
