# ORACLES — flash-data-scale-13

All gates are structural (counts, exponents, equivalence) — never absolute milliseconds.
"Two consecutive passes" means the exact command below run twice back-to-back, both green.

## O1 — Catalog population is not O(N²)

`ctest -R test_data_scale -j1` (new target), case "population copy count is linear in assets":
- populate 1k / 10k / 100k assets through `DataManager::registerSource`;
- structural counter `recordCopies` (detach copies during publication) must satisfy
  `copies(100k) / copies(10k) < 20` (i.e. exponent < log(20)/log(10) ≈ 1.30, allowing
  constant per-mutation chunk work) and `copies per mutation <= kChunkRecords`;
- the pre-existing observatory gate `obs_dataset_register_scaling` (exponent < 2.75) stays green.

## O2 — findByPath probe is O(1) and does not redo per-record canonicalization

- `ctest -R test_data_scale`: 100k-asset catalog, 1000 random probes; every probe resolves
  the same asset as the reference linear scan (equivalence oracle over local, relative,
  `mem://`, `/vsicurl/`, `https://`, mixed-case scheme, missing-file, and whitespace inputs);
- structural counter `canonicalizationCalls` must be ≤ probes × small constant (one
  resolution of the QUERY per probe, zero per-record filesystem work);
- probe cost stays in the shard-walk regime: the walk is O(#shards) = O(N/kShardRecords)
  hash lookups plus the query's own single filesystem resolution; the ladder gate bounds
  growth < 100x and the 100k rung < 2 ms (the linear scan it replaced costs ~1 us PER
  RECORD, ~100 ms per probe at 100k);
- observatory gates `obs_dataset_find_by_path_scaling` (< 1.5) and
  `obs_dataset_find_by_path_hotspot` stay green.

## O3 — Path index consistency after mutation

- after add / update (relocate, commitEdit, notifyExternalContentChange, promote) / delete
  (unload, reap) / rename (re-register same file under a new spelling), `findByPath` returns
  exactly what a brute-force reference scan returns (including insertion-order precedence
  and first-match-wins for duplicate keys);
- no stale entries: after erase, the erased asset's paths resolve to nothing (or to the
  remaining duplicate owner).

## O4 — Governance deep paging is not OFFSET-rescan dominated

- `ctest -R test_governance_store`, `test_workspace_stress`, `test_workspace_services`,
  `test_workspace_browser_wiring`, `test_governance_tools`, `test_perf_observatory` — all green;
- new keyset oracles: page-walk 100k rows via cursor returns every row exactly once, in a
  total order, with mutations between pages producing no duplicates and no unexplained skips;
  duplicate sort keys (same `updated_ms` batch) paginate stably with the id tiebreak;
- `EXPLAIN QUERY PLAN` for the paged SELECT shows an index-driven plan
  (`USING INDEX`/`SEARCH`, covering) rather than a table scan + sorter;
- observatory gate `obs_governance_paging_scaling` (worst doubling < 2.2) stays green and the
  recorded `ms_per_page` stops growing with N;
- count semantics documented and tested: `total` is computed for first-page queries
  (`offset==0`, no cursor); cursor continuations do not rescan for a count.

## O5 — 100k scale ladder completes with bounded structural memory

- 100k synthetic catalog: population + 1000 probes + full page drain complete;
- peak structural memory reported (record store chunks + index) and asserted to be within a
  stated multiple of the raw record bytes (no per-mutation snapshot retained).

## O6 — Concurrency & lifecycle

- multi-reader + mutation-writer stress (readers on foreign threads via snapshot-served
  readers only): no crash, no UAF (ASan lane where toolchain available), every reader sees a
  generation-consistent snapshot (no record appears/disappears mid-iteration);
- generation rollover: `catalogGeneration()` strictly monotonic; stale unload plans still
  rejected after the representation change;
- existing cross-thread tests stay green: `test_temporal_workspace.cpp` (#852 race test),
  `test_execution_plane_9.cpp` (M2 generation monotonicity), `test_data_manager_reap.cpp`
  (re-entrancy), `test_adversarial_m5.cpp` (affinity contract).

## O7 — Gate potency (anti-vacuous proof)

- at least one new test is proven to FAIL on an injected regression: re-introduce the
  per-mutation full-copy publication (or a per-record canonicalization in the probe path,
  or drop the id tiebreak / use OFFSET in the panel) and watch the new gate go red;
- the observatory's own potency precedent (ledger Round 13: injected temporal fold +
  governance page-count lie both fired) stays intact.

## O8 — Legacy correctness

- all pre-existing suites that touch the owned surface stay green, two consecutive passes:
  `test_data_manager`, `test_data_manager_collection`, `test_data_manager_dependency`,
  `test_data_manager_panel`, `test_data_manager_promote`, `test_data_manager_reap`,
  `test_asset_catalog_index`, `test_workspace_catalog`, `test_workspace_snapshot`,
  `test_workspace_state`, `test_workspace_project_v3`, `test_temporal_workspace`,
  `test_object_identity`, `test_workflow_resume_provenance`, `test_execution_plane_9`,
  `test_adversarial_m5`, `test_e2e_open_issues`, `test_execution_benchmarks`,
  `test_governance_*`, `test_workspace_*`, `test_perf_*`.

## O9 — Independent review

- read-only reviewer on `origin/master...HEAD`: P0 = 0, P1 = 0 after fixes; second-pass
  review confirms no regression from the fixes.

## O10 — Final oracle (shared)

- fresh worktree from live `origin/master`; dedup re-read; targeted build green; two
  consecutive passes of O1–O8 gates; `git diff --check` clean; final fetch + overlap scan;
  PR created (base `master`), no self-merge, no CI wait.
