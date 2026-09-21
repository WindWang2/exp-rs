# DECISIONS — Track H (autonomy=full; options + taken defaults)

## D-1301: admission scope = one origin transfer, not one fetch operation

Candidates:
- (a) Keep admission spanning the whole `fetchRange` incl. backoff (status quo — the
  documented limitation).
- (b) Acquire/release the in-flight byte slot PER ATTEMPT inside `fetchRange`; the
  backoff sleep holds nothing.
- (c) Release the slot for the sleep but keep a "reservation" token so the retry
  re-enters ahead of queue.

**Taken: (b).** The cap's purpose is bounding bytes concurrently in flight — a sleeping
thread has zero bytes in flight, so holding the slot is pure head-of-line blocking.
Attempt-budget fairness is already `fetchAttempts` (≤8); re-admission goes through the
same `admitFetch` wait (fair, no barging privilege). (c) adds a reservation concept no
oracle requires — YAGNI. The VSI-object path (`fetchRangeVsi`, single attempt) keeps its
admission in `serveOrFetch` — same scope (one transfer), no behavior change.

## D-1302: cancel-aware backoff via store-level condition variable

Candidates:
- (a) Poll `entry->fetchCancelled` on a coarse interval (e.g. 50 ms slices).
- (b) `std::condition_variable` on CacheStore, `notify_all` from `setFetchCancelled(true)`,
  `wait_for` with predicate on the entry flag.

**Taken: (b).** Wake latency is the CV's, not a poll quantum; no timed polling churn.
One CV shared by all entries: a cancel of ANY resource wakes all sleepers, each rechecks
its own flag — spurious wakes are cheap and rare (backoff sleeps are short, cancels
rarer). On wake-cancel the fetch throws `GeoError(Cancelled)`; `serveOrFetch`'s existing
catch degrades to `fallbackRead` — cancelled bytes never publish (the 12.0 discard
contract unchanged).

## D-1303: Stat shares the Open identity-resolution contract, not its code path

Candidates:
- (a) Extract a shared `resolveEntry` helper used by both Open and Stat (maximal
  deduplication, but a bigger refactor of Open's proven flow).
- (b) Give Stat the same ordered steps — TTL expiry → policy revalidation → probe —
  implemented for the stat context (size answer), reusing the same CacheStore primitives.

**Taken: (a), revised during implementation.** The trust block turned out cleanly
separable from Open's handle-construction concerns (vsiObject probe + credential
context are inputs, not interleaved), so `applyTrustPolicy(cache, key, requestUrl,
vsiPath, credentialContext, config)` was extracted as ONE shared implementation —
the strongest parity guarantee: Stat cannot diverge from Open because there is one
code path. Contract: TTL expiry → `invalidate` → caller's uniform probe path;
`RevalidateOnOpen` → Changed invalidates and returns nullptr (the caller re-probes —
Open's old early `return nullptr` is the identical end-state); Unchanged refreshes
size + `touchEntryProven`; Inconclusive keeps the entry (declared trust).
`ValidateOnce`/`TrustForever` add zero traffic. Requests per stat under
RevalidateOnOpen = one conditional GET — identical to Open's per-open cost.

## D-1304: orphan names — UTF-8 comparison, native-path deletion, refusal counters

Candidates:
- (a) Compare names as `fs::path` in native space (manifest name → `fs::u8path` → path
  compare) and delete via `it->path()`.
- (b) Keep the UTF-8 string domain for the referenced check (`filename().u8string()`),
  delete via `atomic_fs::removeFileQuiet(chunkDir + "/" + utf8Name)`.

**Taken: (b).** Manifest names are UTF-8 JSON strings — comparing in the same domain
keeps the orphan contract obvious. `u8string()` is a no-validation byte copy on POSIX
(invalid-UTF-8 names compare byte-wise, consistent with manifest bytes) and a checked
UTF-16→UTF-8 conversion on Windows: a name that cannot convert (invalid UTF-16 boundary)
is a REFUSAL — counted, never deleted (cannot prove orphanhood). Deletion stays inside
the atomic_fs house rule: `removeFileQuiet` re-encodes UTF-8→native via `fs::u8path`,
which is correct for any name `u8string()` produced — no ACP path anywhere. A
symlink/reparse entry is refused via `symlink_status` (not `status`) before any name
work; a failed `removeFileQuiet` counts `orphan_files_failed`. `verifyMirror`'s scan
gets the identical name handling (inventory parity, O3).

## D-1305: telemetry field names (additive, JSON-exported)

`RangeCacheTelemetry`: `fetch_attempts` (origin GET attempts incl. first tries),
`backoff_waits` (backoff sleeps entered — every sleep runs slot-free, so this IS the
slot-release count; a separate `retry_slot_releases` would alias it 1:1 and was
dropped), `admission_waits` (admissions that queued for capacity — the O1 delta
oracle), `cancelled_fetches` (veto outcomes: before start, in flight, during backoff),
`ttl_expirations` (entries dropped by age), `ttl_refreshes` (304/unchanged re-proofs).
`MirrorPruneReport`: `orphan_files_refused` (unconvertible name or symlink/reparse —
fail-closed), `orphan_files_failed` (removal attempted and failed — locked/permission).
Rationale: every new behavior gets a counter the oracles can assert on; gauges
(`in_flight_fetch_bytes`, `max_in_flight_fetch_bytes`) already exist.

## D-1306: no new fixture arms unless required

`HttpRangeServer`'s `setFaultScript` + `Slow`/`ServerError` arms already stage every
shape O1/O6 needs (scripted 500 → Normal for retry; Slow for cancel-in-flight). If a
finer-grained signal is needed (e.g. request-arrival timestamp), prefer reading the
existing `requestLog`/`requestCount` over adding state to the shared fixture.

## D-1307: stat-ok-but-unreadable manifest is corrupt, not empty (review P2 fix)

`readManifest` previously returned the empty object when `VSIStatL` succeeded but the
stream open failed — indistinguishable from a missing manifest, so verify reported
every chunk file orphaned and prune would have deleted the entire payload.
Alternatives: (a) keep empty-object (fail open — rejected, silent deletion risk);
(b) return null like an unparseable manifest (chosen) — every caller already refuses
null (verify/prune set `manifestUnreadable`; repair refuses; materialize rebuilds).
Same reasoning applied to the name domain: `utf8FileName` round-trips
`u8path(out) == filename` so a lossy U+FFFD-substituting conversion is a refusal,
not a trusted name.

## D-1308: accepted P3s (recorded, not fixed)

`removeFileQuiet` returning true on an already-absent path can over-count
`orphan_files_removed` in a narrow TOCTOU (outcome still safe); `ttl_expirations`
counts expiry decisions not drops (concurrent expiry can double-count); expired/quota
unlink failures in prune don't decrement `bytes_removed` (pre-existing accounting);
`admitFetch` charges requested bytes while a range-ignoring origin may send more
(pre-existing 9.0 gauge semantics); `manifestSnapshot`'s (size,mtime) key can serve
a stale snapshot only to a same-size in-tick rewrite — production writes always
invalidate, test-side hazard only. All are either pre-existing semantics or
safe-direction over-counts; none block the track oracles.
