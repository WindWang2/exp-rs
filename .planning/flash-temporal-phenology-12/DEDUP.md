# DEDUP — flash-temporal-phenology-12

Checked at fetch time 2026-09-20 ~00:55 +08:00 against live GitHub state.

## Open PRs — overlap verdict

| PR | Verdict | Evidence |
|----|---------|----------|
| #1116 geospatial-fabric-12 | No semantic overlap. Only shared file: `tests/CMakeLists.txt` (append-only). `src/geospatial/{fabric,remote,stac}` read-only for this track. | `gh pr view 1116 --json files` |
| #1117 data-experiment-12 | No semantic overlap (`src/data`, `src/dataset`, `src/experiment`). Shared: `tests/CMakeLists.txt`. | file list |
| #1118 model-runtime-12 | No semantic overlap (`src/operators/{framework,runtime}`). Shared: `src/operators/CMakeLists.txt`, `tests/CMakeLists.txt`. | file list |
| #1119 hyperspectral-12 | **Partial file overlap, no semantic overlap.** Edits 4 temporal `algorithm_meta` sidecars (`rs-temporal-extract-regions`, `rs-temporal-harmonic-breaks`, `rs-temporal-region-features`, `rs-temporal-regularize`) — likely meta-drift fixes. My new operators get NEW sidecar names → no conflict. Shares `rs_operators_init.cpp`, `src/operators/CMakeLists.txt`, `src/processing/CMakeLists.txt`, `tests/CMakeLists.txt`, `determinism_census.snap.json`. | file list |
| #1120 offline-labs-12 | No overlap (labs/packaging/tooling). Shared: `tests/CMakeLists.txt`, `.gitignore` append. | file list |

## Merged PRs covering this domain (do-not-rebuild)

- #973 temporal-eo-phenology-change-10
- #986 temporal-phenology-timeline (D16)
- #1014 temporal-intelligence-11
- #1113 EVI/SAVI scale-probe fix + parseIsoDate hardening

## Remote branches — residue

None temporal-scoped. `*-12` branches are the open PRs above. Others are
historical fix/integrity tracks superseded by #1100–#1115. Not cherry-picking.

## Issues

- Open: none.
- Recently closed relevant: #1076 (temporal_index_series stale buffer — fixed),
  #1044 (unbounded `temporal_length` knob → overflow; check bound handling
  before adding new length knobs).

## Delta

All 7 WPs = gaps on top of existing `src/processing/algorithms/temporal/` +
`src/operators/rs/rs_temporal_*`. No open PR duplicates any WP.
