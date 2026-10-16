# Goal: R13-J Security and Concurrency Hardening

## Target Objectives

### Objective 1: SSRF Defense via Hostname Resolution (DEF-GEO-01)
- **Problem**: `offline::isPrivateHost()` currently checks IPv4/IPv6 address literals, but unparsed hostnames (e.g. `localhost.localdomain`, local domain names pointing to 127.0.0.1 or 169.254.169.254, or custom DNS rebinding) bypass string comparison.
- **Fix**: Resolve hostname via standard POSIX `getaddrinfo` (with `AI_ADDRCONFIG` or generic stream hints) when string format is not recognized. If any resolved socket address falls within loopback, RFC 1918 private subnets, link-local, carrier-grade NAT, or multicast, return `true` (block).

### Objective 2: GDAL Range Cache SSRF Redirect Guard (DEF-GEO-02)
- **Problem**: `range_cache.cpp` fetch implementation does not enforce `redirectGuard` or private host check on redirect hops, allowing SSRF bypass when a remote host 302-redirects to an internal IP.
- **Fix**: Wire `offline::isPrivateHost` and `redirectGuard` callback into `GdalRangeCache` fetch logic.

### Objective 3: ResourceUri Percent-Encoded Directory Traversal (DEF-GEO-03)
- **Problem**: `ResourceUri::resolveAgainst` checks `segment == ".."` on raw path tokens, failing to block `%2e%2e` or `%2E%2E`.
- **Fix**: Apply `percentDecode` on path tokens before evaluating relative escape checks.

### Objective 4: TemporalCube Cache Data Race and UAF (DEF-COR-01)
- **Problem**: `TemporalCube::readChunk()` accesses and mutates `mLru` and `mIndex` without synchronization across worker threads, returning raw pointers to `Tile` whose memory is freed when evicted by another thread.
- **Fix**: Guard `mLru` and `mIndex` with a `std::mutex`, and store `std::shared_ptr<Tile>` so callers retain a valid reference even during eviction.

### Objective 5: Python Worker Pool UAF and Socket Drain Race (DEF-RNT-01 & DEF-RNT-02)
- **Problem**: `PythonWorkerProcessPool::terminateWorker` drains sockets and modifies worker state without holding `m_nodesMutex`. `PythonPluginAdapter` holds a raw `WorkerNode*` across asynchronous invocations, causing heap UAF if the worker crashes or restarts.
- **Fix**: Synchronize `drainBufferedResponses` under `m_nodesMutex`. Change adapter to store worker ID and look up worker safely via `serverForWorker(id)`.

### Objective 6: GPU ModelSessionPool Eviction Under Pressure (DEF-RNT-03)
- **Problem**: In `gpu_plane.cpp`, when session count exceeds `kMaxSessionsPerDevice` or VRAM exceeds budget, released sessions are not pruned, causing unnecessary allocation failure or fallback.
- **Fix**: Implement an eviction loop to free released sessions before rejecting new session creation.

### Objective 7: ScienceContextBroker Multi-Threading Thread Safety (DEF-AGT-01)
- **Problem**: `ScienceContextBroker` methods `refreshRecipes()`, `invalidate*()`, and `synthesize()` access recipe store and asset cache without a mutex, causing data races when accessed concurrently by background planners.
- **Fix**: Add `mutable std::mutex mMutex;` and guard shared state operations with `std::lock_guard`.
