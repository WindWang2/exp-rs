# PARALLEL_OWNERSHIP — F14 radiometric-physics-11

Snapshot at track start (2026-09-15), `origin/master` = `a5b11b7f10`.

## Open PRs

### PR #1008 `zcode/radiometric-spectral-workbench` — "Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench"

45 changed files. Ownership classification for this track:

| PR #1008 file(s) | Overlap with this track | Policy for this track |
|---|---|---|
| `src/core/radiometric_state.{h,cpp}`, `docs/adr/0158-radiometric-physics-state-system.md` | Package A (unit FSM, layer preflight, ADR 0158) | **READ-ONLY / DO NOT DUPLICATE.** This track does not build a second unit enum/FSM. Package A here = conversion-legality + required-input binding + provenance over master's existing `SICNU_RADIOMETRIC_STATE` string vocabulary (`satellite_products.h`), which PR #1008 does not provide. |
| `src/processing/algorithms/radiometric_calibration.{h,cpp}` (append-only additions: `exp_radiometric::RadiometricCalibrator`, ESUN+d² TOA buffer kernels) | Primary write scope conflict | **DO NOT EDIT these files in this track.** All new modules live in NEW files (`solar_geometry.*`, `radiometric_transition.*`, `atmospheric_provider.*`, `brdf_normalization.*`, `radiometric_qa.*`). Zero file-level conflict with PR #1008 on this pair. |
| `src/analysis/atmospheric/fast_6s_lookup.{h,cpp}`, `tests/test_fast_6s_atmospheric.cpp` | Concrete 6S-class LUT implementation | **READ-ONLY.** This track builds the *provider seam interface* (Package C) that a LUT like this could implement later; no 6S implementation of our own. |
| `tests/test_radiometric_state.cpp`, `tests/test_radiometric_calibration.cpp`, `tests/CMakeLists.txt` | test target names | This track uses **distinct test target names** (`test_solar_geometry`, `test_radiometric_transition`, `test_atmospheric_provider`, `test_brdf_normalization`, `test_radiometric_qa`). `tests/CMakeLists.txt` conflict on rebase is possible → resolve append-only. |
| `src/agent/spatial_tools/spectral_spatial_tools.*`, `src/agent/CMakeLists.txt`, `src/analysis/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/app/*` widgets | Spectral workbench (not this track's scope) | Untouched. |
| `docs/adr/0158-*` | ADR numbering | This track references ADR 0114 (master) for state vocabulary; its own decisions recorded in `.planning/.../DECISIONS.md`. If a new ADR is needed, next free number is checked at PR time (0159+), but planning-level DECISIONS.md is the primary record to avoid racing PR #1008's ADR 0158. |

### Deduplication risk register

1. **Unit FSM duplication risk (Package A)** — mitigated: no new unit enum; this track's
   `RadiometricTransition` authority consumes `SatelliteProducts::kRadiometricState*` string
   constants (master-stable since ADR 0114) and models *legal transitions with required inputs*,
   which neither master nor PR #1008 models.
2. **6S duplication risk (Package C)** — mitigated: no atmospheric RT implementation in this
   track; only the seam + registry + built-in DOS/QUAC adapters over existing
   `AtmosphericCorrection` functions.
3. **TOA-via-ESUN duplication risk (Package B)** — PR #1008 *consumes* `earthSunDistAu` as an
   input parameter; it does not compute sun position/distance. This track provides the
   *computation* (declination, EoT, azimuth/elevation, earth-sun distance from date/time). If
   both merge, PR #1008's `SensorCalibrationParams` fields can be populated from this module by a
   trivial follow-up; no API conflict.
4. **Test CMake collisions** — both tracks append to `tests/CMakeLists.txt`; rebase conflict is
   mechanical (append-only both sides).

## Open issues at start

#1001–#1007 (all other tracks' R2 residuals; none in this track's domain) → **no dedupe work
required**, none implemented here. `ISSUES.md` items re-checked against current code: all target
temporal/SAR/hyperspectral/cartography operators outside this track's primary scope.

## This track's write scope (final)

Primary (new files only):
- `src/processing/algorithms/solar_geometry.{h,cpp}` (Package B)
- `src/processing/algorithms/radiometric_transition.{h,cpp}` (Package A)
- `src/processing/algorithms/atmospheric_provider.{h,cpp}` (Package C)
- `src/processing/algorithms/brdf_normalization.{h,cpp}` (Package E)
- `src/processing/algorithms/radiometric_qa.{h,cpp}` (Package F)
- `tests/test_solar_geometry.cpp`, `tests/test_radiometric_transition.cpp`,
  `tests/test_atmospheric_provider.cpp`, `tests/test_brdf_normalization.cpp`,
  `tests/test_radiometric_qa.cpp` (Package H)
- `docs/processing/radiometric-physics-11.md` (docs), CHANGELOG entry

Shared integration files (minimal append-only):
- `src/processing/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/operators/rs/rs_operators_init.cpp`
- `src/operators/CMakeLists.txt` (if needed for new operator files)
- `src/processing/algorithm_help_catalog.cpp` (help entries)
- `CHANGELOG.md`, `.gitignore` (one whitelist block, already added)

New operator files (Package G): `src/operators/rs/rs_solar_geometry_operator.{h,cpp}`,
`src/operators/rs/rs_brdf_normalization_operator.{h,cpp}`, `src/operators/rs/rs_radiometric_qa_operator.{h,cpp}`
— subject to confirmation during Phase 4 (operator surface only where it earns its place; the
module seam + contracts may suffice for provider/transition authorities, matching how master
treats pure-seam modules).
