# OWNERSHIP — flash-sar-radiometry-13 (Track E)

## Owned (may be modified)

- `src/processing/algorithms/sar/sar_metadata.{h,cpp}` — state vocabulary,
  derived tokens, LUT key + parser.
- `src/processing/algorithms/sar/sar_calibration.{h,cpp}` — LUT-aware DN
  calibration kernel.
- `src/processing/algorithms/sar/sar_speckle.cpp` — derived-token propagation.
- `src/operators/rs/rs_sar_terrain_flatten_operator.cpp`,
  `rs_sar_terrain_correction_operator.cpp` — input-state verification.
- `src/operators/rs/rs_sar_geocode_operator.cpp` — input-state verification +
  complete state write + per-band states.
- `src/operators/rs/rs_sar_ratio_operator.cpp`,
  `rs_sar_texture_operator.cpp` — derived-state declaration.
- `src/operators/rs/rs_sar_calibrate_operator.cpp` — LUT param + refusals.
- `src/operators/rs/rs_sar_calibrate_operator.h` (schema param).
- `docs/processing/sar-domain.md` — §16.
- `tests/test_sar_radiometric_state.cpp` (new) + `tests/CMakeLists.txt`
  (append-only entry, main-agent integration only).
- `.planning/flash-sar-radiometry-13/`, `.goal-loop-ledger.md`.

## Forbidden (parallel domains)

- `src/operators/rs/rs_temporal_sar_fusion_operator.*` and its capability
  sidecar (open PR #1135).
- `data/labs/*` (open PR #1136), `data/processing/algorithm_meta/*`,
  `data/agent/capabilities/*`, `data/help/*` (drift gates already red on
  master; this track must not perturb them).
- `src/processing/algorithms/temporal/*` (temporal PR domain).
- InSAR algorithm kernels beyond state-metadata defects (none planned).
- `src/core/radiometric_state.*` (optical FSM, ADR 0158) — read-only reference;
  the SAR vocabulary is additive and lives in `sar_metadata.h`.

## Shared conflict hotspots

- `tests/CMakeLists.txt`: append-only tail convention; the three open PRs also
  append there — expected textual conflict only.
- `docs/processing/sar-domain.md`: only SAR 12.0 §15 precedes this track's §16.
