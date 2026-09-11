# PERFORMANCE — 8.0 measurements (local, single-run evidence)

Environment: Linux, GDAL 3.13.3, -j2 bounded builds, tests run standalone.

## Range cache byte accounting (through `/vsirangecache/`)

Measured by `test_io_range_cache` against the loopback fixture (loopback only;
no network) — numbers from the passing runs:

| Workload | Origin bytes fetched | Object size | Note |
|---|---|---|---|
| 256×256 Float32 window ×1 reader | 5 coalesced ranged GETs, ~266 KB | ~4 MB raw TIFF | metadata + tiles; window ≈ 256 KB of pixels |
| same window ×4 concurrent readers | < 4×window block bytes (bound asserted; observed ≈ single-reader + per-open metadata) | ~4 MB | fetch-mutex dedup: no 4× multiplication |
| adjacent+overlapping re-read | +0 origin bytes on overlap | ~4 MB | fully-cached overlap pays once |
| mid-range connection reset | fallback serves correct bytes; `fallback_reads > 0` | ~4 MB | reset is transient; origin recovers |
| COG overview tile + 128×128 window | ~1.7 MB of a ~4.2 MB deflate-hostile COG (~40%) | 4.2 MB | IFD walks + full-tile inflation dominate; asserted fetched×2 < payload |

## Identity token cost

`remoteIdentityToken` performs exactly one bounded ranged probe
(`bytes=0-1023`, hard-capped ≤1 MiB) + a SHA-256 over a <1 KB basis —
never a content download. Verified by the byte-accounting fixture server in
`test_io_identity` (probe answers only; no payload-sized transfers).

## STAC series ordering

`buildTemporalSeriesDetailed` is O(n log n) on precomputed instants (one
bounded string parse per item, ≤ nanosecond ints). No I/O. Verified by
deterministic-order tests over mixed-offset sets.

## Doctor additions

`cacheability` reuses the bounded probe identity (no extra network round
trip beyond the existing optional `includeRemoteProbe` HEAD/ranged GET).
`multidim_axes` and `resampling_risk` are pure metadata checks (no I/O
beyond the single inspection open).

## Bounds & hygiene

- Fixture concurrency is hard-capped at 8 handler threads (test-only).
- `data cache check --bytes` is clamped to ≤ 64 MiB per run.
- All new tests use ≤ 4.3 MB fixtures; scratch dirs are per-suite and removed.
