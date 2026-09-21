# BASELINE — Track H: Remote Geospatial Fabric Maintenance 13.0

## Trunk

- `origin/master` = `79adfe78a16b9419eef180cf9e6e5739658621a2` (2026-09-20 21:39:35 +0800)
  "Merge pull request #1134 from WindWang2/agent/flash-plugin-sdk-12"
- Worktree: `/home/kevin/project/exp-rs-worktrees/geo-maintenance-13`
- Branch: `agent/ds41-geospatial-maintenance-13` (created from fetched origin/master via
  `scripts/dev/new_worktree.py`; merge-base verified = baseline SHA)
- Fetched at session start with `git fetch origin --prune`.

## Live remote state at baseline (2026-09-21)

- Open PRs: **1** — #1135 `agent/flash-temporal-phenology-12` (temporal phenology analytics;
  file list touches NO `src/geospatial/{remote,fabric,stac}` paths — only operators,
  capability metadata, planning docs, `.goal-loop-ledger.md`, `tests/CMakeLists.txt` append).
- Open issues: **0** (gh issue list returned empty).
- Recently merged PRs (last ~40 commits): the 12.0 wave #1116–#1134, incl. **#1116
  `agent/flash-geospatial-fabric-12`** — the Geospatial Fabric 12.0 this track extends.
- Remote branches judged residue: `agent/flash-geo-fabric-integrity` (PR #1062 CLOSED,
  superseded by merged #1100 fail-closed wave), `agent/ds41-http-fetch-strict` (PR #1057
  CLOSED, same), `fix/review-issues-1033-1056` (PR #1060 CLOSED). Evidence only; nothing
  cherry-picked.

## The three declared 12.0 limitations this track addresses

From PR #1116 body ("Known limitations (P2-accepted, documented in code)"):

1. **Admission guard spans backoff sleeps** — `range_cache.cpp` `serveOrFetch`:
   `InFlightAdmission` RAII wraps the WHOLE `fetchRange` call incl. `sleep_for` backoff
   (comment at ~line 1280). Bounded by fetchAttempts×cap but holds the origin slot while
   sleeping.
2. **Stat path does not enforce TTL** — `RangeCacheFilesystemHandler::Stat` never calls
   `entryExpired`, never applies `stalePolicy` revalidation: a stat answer can stay
   conservative-stale forever while the read path re-proves.
3. **Non-ASCII orphan names counted but not deleted on Windows** — `mirror.cpp`
   `pruneMirror` orphan scan skips non-ASCII names via `filename().string()` (ACP-mangled
   on Windows) + explicit ASCII gate.

## Relevant source map (read at baseline)

- `src/geospatial/remote/range_cache.{h,cpp}` — CacheStore (admission gate
  `admitFetch`/`completeFetch`/`inFlightBytes`, `mCancelledFetches` veto set, TTL via
  `provenAtMs`/`entryExpired`/`touchEntryProven`), `fetchRangeOnce`/`fetchRange`
  (retry+backoff), `RangeCacheHandle::serveOrFetch`, handler `Open`/`Stat`.
- `src/geospatial/remote/remote_source_validator.{h,cpp}` — `RevalidationOutcome`
  {Unchanged, Changed, Inconclusive}; conditional GET semantics.
- `src/geospatial/fabric/mirror.{h,cpp}` — `verifyMirror` (orphan scan ~line 584),
  `repairCleanup`, `pruneMirror` (orphan scan ~line 1325, ASCII gate ~1340),
  `isSafeMirrorChunkFileName`.
- `src/geospatial/util/atomic_fs.{h,cpp}` — `removeFileQuiet` (UTF-8 → `fs::u8path`),
  `fileExists`, wide-path discipline on Windows.
- `tests/support/http_range_server.{h,cpp}` — `ServerBehavior` fault arms +
  `setFaultScript` (deterministic per-request script), `setConcurrency`, byte/request
  accounting, `requestLog`.
- `tests/test_io_range_cache.cpp` — 12.0 suites: admission peak, retry/backoff, cancel
  veto + mid-flight discard, TTL, churn budgets.
- `tests/test_io_mirror_maintenance.cpp` — verify/repair/prune suites.
- `tests/test_io_microbench.cpp` — bytes/requests/budget gates (never ms).

## Conflict hotspots (shared files)

- `tests/CMakeLists.txt` — append-only convention; open PR #1135 also appends.
- `.goal-loop-ledger.md` — append-only.
- `tests/support/http_range_server.*` — additive-only extension allowed.
- `src/geospatial/remote/range_cache.*`, `src/geospatial/fabric/mirror.*`,
  `src/geospatial/util/atomic_fs.*` — this track's core; no other open PR touches them.
