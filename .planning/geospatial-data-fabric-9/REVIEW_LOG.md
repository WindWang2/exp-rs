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

## Round 1 — adversarial review (two read-only subagents) — completed

### Reviewer A (architecture / correctness / concurrency / scientific validity / security)

| Finding | Sev | Disposition |
|---|---|---|
| STAC 6-value (3D) bbox mis-indexed into the record's spatial extent (maxX=minZ, maxY=east) | **P1** | FIXED: horizontal extent read from indices 0/1/3/4; regression test added (old indexing provably yields -50/11) |
| Concurrent invalidation between fetch and write-through persisted old-content bytes under the REFRESHED identity basis — checksum-valid, restart-surviving cache poison | **P1** | FIXED: insertBytes and putRunToDisk both refuse when the entry generation moved mid-fetch (capture under fetchMutex, checked under the store lock) |
| Over-long 206 body cached beyond the echoed window (gate asymmetry vs 8.0's implicit rejection) | P2 | FIXED: body sliced to the echoed window; deterministic LongRange fault test (garbage-polluted next block reads fresh) |
| Disk store read `state.directory` outside its mutex (torn read vs configure()) | P2 | FIXED: directory copied under the lock in readBlock |
| putBlock held the global mutex across file I/O + a full eviction walk per put | P2 | FIXED: name allocation under lock, I/O outside, re-lock for stats + eviction amortized via the put-accounted estimate |
| Admission gate can starve the over-cap fetch under sustained small-fetch barging (the "no starvation" claim was too strong) | P3 | header caveat documented (best-effort head-of-line) |
| VectorExtent::exact overclaims provenance (driver-cached/estimated extents indistinguishable) | P3 | header semantics documented as "driver-reported" |
| fieldStatistics caller-owned WHERE clause not declared in the public API | P3 | SECURITY NOTE added to the header |
| Subdataset basis domain separator hashed with a wrong length constant | P3 | FIXED: length-arg dropped |
| cogOptimized heuristic vs declared contract | P3 | header documents the COG-or-tiled-GTiff heuristic |
| resolveLocalReference lexical containment does not re-canonicalize symlinks | P3 | boundary documented (trusted local trees) |

### Reviewer B (tests / performance / portability / resource bounds / docs-vs-code)

| Finding | Sev | Disposition |
|---|---|---|
| Truncation-gate test self-satisfying (fixture's short body with full Content-Length trips a transport error BEFORE the gate; assertions could not see a gate removal) | **P1** | FIXED: fixture now sends an honest short Content-Length with a lying full-window Content-Range (gate branch genuinely reached); fallback_reads delta asserted |
| Admission test could not detect gate removal (workload below cap even ungated; peak delta masked by the monotonic high-water from earlier tests) | **P1** | FIXED: moved to first position in the binary, 4 readers × 256 KiB fetch runs (ungated sum ~1 MiB), ABSOLUTE gauge asserted ≤ the 256 KiB cap |
| TEST_MATRIX/PERFORMANCE rows claiming tests that did not exist | **P1** | FIXED: rows rewritten to match reality; real tests added where cheap (dedup_hits > 0, real amplification > 0, hardCap count-off ResourceExhausted) |
| Catalog sort keyed on the normalized STRING: fractional seconds misorder ("…00.250Z" before "…00Z") | P2 | FIXED: comparator sorts by parsed epochNanos; unparseable keys keep stable input order |
| STAC local-href containment breaks on Windows (separator-sensitive prefix compare) | P2 | FIXED: lexically_relative + leading-".." component check |
| asset_identity fopen(fs::path) does not compile on Windows (wchar_t* c_str) | P2 | FIXED: .string() conversion like the sibling code |
| #850 residue check vacuous (scratchDir() re-wiped before counting) | P2 | FIXED: snapshot before, count without wiping |
| STAC query cache held its mutex across the network fetch | P2 | FIXED: lookup under lock, fetch outside, store under lock |
| range_cache_disk.h "short mutex" contract false (I/O + walk under it) | P3 | FIXED (see P2 above) + docs |
| evictUnderCap error path wraps total (file_size returns -1 on error) → mass eviction + garbage bytesStored | P3 | FIXED: ec checked before accumulating |
| Abandoned .tmp files never evicted/cleared | P3 | FIXED: tmp files are eviction candidates and clear() drops them |
| #874 test title promised a NaN case with no NaN in the body | P3 | FIXED: Float32 NaN-nodata case added |
| FlatGeobuf certification never compared geometry | P3 | FIXED: point WKT coordinates compared in the sorted loop |
| STAC cache bytes = compact-serialization accounting presented as a memory bound | P3 | PERFORMANCE.md states the convention |

## Verified sound by both reviewers (not fixed because not broken)

#850 move semantics + teardown discipline; #874 float-space comparison exactness
(readWindow widens without scale/offset); Float32 overflow gate; atomic_fs
main-restore dedup is a strict improvement (backup preserved on failed
restore); fetchRange gate logic; admission CV usage (no lost wakeups, no
lock-order hazards); block index math; RFC 3986 merge edge cases; STAC cache
key handling (in-process-only claim holds); cfUnitEpoch math; io:inspect
schema truth; ownership boundaries (all new modules inside src/geospatial);
main-phase rollback, #850, #874 tests are load-bearing (fail on old code).
