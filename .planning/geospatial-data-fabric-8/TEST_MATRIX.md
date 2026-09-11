# TEST MATRIX — 8.0 evidence (executed locally, Linux / GDAL 3.13.3)

All suites run with Catch2 directly (bounded timeouts), never concurrently
with other builds. "Executed and passed" always means this exact environment.

| Suite | Cases | Result | New in 8.0 | What it proves |
|---|---|---|---|---|
| test_io_identity | 5 | ✔ executed & passed (47 assertions) | all | SHA-256 FIPS 180-4 vectors; ISO-8601 mixed-offset/naive/refusal parsing; fail-closed identity tokens (weak/none → ""), stability, change-on-content-change, credential-shape stripping (re-sign ⇒ same token; no secret/URL fragments in token) |
| test_io_stac_client | 14 | ✔ executed & passed (97 assertions) | +3 | mixed-offset series ordering by instant; deterministic duplicate-acquisition handling + reporting; assumed-UTC flagging; toJson stays verbatim |
| test_io_range_cache | 13 | ✔ executed & passed (83 assertions) | +5 | concurrent readers share origin fetches (byte accounting); adjacent/overlapping windows byte-correct + pay-once; mid-range connection reset degrades to fallback without wrong bytes; changed-content-under-same-URL invalidates across generations (no blend); COG overview+window reads through cache with byte accounting |
| test_io_vector_interop | 7 | ✔ executed & passed (172 assertions) | +2 | GeoParquet certified round-trip: field types, null≠empty≠0, empty string, projected CRS (EPSG:32648), polygons, null geometry survives; registry answers Certified (driver-gated) |
| test_io_multidim | 9 | ✔ executed & passed (178 assertions) | +2 | string datetime axes captured (netCDF-4); exact-label + offset-normalized instant selection; typed miss for absent labels; EO workflow: instant selection → bounded window (dimension-order fidelity) → maxCells refusal |
| test_io_doctor | 8 | ✔ executed & passed (59 assertions) | +2 | cacheability verdict follows validator strength (ok vs warning); reproducibility warning + advice for weak identities; categorical band resampling risk; auto_fixable stays false |
| test_io_remote_validator | 14 | ✔ executed & passed (83 assertions) | regression-green | pre-existing remote identity contracts |
| test_io_stac / test_io_remote_range / test_io_uri | 4/5/— | ✔ executed & passed | regression-green | no regressions in adjacent suites |
| test_temporal_workspace | 20 | ✔ executed & passed (264 assertions) | +1 | resolver bridge: unregistered remote input fingerprints through token; "" stays uncacheable; scene loop takes the bridge |
| test_cli_commands_json | 6 | ✔ executed & passed (33 assertions) | +3 | `data identity` end-to-end (fresh + offline states, token_provable, signed-URL redaction); `data cache` byte accounting through the cache; pre-existing `--json` swallow bug fixed (runHelpProjections) |

All suites re-verified after the second remediation pass (fixture
join-under-lock deadlock fix, SIGPIPE hardening): range cache 3/3 consecutive
clean runs; every fixture-dependent suite green in the final sweep.

## Post-review additions (second sweep)

- regression tests for the strict-weak-ordering fix (unparseable datetime
  mixed into the offset set) and the range-only `datetimeNormalized` flag
  — `test_io_stac_client` 16 cases / 110 assertions;
- parser bounds (year 2263/1677/9999/0001 refused; 2262 accepted;
  negative-epoch round-trip; leap-second clamp) — `test_io_identity`;
- `resetFired()` proves the transient reset fired in the reset test;
- concurrent-reader bound tightened to < 2× distinct block bytes.

## Environment

- Linux 6.18.49-2-lts x64, GCC 16 (system c++), Ninja, CMake 4.4.3
- GDAL 3.13.3 (netCDF rw, HDF5 ro, Zarr rw, Parquet rw via ogr_Parquet plugin)
- Resource bounds: -j2 builds, single-threaded test runs, loopback fixtures
  with bounded receive windows; all scratch under /tmp with per-suite cleanup

## Honest limitations

- The `updateEntrySize` unknown-size guard (range cache Open path) is fixed
  and code-reviewed but not fixture-reachable in tests: the loopback fixture
  always declares Content-Length/Content-Range, so an Unchanged revalidation
  with an unknown size cannot be fabricated without a misbehaving-origin
  mode; noted for the adversarial review.
- `test_io_vector_interop`'s 7.0 GeoParquet block previously used
  `GDALCreateCopy`, which fails (returns null, writes nothing) on GDAL 3.13.3
  — a pre-existing latent failure in this environment, now authored through
  the certified write path instead.
