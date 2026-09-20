# Performance Observatory — baseline and hotspot evidence

Schema `sicnu-perf-observatory/1`. Machine: Windows 11 x64, MSVC 19.38.33130,
**Debug**, 16 cores, Qt 6.8.0, GDAL from the x64-windows vcpkg manifest.
Produced by `test_perf_observatory` + `test_perf_io_observatory` at
`SICNU_OBS_SCALE=small`. The record files under `benchmarks/observatory/` and
every number below come from the **same** run.

Reproduce:

```bat
cmake --build <build> --target test_perf_io_observatory test_perf_observatory
set SICNU_OBS_OUT=%TEMP%\obs-run
set QT_QPA_PLATFORM=offscreen
ctest --test-dir <build> -j1 --output-on-failure ^
    -R "obs (io|dataset|governance|taskcenter|temporal|tiled)"
python scripts\bench\perf_observatory_report.py compare %TEMP%\obs-run benchmarks\observatory
```

**Every claim below is labelled MEASURED (a number the harness produced),
INFERRED (a mechanism read off source lines), or RECOMMENDED (a candidate
change).** A recommendation that is not anchored to a measurement is not made.

---

## What the harness certifies as healthy (MEASURED)

| contract | measurement | verdict |
|---|---|---|
| Windowed raster scan visits every tile once | `tiles_observed == tiles_expected`, pixels == side² | holds |
| Full raster read and windowed scan agree | identical checksums, `max_abs_error < 1e-3` | holds |
| Atomic writer round-trips values | 0 mismatched pixels vs an independent replay | holds |
| `findByPath` is O(catalog) | exponent under 1.5 over 2000 → 4000 assets (1.55 and 2.25 style spread across runs) | linear, as intended |
| Governance paging never materializes the table | first page 200 rows = `kPageSize`; 10 pages = ceil(2000/200); materialized rows = 200 | holds |
| Temporal fold produces the right numbers | 6 sampled pixels vs an independent generator replay: **0 mismatches, max error 1.5e-06** (float32 rounding) | holds |
| Temporal tile scratch is date-count independent | `peak_slots` 131072 at 3 scenes vs 131072 at 6 scenes — **identical** | holds |
| Tiled inference working set is tile-proportional | 524288 bytes = 2 × 256² × 4, independent of raster size | holds |
| TaskCenter drains a batch | 64/64 completed; queue wait mean 0.11 ms, max 1.0 ms | holds |
| DAG gates children on parents | 0 ordering violations across a 4-level chain | holds |

Gate potency was checked by injecting defects, not by reading the code: a
scene-dropping bug in the temporal fold made all 6 sampled pixels mismatch
(max error 41.8), and a "pretend one page held the whole table" regression
failed `pages == expectedPages` with `1 == 10`.

---

## Hotspot 1 — catalog registration is quadratic in catalog size

**MEASURED.** `obs_dataset_register_scaling`, 1000 → 2000 assets:

| assets | wall ms |
|---|---|
| 1000 | 1256.8 |
| 2000 | 5992.1 |

Empirical exponent **2.25** (base-2) in the committed record, i.e. registering
2 000 assets cost 4.8× what 1 000 cost rather than 2×. Across the runs made
while building this track the exponent moved between 1.55 and 2.25 — the gate is
set at 2.75 so that spread is not a flake, and only a path that becomes worse
than quadratic trips it.

**INFERRED.** `DataManager::publishSnapshot()`
(`src/data/data_manager.cpp:210-224`) constructs a fresh `CatalogSnapshot` and
copies **every** `AssetRecord` on **every** mutation, so `registerSource()` is
O(N) and a batch of N registrations is O(N²). At the `scale` rung (20 000
assets) that is seconds of pure copying.

**RECOMMENDED.** Copy-on-write or append-diff snapshot publication (publish a
generation plus the mutation, or an immutable chain of record vectors) turns
population into O(N) total. The gate `checkComplexity(exponent, 2.75)` documents
the current behaviour and trips if the per-mutation cost itself starts growing
with N.

**Scope note.** `src/data/` is production code outside this track's owner
area; no change is made here — this is evidence for a follow-up issue.

---

## Hotspot 2 — `findByPath` redoes per-record identity work on every probe

