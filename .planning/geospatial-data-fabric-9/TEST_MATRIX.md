# TEST MATRIX — 9.0 (executed evidence appended per milestone)

Existing suites we extend (all `sicnu_add_io_test`):
`test_io_vector_contract`, `test_io_raster_contract`, `test_io_atomic_failures`,
`test_io_remote_range`, `test_io_range_cache`, `test_io_identity`,
`test_io_stac_client`, `test_io_multidim`, `test_io_doctor`,
`test_io_roundtrip_matrix`, `test_io_cog`, `test_io_operators`.

| Milestone | New tests | Old-code-fails property |
|---|---|---|
| M0 | #850: move mid-stream GPKG writer, finalize, reopen → all features present | old code: 0 features (silent rollback) |
| M0 | #850: move-assign chain A→B→C keeps commit semantics | old code: partial/empty |
| M0 | #850: cancel after move discards staging, target untouched | old code: staging leaked |
| M0 | #874: Float32 sentinel -9999.9 (non-float-exact) masked invalid | old code: valid (255) |
| M0 | #874: Float64 exact sentinel unchanged; NaN nodata unchanged | (guard) |
| M0 | Float32 write overflow → FidelityLoss; ±inf never written | old code: inf written |
| M0 | crash-between-phases: killed group publish leaves previous-good group visible | new discipline proof |
| M0 | dir-fsync: publish survives simulated rename-loss ordering (torn publish fault point) | hardening evidence |
| M1 | local identity: stable across re-open, changes on content/mtime change; "" for unreadable; strength flags; budget honored | new capability |
| M1 | STAC asset identity + multidim subdataset identity composition | new capability |
| M2 | truncated 206 (honest short Content-Length, lying full-window echo) → gate throws, fallback_reads delta proves refusal, reads byte-correct | old code: poison accepted |
| M2 | over-long 206 (window + garbage) sliced to the echoed window — the targeted NEXT block reads fresh and correct | old code: checksum-valid garbage cached |
| M2 | global in-flight byte bound: first-position ABSOLUTE gauge ≤ cap with 4 concurrent readers whose ungated sum is ~1 MiB (gate deletion fails the test) | new capability |
| M2 | dedup_hits > 0 under same-resource racing + byte-bound dedup proof; amplification = fetched/served > 0 after real misses | new capability |
| M3 | disk store: write→read, corruption→miss, cap→LRU eviction, torn block→miss; clearEntries() (memory cold) reuse is the in-process equivalent of restart reuse (content-keyed files are process-independent by construction) | new capability |
| M4 | relative href resolution (./, ../, absolute) | new capability |
| M4 | query cache: request-count-proven hits, different-query misses, entry-cap eviction; TTL implemented (time-based expiry not sleep-tested — deterministic-policy review only); keys in-process only, display redaction covered by the 8.0 credential suite | new capability |
| M5 | cube JSON round-trip symmetry (numeric + string datetime axes) | new capability |
| M6 | FlatGeobuf round-trip (runtime-gated, honest skip) | new capability |
| M6 | extent()/stats pushdown + bounded fallback; batch write | new capability |
| M6 | io:inspect schema truth asserted by the operator suite; mechanical schema-vs-run comparison across all io:* operators ran clean during review | drift would fail |
| M7 | catalog query: predicates/sort (parsed instants — fractional seconds safe)/pagination bounds/spatio-temporal; hardCap typed backstop in count-off mode; 100k records bounded memory | new capability |
| M8 | hints: tiled vs strip raster seek cost + chunk shapes + identity strength, vector spatial-filter capability, unreadable-input honesty (no invented facts); remote/multidim hint paths share the identity/probe plumbing covered by the M1/M2 suites | new capability |
| M9 | doctor capability matrix JSON vs real GDAL | new capability |
| M9 | concurrent window reads FD bound | new capability |

Execution rules: tests `-j1`/small parallel; "not run / driver missing" is
reported as SKIP with reason, never PASS.
