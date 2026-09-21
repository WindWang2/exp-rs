# OWNERSHIP — flash-temporal-phenology-12

## Writable (this track owns)

- `.planning/flash-temporal-phenology-12/*.md` (gitignore whitelist added;
  markdown-only per repo convention)
- `.goal-loop-ledger.md` at worktree ROOT — gitignored (`/.goal-loop-ledger.md`,
  line 312, marked "local-only process artifact"). Kept uncommitted per runbook
  default ("只留在 worktree，不与其他 Track 争抢共享账本").
- New files under `src/processing/algorithms/temporal/` — candidate home for
  temporal12 additions (final placement per DECISIONS.md after census)
- New operator files `src/operators/rs/rs_temporal_*` (new files only)
- New algorithm_meta sidecars `data/processing/algorithm_meta/rs-temporal-*.json`
  for NEW operators only (never the 4 files #1119 edits)
- New test files `tests/test_temporal_*` and fixtures/support helpers
- `docs/adr/0166+` if a design decision warrants (check next free number)

## Shared — append-only minimal diff

- `.gitignore` — whitelist (done)
- `src/operators/CMakeLists.txt`, `src/processing/CMakeLists.txt`,
  `tests/CMakeLists.txt` — add lines only (shared w/ #1116–#1120)
- `src/operators/rs/rs_operators_init.cpp` — register new operators only
  (shared w/ #1119)
- `data/processing/toolbox_manifest.json` — only if new processing algorithms
- `data/contracts/determinism_census.snap.json` — only if census test requires

## Read-only for this track

- `src/geospatial/{fabric,remote,stac}` (#1116)
- `src/data/**`, `src/dataset/**`, `src/experiment/**` (#1117)
- `src/operators/{framework,runtime}/model_*` (#1118)
- `src/processing/algorithms/spectral_*` + spectral meta (#1119)
- `data/labs/**`, `packaging/**`, `scripts/**`, `tools/**` (#1120)
- `src/workflow/pipeline_run_coordinator.cpp` (#1117/#1118/#1119 all touch it)
- TaskCenter / Geo Fabric kernels (runbook rule)
- The 4 temporal algorithm_meta files edited by #1119
- GUI: at most metadata additions for result display

## Existing temporal files — modify only with justification

`src/processing/algorithms/temporal/` + `src/operators/rs/rs_temporal_*` are
this track's domain; semantic extension allowed when a WP requires it, each edit
justified in DECISIONS.md, no behavior regression vs existing tests.
