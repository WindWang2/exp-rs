# ADR 0166: Data Scale 13.0 — Immutable Chunked Catalog, Path Index, Keyset Paging

Status: accepted · Branch `agent/flash-data-scale-13` · Baseline `origin/master@79adfe78a1`

## Context

The merged Performance Observatory (#1125) measured three structural hotspots in
`src/data/**` with machine-independent indicators and committed the evidence
(`benchmarks/observatory/*.json`), but the fixes were deliberately out of its
scope:

1. **Catalog population was O(N²).** `DataManager::Impl::publishSnapshot()`
   copied the live `QVector<AssetRecord>` into every published snapshot. Qt
   copy-on-write makes the publication itself a refcount bump, but the *next*
   mutation of the live vector then detaches and deep-copies every record — so
   registering N assets pays ~N²/2 record copies
   (`obs_dataset_register_scaling.json`, exponent ≈ 2.25).
2. **`findByPath()` re-derived identity per probe.** Every probe re-ran
   `virtualPathAliases()` (a `QStringList` allocation) and
   `QFileInfo::canonicalFilePath()` (filesystem syscalls) for *every* record
   (`obs_dataset_find_by_path_hotspot.json` records the controlled A/B).
3. **Governance deep paging was OFFSET-rescan dominated.**
   `GovernanceStore::query()` ran `COUNT(*)` + `SELECT … ORDER BY <key> LIMIT ?
   OFFSET ?` with no unique tiebreak, no index on any sort key, and a total
   recomputed per page (`obs_governance_paging_scaling.json`, worst doubling
   ≈ 1.71–1.77).

The three fixes had to preserve two hard contracts: the DataManager's
thread-affinity model (owner-thread mutations; snapshot-served const readers,
#703/#852) and `findByPath`'s observable semantics (alias/string identity
first, then raw/canonical/absolute path tiers for non-virtual spellings on both
sides, first match in insertion order).

## Decisions

1. **Immutable chunked record store** (`src/data/internal/catalog_record_store.*`).
   Records live in fixed-capacity immutable shards (`kShardRecords = 256`) plus
   one live tail shard. A published snapshot aliases the sealed list and the
   tail by `std::shared_ptr` — two refcount bumps, no record is touched. A
   mutation first makes its target shard private (one COW copy bounded by the
   shard capacity), so already-published snapshots stay immutable. Sealing moves
   the tail into a shard (O(1)); erases and replaces COW-copy the affected shard
   and rebuild only that shard's key index. Per-mutation record copies are
   bounded by a constant; the 1k→100k ladder measures 124 → 127 copies per
   mutation (linear), versus ~N/2 per mutation before.
2. **Generation-scoped path index inside the shards.** Each shard carries a
   `QHash<QString, QVector<PathKeyEntry>>` over tier-prefixed identity keys
   (`a|` alias/string tier, `p|` filesystem path tier). Keys are computed once
   per mutation (one `canonicalFilePath` per record); a probe resolves at most
   the query path and walks the shard maps — zero per-record work, zero
   per-record syscalls. The walk order (sealed shards oldest→newest, then the
   tail) reproduces "first match in insertion order" exactly, and the tier
   separation reproduces the legacy virtual/non-virtual asymmetry (remote/VSI
   records contribute alias keys only; path tiers are consulted only for
   non-virtual spellings on both sides). Semantics are pinned by an equivalence
   oracle in `tests/test_data_scale.cpp` that runs a verbatim copy of the
   pre-change algorithm beside the indexed one over a path matrix (existing
   local files, symlinks, relative and dot-segment spellings, missing files,
   `/vsicurl/` ↔ `https` aliases in both directions, case-variant schemes,
   unicode, empty/whitespace).
3. **Live-side source-identity index.** The registration/restore/relocate
   conflict scans consult a `QHash<QString, AssetId>` keyed by a faithful
   serialization of the `SourceKey` fields of the record's descriptor. Those
   scans were O(N) per insert — on their own enough to make a 100k population
   pay ~5e9 key comparisons — and they are the reason the measured exponent was
   2.25 rather than exactly 2. The index is live-side only (every consumer is an
   owner-affine mutation path), so it needs no copy-on-write.
4. **Keyset pagination for the governance store.** Additive
   `CREATE INDEX IF NOT EXISTS (sort_key, pk)` indexes back every `ORDER BY`
   variant, each `ORDER BY` gains its primary key as a unique tiebreak (a whole
   upsert batch shares one `updated_ms` stamp, so without it the order of tied
   rows is unspecified and pages could skip/duplicate), and
   `WorkspaceQuery::cursor` / `WorkspacePage::nextCursor` wire the repo's
   existing opaque `sicnu::data::QueryCursor` (v1 | filter echo | keyset tuple,
   digest-verified) into a `(sort_key < ? OR (sort_key = ? AND pk > ?))` seek.
   The legacy `offset` path is preserved byte-for-byte for its single-page
   callers (`project:search`, CLI, smart collections); the workspace-browser
   panel — the only multi-page consumer — pages by cursor. `total` stays the
   real `COUNT(*)` for every non-cursor query and is *not* recomputed for
   cursor continuations (a deep walk would otherwise pay O(rows) per page);
   continuation termination is an empty `nextCursor`. A cursor that fails to
   decode, or whose filter echo does not match the current filters, yields an
   empty page with `cursorError` set — never fabricated rows. `schema_version`
   stays `"1"`: indexes are additive and idempotent, so existing stores gain
   them on open and the forward-tolerance rule is untouched.
5. **Structural, not temporal, gates.** `CatalogScaleCounters` records record
   copies, shard seals, publications, filesystem canonicalizations, index
   lookups and record visits. The scale oracles gate on those counts plus
   equivalence against the pre-change algorithm; wall-clock exponents are
   recorded as secondary evidence only. The observatory's existing gates
   (registration exponent < 2.75, findByPath exponent < 1.5, governance worst
   doubling < 2.2) are unchanged and now pass with room.

## Consequences

* Population of 100k assets costs ~12.7M record copies (≈127 per mutation) and
  completes in seconds; the old implementation needed minutes and ~5e9 copies.
* `findByPath` over a 100k catalog performs zero per-record work and zero
  per-record filesystem resolutions per probe (1000 probes: 0 record visits,
  0 canonicalizations, ~591k shard-hash lookups — the walk is O(shards), not
  O(records)).
* Documented contract change: a record's canonical (symlink-resolved) identity
  is resolved when the record enters the catalog or is relocated, not per
  probe. A filesystem change that retargets a path *after* registration is
  therefore not observed by the index; no test exercises that window and
  providers already canonicalize real files at registration.
* Governance deep pages are index seeks; the cursor walk returns every row
  exactly once in a total order, with mutation-between-pages behaviour
  (no duplicates; concurrent inserts before the cursor are not revisited)
  documented and tested.
* Reader/writer concurrency keeps the existing contract: readers only touch
  published snapshots (immutable shards), the mutation side is owner-affine,
  and the shared/private flags that drive copy-on-write are owner-thread state.
