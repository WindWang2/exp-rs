# DECISIONS — flash-data-scale-13

Autonomous decisions taken under `autonomy=full` (`.agents/AGENTS.md` unattended-mode rule):
candidates, tradeoffs, and the adopted default for each.

## D1 — How to remove the O(N²) snapshot publication

**Candidates**

| Option | Publish cost | Mutation cost | Reader cost | Notes |
|---|---|---|---|---|
| A. Keep flat `QVector<AssetRecord>` + eager full copy (status quo) | O(N) detach on next mutation | O(N) | O(N) | measured exponent ≈ 2.25 — the hotspot |
| B. Lazy publication: bump generation on mutation, materialize the immutable vector on first reader | O(1) | O(1) | O(N) build once per generation | **Rejected**: the build must copy the live vector, which races with owner-thread mutations unless the live side is journaled; with a shared journal it degrades to option C |
| C. Append-diff journal + periodic compaction into a shared base | O(1) | O(journal) ≤ O(K) | O(N + journal) | compaction is O(N) every K mutations → O(N²/K) total record copies |
| D. **Immutable chunked (sharded) record store with structural sharing** (ADOPTED) | **O(1)** (two shared_ptr copies) | O(K) (one shard copy, K = 256) | O(N) scan / O(N/K) shard hops | each record is copied at most once per shard fill; total population copies = O(N·K/2) with a small constant |