**MEASURED.** Controlled A/B at a **fixed** 2000-asset catalog, same 64 probes,
differing only in the scheme of the stored canonical source:

| case | µs per probe |
|---|---|
| `/vsicurl/https://…` (string/alias identity) | 5554 |
| `mem://…` (treated as filesystem-backed) | 115448 |

In the committed record the local-ish case is **20.8×** slower than the virtual
one, i.e. **57.7 µs per record per probe**. The per-probe numbers are printed
by the run itself (`virtual_path_us_per_probe` / `localish_path_us_per_probe` in
`obs_dataset_find_by_path_hotspot.json`) rather than transcribed here, because
they move by tens of percent between runs on a loaded machine.

**INFERRED.** `DataManager::findByPath()`
(`src/data/data_manager.cpp:821-872`) loops over the whole catalog and, for
every record, rebuilds `virtualPathAliases(stored)` (a fresh `QStringList`
alloc) and constructs a `QFileInfo` whose `canonicalFilePath()` resolves the
path. `isVirtualOrRemotePath()` only recognises `/vsi`, `http://` and
`https://`, so a path with any other scheme takes the filesystem branch for
every record on every probe — O(N) alias allocations and path resolutions per
lookup, with no index behind it.

**RECOMMENDED.** Cache the resolved alias set and the canonical path as part
of the record (or behind a path→index map rebuilt per generation), so a probe
is one hash lookup plus alias comparison. Both numbers above are the
before/after anchor; the gate already bounds the overall scan at exponent
1.5, which catches a *worse* regressor but not this constant-factor cost.

---

## Hotspot 3 — governance paging cost per page grows with the table

**MEASURED.** `obs_governance_paging_scaling`, three rungs walked in one
process (500 / 1000 / 2000 rows):

| rows | wall ms | ms per page |
|---|---|---|
| 500 | — | 9.3 |
| 1000 | — | 15.5 |
| 2000 | — | 21.9 |

Worst doubling exponent **1.50**; ladder exponent **2.97**. Per-page cost
roughly doubles from the smallest rung to the largest.

**INFERRED.** `GovernanceStore::query()` issues a `SELECT COUNT(*)` and then a
`SELECT … LIMIT ? OFFSET ?` per page (`src/data/governance/governance_store.cpp`
around 2424-2445). OFFSET pagination makes each page rescan everything before
it, so a full drain is O(rows × pages) rather than O(rows). The memory
behaviour is already correct — one page at a time — so this is purely a
latency/complexity issue that only shows up on large workspaces.

**RECOMMENDED.** Keyset (seek) pagination — carry the last row's sort key into
`WHERE key > ?` instead of `OFFSET ?` — or index the filter column so the
offset scan does not walk the table. The gate is set at 2.2 for the worst
doubling: above the measured 1.50 baseline, so it catches further degradation
toward O(N²) without flagging the known behaviour.

---

## Honest limits of this baseline

- **The windowed-scan complexity gate measured exponent 0.00** (the 512² rung
  was *faster* than the 256² rung, so the exponent clamps to zero). At these
  sizes the scan is noise-dominated: the gate `exponent < 1.5` is directionally
  right — it fires if a 4× area scan costs ≥ 2.83× — but nothing in the current
  numbers is close to it, and the record says so rather than hiding it.
- **No absolute millisecond budget.** The two runs compared above differ by up
  to **−71 %** on the same 14.9 ms workload and **+28 %** on an 11 ms one. On a
  shared machine that spread is normal, which is exactly why the gates are
  counts, complexity exponents and structural memory units.
- **`peak_rss_mb` is often `null`**, with a reason: the watermark is 1 MiB /
  2 ms sampled, so a sub-MiB or sub-interval allocation is invisible. A zero
  there would be the fake number the schema promises never to write.
- **`cpu_ms` is process CPU across all threads**, so it can exceed `wall_ms`
  on a multi-core lane and is not comparable across machines.
- **No scientific-semantics change.** None of these workloads alters a result;
  the writer round-trip, full/windowed and temporal replay comparisons are
  equivalence oracles, and all pass.
- **No GPU dependency.** Every workload runs on CPU in a Debug build.
