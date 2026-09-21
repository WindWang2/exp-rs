# BASELINE — flash-sar-radiometry-13 (Track E)

- **Date**: 2026-09-21
- **Track**: GLM-5.3-Flash Track E — SAR Radiometric State & Calibration Chain 13.0
- **Branch**: `agent/flash-sar-radiometry-13`
- **Baseline SHA**: `79adfe78a16b9419eef180cf9e6e5739658621a2` (origin/master, fetched 2026-09-21)
- **Worktree**: `/home/kevin/project/exp-rs-sar-radiometry-13` (created from origin/master via `scripts/dev/new_worktree.py`; `git merge-base HEAD origin/master` == HEAD == baseline SHA)

## Live PR / issue state at fetch time

Open PRs (3): #1137 geospatial-maintenance-13 (retry/Stat TTL — no SAR files),
#1136 offline-labs-13 (touches `data/labs/lab12_sar_processing.lab.json` only),
#1135 flash-temporal-phenology-12 (touches `rs_temporal_sar_fusion_operator.*` +
`data/processing/algorithm_meta/capability/rs-temporal-sar-fusion.json`).
Open issues: 0.

Overlap verdict: none of the three open PRs touches this track's ownership
(`src/processing/algorithms/sar/*`, `src/operators/rs/rs_sar_*`, SAR tests,
`docs/processing/sar-domain.md`). PR #1135's temporal-SAR-fusion files are
explicitly excluded here.

## Direct predecessor

PR #1131 (SAR 12.0, merged `2eb5c176`) established the declared-calibration
fail-closed guards in `rs:sar_calibrate` / `rs:sar_backscatter` and state
propagation in `rs:sar_speckle`, and documented its own follow-ups (verbatim
from the PR body):

- `rs:sar_terrain_flatten` / `rs:sar_terrain_correction` hardcode `gamma0`
  regardless of input state.
- `rs:sar_geocode` copies the raw calibration token but never writes
  `SICNU_RADIOMETRIC_STATE`.
- `rs:sar_ratio` / `rs:sar_texture` drop the state.
- DN products with LUT-based calibration remain out of scope.

This track implements exactly those recorded gaps after an independent census.

## Census evidence (23 registered `rs:sar_*` operators)

Registry: `src/operators/rs/rs_operators_init.cpp` (REGISTER_RS_OPERATOR +
explicit `add()` list — the only guaranteed registration path, #707).
State-token reads/writes per operator (grep census, see DEDUP.md for the table):

| operator | reads state | writes state |
|---|---|---|
| rs:sar_calibrate | declaredCalibrationToken | sigma0 + full block |
| rs:sar_backscatter | declaredCalibrationToken | to-state + full block |
| rs:sar_speckle | readCalibration (kernel) | propagates (SAR 12.0) |
| rs:sar_terrain_flatten | readDomain only | gamma0 (kernel) + RADIOMETRIC_STATE (operator) |
| rs:sar_terrain_correction | readDomain only | gamma0 (kernel) + RADIOMETRIC_STATE (operator) |
| rs:sar_geocode | none | raw token copy only; no RADIOMETRIC_STATE |
| rs:sar_ratio | readDomain (both inputs) | nothing |
| rs:sar_texture | none | nothing |
| rs:sar_change | readDomain (both inputs) | nothing |
| rs:sar_coregister(_local), interferogram, phase_filter, unwrap, remove_topographic_phase, polsar_decompose, dualpol_features, displacement, pair_network, network_inversion, terrain_masks, temporal_stats, temporal_events | none | none (complex/geometry/mask/statistics products — not backscatter rasters) |

## Known pre-existing red gates on master (not caused by this track)

Recorded by SAR 12.0 and reproduced here on the same master code:
`test_algorithm_meta_drift` (`expectedCatalog.size() == 43`, actual 53) and 5
cases of `test_capability_drift`. This track touches no recipes, capability
sidecars or agent code, so it can neither fix nor worsen them; they are
excluded from this track's gate set with evidence.