**Adopted: D.** `records` becomes a live pair {sealed shards (immutable, `shared_ptr<const QVector<shared_ptr<const Shard>>>`), live tail shard (`shared_ptr<const Shard>`)}. Sealing moves the tail into an immutable shard (QVector move, O(1)) and re-links the sealed vector (O(#shards) pointer copies). A published snapshot aliases both by `shared_ptr` — two refcount bumps, no record is ever copied for publication. Readers iterate sealed shards oldest→newest then the tail, preserving the previous insertion order exactly. Erase/replace COW-copy the affected shard (O(K)) and re-link (O(#shards)) — these are user-driven, off the population path.

**Tradeoff accepted**: per-mutation cost is O(K) shard-copy instead of O(N); with K = 256 that is ~10 µs, i.e. 100k registrations ≈ 1 s versus minutes before. An erase/replace COW-copies the affected shard (O(K) record copies) plus that shard's index rebuild (O(K × keys) hash inserts), and the sealed-vector re-link is O(#shards) pointer copies — so a bulk reap of 100k session-temporary assets costs ~100k × (256 record copies + ~768 hash inserts) rather than the ~2×10⁷ pointer copies an append-only design would need. Accepted: reaps are host-driven batch operations, not per-frame work, and the 100k ladder still completes in seconds.

## D2 — Path index representation

**Candidates**

| Option | Probe | Mutation | Staleness | Notes |
|---|---|---|---|---|
| A. Per-record linear scan + probe-time canonicalization (status quo) | O(N) syscalls | — | never | measured hotspot |
| B. Lazy per-generation rebuild of a `QHash` index | O(1) after build | O(1) (invalidate) | never stale, but a full rebuild (O(N) canonicalizations) per generation that is probed | **Rejected**: an interleaved mutate→probe pattern redoes full-catalog canonicalization on *every* lookup, violating the track contract |
| C. Incremental index carried inside the same shards as the records (ADOPTED) | O(#shards) hash lookups, zero syscalls | O(K) (shard copy carries the key hash) | maintained through add/update/delete — never stale | probe at 100k ≈ 40–80 µs (≈ 1300× faster than A), no per-record filesystem work |

**Adopted: C.** Each shard carries `QHash<QString, QVector<PathKeyEntry>> byKey` where `PathKeyEntry` is the owning record's index inside the same shard; records are only appended and removed, never reordered, so a lower index always means an earlier insertion and "first match in insertion order wins" needs no sequence number. Key tiers per record: alias spellings (`virtualPathAliases`), the raw `canonicalSource`, the absolute path, and the canonical (symlink-resolved) path. Virtual/remote records contribute alias keys only (their raw spelling is already one of the aliases); path tiers are inserted only for non-virtual records, mirroring the legacy asymmetry. Query keys are computed once per probe (one `QFileInfo` resolution of the query) and looked up in every shard oldest→newest; the walk stops at the first shard with a hit (later shards hold later insertions by construction). The probe is O(#shards) = O(N / kShardRecords) hash lookups plus the query's own single filesystem resolution — measured ~0.01–0.2 ms per probe at 100k versus ~100 ms for the scan.

**Tradeoff accepted (documented contract change)**: the stored-side canonical key is resolved when the record enters the catalog (or is relocated), not at probe time. A filesystem change that retargets a path *after* registration (e.g. a symlink repointed) is therefore not observed by the index. No test exercises that window, providers already canonicalize real files at registration (GDAL follows symlinks), and the equivalence oracle (legacy reference scan vs. index over a path matrix) pins every covered behavior.

## D3 — Governance pagination

**Adopted**
1. Additive composite indexes `(sort_key, pk)` for every ORDER BY variant (`assets(updated_ms, asset_id)`,
   `assets(display_name COLLATE NOCASE, asset_id)`, `assets(acquisition_ms, asset_id)`, and the
   `updated_ms, <pk>` pair for results/runs/datasets). `CREATE INDEX IF NOT EXISTS` — old stores gain
   them on next open; **schema_version stays "1"** (physical indexes, no logical schema change, no
   migration; the forward-tolerance rule is untouched).
2. Unique tiebreak appended to every ORDER BY (previously unspecified order for tied keys).
3. Keyset/seek pagination with the repo's existing opaque `sicnu::data::QueryCursor` (v1 | filter
   echo | keyset tuple, digest-verified): `WorkspaceQuery::cursor` in, `WorkspacePage::nextCursor` +
   `WorkspacePage::cursorError` out. The legacy `offset` path is preserved byte-for-byte for the
   single-page callers (`project:search` tool, CLI, smart collections); the workspace-browser panel
   (the only multi-page consumer) switches `fetchMore` to cursor paging.
4. Count semantics made explicit: `total` is computed for first-page queries (`offset == 0` and no
   cursor); cursor continuations return `total = 0` and terminate on an empty `nextCursor`. A
   cursor that fails to decode, or whose filter echo does not match the current query, yields an
   empty page with `cursorError` set — never fabricated rows.
5. `EXPLAIN QUERY PLAN` assertion in the new tests pins the index-driven plan.

**Rejected**: keeping `total` on every page (drain stays O(N²/pageSize) — the gate would not move);
translating page numbers to offsets internally (same rescan); bumping schema_version (would open
every existing store read-only).

## D4 — Scale oracles

Structural counters only (never absolute ms): record copies, key-entry copies, shard seals,
snapshot publications, path canonicalizations, path-index hash lookups, record visits. Ladder
1k → 10k → 100k with a fixed probe count; gates on copies-per-mutation ≤ shard capacity,
canonicalizations-per-probe ≤ constant (after the index exists for the generation), record visits
per probe = 0, and complexity exponents as secondary evidence.

## D5 — Concurrency/lifetime

Readers use only the snapshot-served API (contract unchanged). New oracle: 4 reader threads
hammering `findByPath`/`asset()`/`assets()` while the owner thread registers/unloads; every reader
observes a self-consistent snapshot (no id appears that was never registered; no read crashes);
`catalogGeneration()` strictly monotonic; a stale unload plan is still rejected. ASan/TSAN lane
attempted in a separate sanitizer build of the light target set only (no full-platform rebuild);
if the toolchain lane is unavailable, the race/lifetime arguments are reviewed statically and the
limitation is recorded.

## D6 — Environment

`cmake`/`ctest` live at `/tmp/cmake-3.30.5-linux-x86_64/bin` (not on PATH); the Ninja alias is
`-j40` and Ninja is not actually installed, so builds run through Unix Makefiles with
`cmake --build -j2`. GDAL comes from `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`; NetCDF is
absent on this host and every parallel track configures against the 8-byte stub
`/tmp/libnetcdf_stub.a` (this track's targets do not use NetCDF).
