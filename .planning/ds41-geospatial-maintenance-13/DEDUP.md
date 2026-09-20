# DEDUP — Track H vs live master/PRs (checked 2026-09-21)

## Already implemented (do NOT redo)

From merged #1116 (flash-geospatial-fabric-12) and earlier waves:

- Ranged-fetch retry with bounded exponential backoff (`fetchAttempts` clamp [1,8],
  permanent-4xx-except-429 no-retry, `retried_fetches` counter) — **retry exists; the
  admission LIFETIME during backoff is the gap** (WP1).
- Global in-flight byte admission (`maxConcurrentFetchBytes`, `admitFetch`/`completeFetch`,
  `max_in_flight_fetch_bytes` peak gauge) — exists; WP1 changes WHERE it is held, not
  whether it exists.
- `entryTtlSeconds` TTL on the Open path (`entryExpired` + `touchEntryProven` + 304
  refresh of the basis) — exists for Open; **Stat ignores it** (WP2).
- `cancelFetches`/`resumeFetches` store-level veto surviving entry churn — exists; WP1
  adds cancel-during-backoff wake-up.
- `verifyMirror`/`repairMirror`/`pruneMirror`, fault script, microbench — exist; WP3/WP4
  extend the orphan scan + telemetry, not the harnesses.

## True gaps (this track's work)

| WP | Gap | Evidence |
|---|---|---|
| 1 | `InFlightAdmission` RAII in `serveOrFetch` spans `fetchRange`'s `sleep_for` backoff: the in-flight byte slot stays held while the thread sleeps (bounded only by attempts×cap). No cancel wake during the sleep (a veto waits out the full backoff). | `range_cache.cpp` ~L1280 comment admits it; `fetchRange` ~L948 `sleep_for` unconditional |
| 2 | `Stat()` never calls `entryExpired` and never applies `stalePolicy`: a cached entry's size is reported forever (even past TTL); revalidation/refresh never happens on the stat path. | `range_cache.cpp` `Stat` ~L1532: no `entryExpired`, no `revalidate`, no `touchEntryProven` |
| 3 | Orphan scans use `it->path().filename().string()` (ACP-mangled on Windows) + an all-ASCII gate in `pruneMirror`: non-ASCII orphans survive forever; a non-ASCII name can't even be compared to `referenced` correctly. No refusal accounting for locked/unremovable files. | `mirror.cpp` ~L596 (verify), ~L1333-1348 (prune ASCII gate) |
| 4 | No telemetry for: retry slot occupancy window, TTL expirations/refreshes, cancel discards, admission waits; orphan removals count only successes (silent failures). | `RangeCacheTelemetry` + `MirrorPruneReport` field lists |

## Open-PR overlap verdict

- #1135 (temporal-phenology-12): disjoint file set. Only shared text: `tests/CMakeLists.txt`
  tail + `.goal-loop-ledger.md` tail (append-only both sides — merge is mechanical).
- No open PR touches `src/geospatial/{remote,fabric,stac}/**`, `atomic_fs`, or the io test
  fixtures this track edits. Nothing to yield or pivot on.

## Residue branches

`agent/flash-geo-fabric-integrity`, `agent/ds41-http-fetch-strict`, `fix/review-issues-1033-1056`:
all CLOSED PRs superseded by merged #1100; diff inspection shows no unmerged increment
relevant to WP1–WP4 (their http_fetch/mirror fail-closed work is already in master).
