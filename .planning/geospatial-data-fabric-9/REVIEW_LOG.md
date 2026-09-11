# REVIEW LOG — geospatial-data-fabric-9

## Round 0 — self-review during implementation (continuous)

| Finding | Severity | Disposition |
|---|---|---|
| VectorWriter move fix initially only covered the ctor, not move-assign | P1 | both fixed + separate regression tests |
| Explicit rollback in cancel() surfaced GPKG trigger bookkeeping CPL errors after rollback | P2 | whole cancel teardown quieted (staged file is discarded either way; CPL complaints are noise) |
| ShortRange fault initially fired on identity-probe windows too → open failed | P2 (test fixture) | fault scoped to rangeStart ≥ 1024 + transient (fired-once) so the /vsicurl/ fallback path stays servable, mirroring ResetRanged |
| Truncation gate first draft duplicated the EOF-clamp logic in two branches | P3 | collapsed to a single echoed-window completeness rule |
| CF unit parser first draft required a leading numeric count ("12 hours") — real CF units are "hours since …" | P1 (caught by M5 test) | fixed; test proves 101h/202.5h resolve to exact UTC instants |
| resolveReference produced "host//path" for root-relative references (pre-existing 8.0 defect, found by M4 test) | P1 | RFC 3986 §5.3 merge fixed for both link resolution and asset hrefs |
| Local asset href containment: escaping the item directory is REFUSED by design (documented; absolute hrefs are the escape hatch) | accepted risk | recorded in TEST_MATRIX + code contract |
| STAC query cache keys carry the raw URL in-process | accepted risk | documented: keys are never logged/persisted; every display surface stays redacted |
| io:inspect schema drift (#880 runtime half) | P1 | schema now declares includeStatistics; mechanical comparison added to the review tooling (script in this log's history) |
| FlatGeobuf certification flip | evidence-gated | round-trip test added FIRST (filtered streaming reads), driver-truth gated with honest SKIP; matches the GeoParquet certification pattern |

## Round 1 — adversarial review (subagents) — pending

Will be appended after the implementation milestones are complete: two
read-only reviewers (architecture/correctness/concurrency/scientific
validity/security; tests/performance/portability/resource bounds/docs).
