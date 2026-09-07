# SCALE BASELINE — governance store at 100k assets (Reliability 4.0)

Environment: 16-core linux host, Release build (Ninja), sequential CTest run,
SQLite WAL, `SICNU_WS3_STRESS=1`. Recorded 2026-09-06 from
`tests/test_workspace_stress` INFO output (`assetLevel: 100000`), on the
Reliability-4.0 branch after the store-hardening changes (issue #758 1–4).

| Operation | 100k baseline (ms) | 3.0 baseline (CHANGELOG, ms) |
|---|---|---|
| Ingest 100k assets (batched mirror) | 1125 | ~1000 |
| Paged query (200/page) | 76 | ~84 |
| Facet filter | 22 | ~21 |
| Facet aggregation | 5 | — |
| 1000 indexed point lookups | 39 | ~42 |
| 10k bulk tag | 15 | ~14 |
| Depth-64 lineage query | <1 | <1 |

Interpretation: the hardening pass (checked COMMIT, alias owner checks,
prepare-once statement cache, real COUNT(*) summary) holds the 3.0 scale
contract — every routine operation stays well inside its assertion budget
(paged < 2000 ms, facet < 1000 ms, lookup < 1500 ms, lineage < 1500 ms) with
no regression outside run-to-run noise. `totalObjectBytes()` full-tree walks
no longer run on the cache store path (incremental accounting, under-budget
fast path).
