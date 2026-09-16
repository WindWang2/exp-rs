# DECISIONS — F14 radiometric-physics-11

Each entry: context → candidates → decision → reason. Autonomy=full: no user questions.

## D1 — Package A shape: transition authority, not a second unit FSM

- Context: master has the `SICNU_RADIOMETRIC_STATE` string vocabulary (ADR 0114,
  `satellite_products.h`) but no conversion-legality authority. Open PR #1008 adds a
  layer-level unit FSM (`exp_radiometric::RadiometricState`, `src/core/radiometric_state.*`,
  ADR 0158) — unmerged, read-only for this track.
- Candidates: (a) duplicate an enum FSM under a new name; (b) build a transition authority over
  master's existing string vocabulary that binds each legal transition to its required inputs
  (coefficients, geometry, provider) and emits a provenance record.
- Decision: **(b)**. No new unit vocabulary; `RadiometricTransition` consumes
  `SatelliteProducts::kRadiometricState*` constants.
- Reason: avoids "same functionality renamed" violation vs PR #1008; the required-input binding
  + provenance chain is the genuinely missing capability and is exactly the mission's "转换合法
  性与 provenance". If both merge, PR #1008's FSM can later delegate legality to this authority.

## D2 — Package B: NOAA/Spencer closed forms, no new dependency

- Context: no solar geometry exists on master or PR #1008 (verified by grep — PR #1008 consumes
  `earthSunDistAu`/`sunElevationDeg` as inputs, never computes them).
- Candidates: (a) NOAA Solar Calculator closed-form set (Spencer 1971 Fourier series for
  declination + equation of time, earth-sun distance series) in-tree; (b) add a dependency
  (libnova/VSOP87).
- Decision: **(a)**, accuracy ~0.01° for declination/position — sufficient for TOA/BRDF
  normalization, no new dependency, fully offline.
- Reason: GOAL forbids heavyweight new deps; published closed forms have testable known answers.

## D3 — Package C: interface + registry + built-in adapters, no plugin framework

- Candidates: (a) abstract `SurfaceReflectanceProvider` + in-process registry + DOS/QUAC
  adapters; (b) QGIS processing provider plugin; (c) do nothing until PR #1008 merges.
- Decision: **(a)**. Requesting an unregistered provider id is a typed refusal (never a silent
  DOS fallback); DOS/QUAC stay available under their own ids.
- Reason: satisfies "为 LUT/外部 6S 类 provider 建立可选接口，无 provider typed-refuse" without
  duplicating PR #1008's concrete 6S LUT; the seam is exactly what that LUT can implement later.

## D4 — Package E: Ross-Li (RossThick + LiSparseR) forward kernels + anisotropy normalization

- Candidates: (a) kernel-driven BRDF (Ross-Li) normalization to reference geometry;
  (b) empirical multi-date rotation only; (c) both.
- Decision: **(c)** — kernel-driven as the primary path (requires per-band sun/view geometry;
  typed refusal without it) plus empirical c-factor two-date pair normalization as a secondary
  method with explicit validity conditions (same sensor/grid, clear-pair regression with
  ≥ minimum pairs and non-degenerate slope).
- Reason: kernel path is the standard MODIS-style operational approach with closed-form
  known answers; empirical path covers no-angle-metadata scenes without pretending angles exist.

## D5 — No edits to PR #1008 files; all new code in new files

- `radiometric_calibration.{h,cpp}`, `radiometric_state.*`, `fast_6s_lookup.*` and other PR
  #1008 files are untouched (see PARALLEL_OWNERSHIP.md). Integration into shared CMake/registry
  files is append-only.

## D6 — Test executables: one per module, independent oracles

- Each new test target re-implements the cited formula independently inside the test (different
  derivation path where possible, e.g. trig identities, published reference values) — never by
  calling the production function. Negative tests assert typed refusal, not error strings.

## D7 — Operators added only where a user-facing surface earns its keep

- `rs:solar_geometry`, `rs:brdf_normalization`, `rs:radiometric_qa` (streaming-file operators,
  house pattern). `RadiometricTransition` and the provider registry are preflight/authority
  APIs consumed by operator preflight paths — exposed via results/provenance metadata rather
  than standalone file-to-file operators (they transform no pixels themselves).

## D8 — Angle metadata keys

- New dataset metadata keys follow the house `SICNU_*` prefix: `SICNU_SUN_ZENITH`,
  `SICNU_SUN_AZIMUTH`, `SICNU_VIEW_ZENITH`, `SICNU_VIEW_AZIMUTH` (degrees), written by
  `rs:solar_geometry` and consumed/required by `rs:brdf_normalization` (missing → typed
  refusal). Distinct from PR #1008's layer-property keys; additive to ADR 0114's two keys.
