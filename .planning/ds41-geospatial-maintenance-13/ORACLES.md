# ORACLES — Track H executable acceptance

All gates run TWICE consecutively (pre-review and post-review), `-j1`, offscreen,
loopback-only. Suites: `test_io_range_cache`, `test_io_mirror_maintenance`,
`test_io_microbench`, `test_io_remote_validator` (+ any new suite this track adds).

## O1 — backoff does not hold the origin slot

- During a retry's backoff sleep, `in_flight_fetch_bytes` drops to 0 for that fetcher
  while its fetch is unfinished — observable via telemetry or a second fetcher being
  admitted while the first sleeps.
- Total concurrent in-flight bytes never exceed `maxConcurrentFetchBytes` (12.0 oracle
  preserved).
- Retries still bounded by `fetchAttempts`; permanent 4xx (non-429) never retried;
  429/5xx retry.
- `cancelFetches` during a backoff sleep makes the sleeping reader exit promptly
  (bounded by a small wake interval, not the full backoff) and degrade to fallback —
  never publish cancelled bytes.
- Telemetry: attempt/slot-release/cancel counters exported.

## O2 — Stat honors the declared trust horizon + policy

- `entryTtlSeconds` expiry applies identically on the Stat path: a stat past the TTL
  re-proves (fresh ranged identity request) instead of serving the stale size.
- `RevalidateOnOpen` on Stat issues the conditional request; a 304 refreshes the TTL
  basis; a validator change invalidates and re-proves (size reflects the new object —
  object shrink covered by the 416/shrink fixture shape).
- `ValidateOnce`/`TrustForever` Stats serve the stored size inside the TTL (no origin
  storm: zero extra requests while trusted).
- Offline/inconclusive revalidation keeps serving the cached size (declared trust).
- Stat and Open agree: stat-then-read and read-then-stat sequences behave identically
  w.r.t. TTL/policy.

## O3 — Unicode orphan maintenance is safe and honest

- Non-ASCII file names in `chunks/` are correctly compared against manifest-referenced
  names (UTF-8 domain) on every platform — a referenced non-ASCII name is never treated
  as an orphan.
- A non-ASCII ORPHAN inside `chunks/` is removed by `pruneMirror` (through the
  `fs::u8path`/native-path route — never an ACP re-encode).
- Refusals stay fail-closed and accounted: names that cannot be converted to UTF-8,
  symlink/reparse-point entries, and files that fail removal (locked) are counted
  (`orphan_files_refused` / `orphan_files_failed`), never crash, never silently skipped.
- Nothing outside the mirror root is ever deleted (only paths enumerated inside
  `chunks/`; no path is recomposed from an untrusted name).
- `verifyMirror`'s orphan inventory uses the same name handling (no false-positive
  unreferenced count for referenced non-ASCII names).

## O4 — Observability is structured and honest

- `RangeCacheTelemetry`/JSON gains: fetch attempts, backoff sleeps, slot releases,
  cancel discards, TTL expirations, 304 refreshes (names decided in DECISIONS.md);
  exported through `telemetryJson()`.
- `MirrorPruneReport` gains refusal/failure counters; `MirrorVerifyReport` keeps an
  honest inventory.
- Microbench keeps gating requests/bytes/budgets only — no millisecond assertions.

## O5 — Regression: the 12.0 gates hold

- Warm cache = zero origin requests; churn bounded by budgets; cancel/ETag/corruption
  never publish wrong bytes; mirror verify/repair/prune legacy assertions all green.

## O6 — Fault-script discrimination

- At least one NEW test demonstrably catches an injected fault in EACH domain:
  (a) a permit held across backoff is detectable (a second fetcher starves without the
      release; the test observes admission during backoff),
  (b) a stale stat is detectable (TTL expiry forces a re-probe the server counts),
  (c) an unsafe orphan deletion is detectable (symlink/non-UTF8-name refusal counted,
      file preserved).
- Existing fault-script suites stay green.

## O7 — House gates

- `git diff --check` clean; targeted build green; two consecutive full-suite passes
  pre-review AND post-review; independent review P0=0, P1=0.
