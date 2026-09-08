# I/O Performance Notes (Foundation 5.0)

> Benchmarks live in `tests/benchmark_io.cpp` (env-gated) with results in
> `benchmarks/*.json`. Numbers are recorded per machine/run there; this page
> documents *what is measured* and the invariants that must hold.

## What is benchmarked

| Benchmark | What it proves |
|---|---|
| open / inspect latency | one read-only open answers canonical metadata (no pixel scans) |
| window read (64²/256²) | bounded buffer per window; cost scales with the window, not the raster |
| multiband window read | band-sequential buffers stay O(window × bands) |
| sequential scan | streamed tile walk over the whole raster stays memory-flat |
| write + finalize | staged → fsync → validate → publish cost is bounded, atomic |
| reproject / warp | tolerance-grade conversions through GDAL utilities |
| COG create | preset-driven COG production + validation |
| vector scan (100k features) | batched streaming stays memory-flat |
| simulated remote range | byte accounting for `/vsicurl/`-style access |

## Invariants enforced by tests (not just measured)

* **No silent full read**: `readFull` refuses beyond its declared byte budget
  (`test_io_raster_contract`); the remote range harness proves a `/vsicurl/`
  window read serves far less than half the payload byte-for-byte
  (`test_io_remote_range`).
* **Memory stays O(window/chunk)**: tile walks never materialize more than one
  tile × bands; vector streams move in bounded batches
  (`test_io_vector_contract`).
* **Multidim slices are budgeted**: `maxCells` (default 64 Mi cells) bounds
  every slice (`test_io_multidim`).
* **Remote surprise downloads are surfaced**: PAM aux probes and chunk
  granularity dominate byte accounting on remote origins — the remote tests
  pin `GDAL_PAM_ENABLED=NO` and a small `GDAL_HTTP_CHUNK_SIZE` so the gate
  measures the asset, not GDAL's sidecar chatter.

## Production defaults worth knowing

* `configureRemoteCachingDefaults()` (data layer) bounds the `/vsicurl/`
  block cache, timeouts and retries once per process.
* `RemoteDatasetPool` caps open handles per remote URL (default 2, env
  `SICNU_REMOTE_POOL_HANDLES`), serializing access per handle.
* Overview policy: algorithms default to `Exact`; preview/UI may opt into
  `Nearest`. A silently sampled overview is a wrong-answer factory.
