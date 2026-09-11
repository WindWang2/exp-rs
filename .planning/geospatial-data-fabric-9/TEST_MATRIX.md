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
| M2 | short/truncated ranged response → error + not cached | new fault test |
| M2 | global in-flight byte bound honored under concurrency | new capability |
| M2 | amplification/dedup metrics correctness vs fault server | new capability |
| M3 | disk store: write→read, corruption→miss, cap→LRU eviction, torn block→miss, process-restart reuse | new capability |
| M4 | relative href resolution (./, ../, absolute) | new capability |
| M4 | query cache: hit/miss/eviction/TTL; redaction preserved | new capability |
| M5 | cube JSON round-trip symmetry (numeric + string datetime axes) | new capability |
| M6 | FlatGeobuf round-trip (runtime-gated, honest skip) | new capability |
| M6 | extent()/stats pushdown + bounded fallback; batch write | new capability |
| M6 | io:* schema-vs-runtime drift guard (mechanical) | drift would fail |
| M7 | catalog query: predicates/sort/pagination bounds/spatio-temporal; 100k records bounded memory | new capability |
| M8 | hints: COG/local/remote/multidim shapes; JSON stability | new capability |
| M9 | doctor capability matrix JSON vs real GDAL | new capability |
| M9 | concurrent window reads FD bound | new capability |

Execution rules: tests `-j1`/small parallel; "not run / driver missing" is
reported as SKIP with reason, never PASS.
