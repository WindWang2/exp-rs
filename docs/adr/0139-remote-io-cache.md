# ADR 0139: Remote I/O, Range Access & Cache Taxonomy (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: remote rasters enter through GDAL's VSI layer (`/vsicurl/` and
  friends) with process defaults from `configureRemoteCachingDefaults()` and a
  bounded `RemoteDatasetPool` in the data layer. The foundation layer needs the
  same guarantees (bounded fetch, timeouts, offline taxonomy) without a second
  network stack, and remote behavior must be *provable* in tests — a COG
  window read over HTTP must not secretly download the file.
- Decision:
  1. **No new network stack**: remote I/O is GDAL VSI. The foundation layer
     configures the same bounded defaults (timeouts, retry counts, block
     cache) via the existing single-source-of-truth helpers; it never streams
     URLs itself.
  2. **Remote probe is bounded**: `remote/probe_remote.*` issues HEAD (or a
     small ranged GET) with explicit timeout and max-bytes bounds to answer
     *reachable? / size / accepts ranges?* — never to fetch content. Results
     are typed (`RemoteReachability`), and failures map to the error taxonomy
     (`NetworkError`, `Timeout`, `Forbidden`) instead of a generic open
     failure.
  3. **Cache taxonomy (documented ownership, no new cache)**:
     *metadata cache* — GDAL/PAM, process-local; *block cache* — GDAL
     `/vsicurl/` block cache, bounded by config; *handle cache* —
     `RemoteDatasetPool` (bounded per URL, serialized); *download/staging* —
     only when a caller explicitly stages a remote asset, into the workspace
     temp area, size-capped, and registered through DataManager as a
     `TemporaryFile` asset. The foundation layer does not implement its own
     eviction; it reuses the data-layer pieces.
  4. **Failure honesty**: a deferred remote open stays `Missing` + warning in
     catalog registration (existing contract); a remote *read* that times out
     surfaces `Timeout`, not a short read. Retry policy is GDAL's configured
     one; the layer adds no infinite loops.
  5. **Proof harness**: `tests/support/http_range_server.*` — a local
     range-request HTTP server fixture (loopback only, test-only) exercises
     range reads, multiple tile fetches, timeouts, non-range servers, server
     errors and truncation. Core remote tests never depend on public network.
     Credentials are never logged; URL display goes through the ADR 0135
     redaction.
- Consequences: "COG over HTTP with one window read" is testable end-to-end
  with byte-level download accounting (the fixture counts served bytes);
  offline/timeout behavior is typed; no duplicate cache implementation exists
  to diverge from the data layer.
