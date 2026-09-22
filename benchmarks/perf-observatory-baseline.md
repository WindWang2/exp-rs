# Performance Observatory — baseline and hotspot evidence

Schema `sicnu-perf-observatory/1`. Machine: Linux x64, GCC 16, **Debug**, 16 cores
(shared with parallel agent sessions), Qt 6.11.2, GDAL from the local SDK root,
SQLite 3.53. Produced by `test_perf_observatory` + `test_perf_io_observatory` at
`SICNU_OBS_SCALE=small`. The record files under `benchmarks/observatory/` and
every number below come from the **same** run.

Reproduce:

```bash
cmake --build <build> --target test_perf_io_observatory test_perf_observatory
export SICNU_OBS_OUT=$PWD/obs-run QT_QPA_PLATFORM=offscreen
ctest --test-dir <build> -j1 --output-on-failure \
    -R "obs (io|dataset|governance|taskcenter|temporal|tiled)"
python scripts/bench/perf_observatory_report.py compare obs-run benchmarks/observatory
```

**Every claim below is labelled MEASURED (a number the harness produced),
INFERRED (a mechanism read off source lines), or RECOMMENDED (a candidate
change).** A recommendation that is not anchored to a measurement is not made.

> **Data Scale 13.0 regeneration.** The previous committed records were produced
> on a Windows/MSVC machine by the observatory track (#1125), which measured
> three hotspots and deliberately left them unfixed. Branch
> `agent/flash-data-scale-13` lands those fixes and regenerates every record
> with the same command on this machine. The before numbers below are the
> committed Windows records plus a same-machine A/B (master code vs branch
> code, both at `SICNU_OBS_SCALE=small` and `mid`); the after numbers are this
> run's records. All three hotspots are now **closed**; the gates that bounded
> them stay in place unchanged.

---

## What the harness certifies as healthy (MEASURED)

| contract | measurement | verdict |
|---|---|---|
| Windowed raster scan visits every tile once | `tiles_observed == tiles_expected`, pixels == side² | holds |
| Full raster read and windowed scan agree | identical checksums, `max_abs_error < 1e-3` | holds |
| Atomic writer round-trips values | 0 mismatched pixels vs an independent replay | holds |
| `findByPath` complexity | exponent **0.00** over 2000 → 4000 assets (index probe, not a scan) | better than linear |
| Governance paging never materializes the table | first page 200 rows = `kPageSize`; 10 pages = ceil(2000/200); materialized rows = 200 | holds |
| Temporal fold produces the right numbers | 6 sampled pixels vs an independent generator replay: 0 mismatches, max error 1.5e-06 | holds |
| Temporal tile scratch is date-count independent | `peak_slots` identical at 3 and 6 scenes | holds |
| Tiled inference working set is tile-proportional | 524288 bytes = 2 × 256² × 4, independent of raster size | holds |
| TaskCenter drains a batch | 64/64 completed; queue wait mean 0.11 ms, max 1.0 ms | holds |
| DAG gates children on parents | 0 ordering violations across a 4-level chain | holds (order-sensitive: see limits) |

Gate potency was checked by injecting defects, not by reading the code: a
scene-dropping bug in the temporal fold made all 6 sampled pixels mismatch
(max error 41.8), and a "pretend one page held the whole table" regression
failed `pages == expectedPages` with `1 == 10`. The Data Scale oracles add
their own potency evidence in `tests/test_data_scale.cpp` (the equivalence
oracle compares the indexed lookup against a verbatim copy of the pre-change
algorithm over a path matrix).

---

## Hotspot 1 — catalog registration was quadratic in catalog size — CLOSED

**MEASURED (before, committed Windows record).** `obs_dataset_register_scaling`,
1000 → 2000 assets: 1256.8 ms → 5992.1 ms, empirical exponent **2.25**.
**MEASURED (same-machine A/B, master vs branch):** exponent **1.99 → 1.00**,
wall **4540 ms → 529 ms** at the `small` rung. **MEASURED (this run):** exponent
**1.03**, wall 512.6 ms.

**INFERRED then, IMPLEMENTED now.** `publishSnapshot()` used to copy the live
`QVector<AssetRecord>` into every published snapshot; Qt copy-on-write made the
publication itself cheap but the *next* mutation detached and deep-copied every
record, so N registrations paid ~N²/2 record copies. The store is now an
immutable chunked structure (`src/data/internal/catalog_record_store.*`):
fixed-capacity shards plus a live tail, publication aliases both by
`shared_ptr` (O(1)), and a mutation copies at most one shard (bounded by
`kShardRecords`). Structural counters in `CatalogScaleCounters` prove it:
124 record copies per mutation at 1k assets, 127 at 100k — linear, not
quadratic (the 1k→100k ladder in `tests/test_data_scale.cpp` gates
`copies(10k)/copies(1k) < 20` and `copies ≤ count × kShardRecords`).

## Hotspot 2 — `findByPath` redid per-record identity work on every probe — CLOSED

**MEASURED (before, committed Windows record).** Controlled A/B at a fixed
2000-asset catalog: the filesystem-backed spelling cost 115 448 µs per probe vs
5554 µs for the virtual spelling — 57.7 µs per record per probe.
**MEASURED (same-machine A/B, master vs branch):** wall **278 ms → 2.4 ms** for
the hotspot workload (115×), probe exponent **0.81 → 0.00**, per-record cost
**2.17 µs → 0.019 µs**. **MEASURED (this run):** local-ish 35.0 µs/probe,
0.017 µs/record, identity-branch slowdown **0.91×** (was 3.4–20.8×).

**IMPLEMENTED.** Each shard carries a tier-prefixed path→record key index
(`a|` alias/string tier, `p|` filesystem path tier) computed once per mutation;
a probe resolves at most the query path and walks the shard maps — zero
per-record work, zero per-record filesystem resolutions (counters:
`record_visits == 0`, `pathCanonicalizations ≈ probes`). Semantics are pinned
by an equivalence oracle against a verbatim copy of the pre-change algorithm
(aliases, `/vsicurl` ↔ `https`, relative/absolute/canonical/symlink spellings,
case-variant schemes, unicode, empty/whitespace, insertion-order precedence).

## Hotspot 3 — governance paging cost per page grew with the table — CLOSED

**MEASURED (before, committed Windows record).** `obs_governance_paging_scaling`,
500/1000/2000 rows: 9.3 / 15.5 / 21.9 ms per page, worst doubling **1.50**.
**MEASURED (same-machine A/B at `SICNU_OBS_SCALE=mid`, 2500/5000/10000 rows —
where the OFFSET rescan actually dominates):** master **7.0 / 10.2 / 16.9 ms
per page** (worst doubling 1.72) vs branch **4.4 / 4.8 / 4.8 ms per page**
(worst doubling **1.05**) — flat. **MEASURED (this run, small):** 3.1 / 3.9 /
4.5 ms per page, worst doubling **1.21**.

**IMPLEMENTED.** `GovernanceStore::query()` now issues a row-value keyset seek
(`(sort_key, pk) < (?, ?)`, tiebreak sharing the sort key's direction so one
index walk serves both the ORDER BY and the seek — `EXPLAIN QUERY PLAN`:
`SEARCH … USING COVERING INDEX ((updated_ms,asset_id)<(?,?))`, no sorter) and
composite `(sort_key, pk)` indexes back every ORDER BY variant. The legacy
`offset` path is preserved for its single-page callers; the workspace-browser
panel (the only multi-page consumer) pages by cursor. `total` stays the real
`COUNT(*)` for non-cursor queries and is not recomputed for cursor
continuations; a cursor that fails to decode or whose filter echo does not
match yields an empty page with `cursorError` — never fabricated rows.
Correctness oracles: a 20 000-row cursor walk returns every row exactly once
(no duplicates, no skips, `pages == ceil(rows/pageSize)`), duplicate sort keys
paginate stably, and a bogus or filter-mismatched cursor fails closed.

---

## Honest limits of this baseline

- **The windowed-scan complexity gate is noise-dominated on this host.** At the
  `small` rungs the scan is sub-millisecond, so the measured exponent moves
  between 0.0 and ~2.2 between runs (this workload links only
  `Sicnu::Geospatial` — it does not link any code this branch changes). It
  passes in isolation and in most full runs; when it fires, re-run. The gate
  itself is unchanged.
- **No absolute millisecond budget.** Runs on this shared 16-core host (with
  parallel agent sessions compiling) differ by tens of percent on the same
  workload, which is exactly why the gates are counts, complexity exponents and
  structural memory units.
- **`peak_rss_mb` is often `null`**, with a reason: the watermark is 1 MiB /
  2 ms sampled, so a sub-MiB or sub-interval allocation is invisible. A zero
  there would be the fake number the schema promises never to write.
- **`cpu_ms` is process CPU across all threads**, so it can exceed `wall_ms`
  on a multi-core lane and is not comparable across machines.
- **The text (`sortBy=name`) keyset seek falls back to an ordered index scan**
  (O(position) per page): SQLite will not turn the `COLLATE NOCASE` row-value
  comparison into an index range. No production caller pages deeply by name —
  the workspace browser uses the default sort and `project:search` is
  single-page — so this is documented, not gated.
- **A record's canonical (symlink-resolved) identity is resolved when the
  record enters the catalog or is relocated, not per probe** (see ADR 0166).
  A filesystem change that retargets a path *after* registration is not
  observed by the index.
- **Pre-existing red tests on this host, reproduced on master code and outside
  this branch's ownership** (see the PR body): the `#860` observer re-entrancy
  case in `test_execution_plane_9`, the affinity-warning expectation in
  `test_data_manager_reap`, and the untranslated labels in
  `test_data_manager_panel` (no `.qm` files are built by this preset).
- **No scientific-semantics change.** None of these workloads alters a result;
  the writer round-trip, full/windowed and temporal replay comparisons are
  equivalence oracles, and all pass.
- **No GPU dependency.** Every workload runs on CPU in a Debug build.
