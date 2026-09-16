# BASELINE — F14 radiometric-physics-11

Audit timestamp: 2026-09-15 (start of track). All facts re-verified against `origin` at start; the
prompt-generation snapshot (origin/master = `ebcafb4d02`) was stale.

## Git / remote state at start

- `origin/master` = **`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`**
  (`fix: fail-closed fixes for review issues #994–#999 (#1000)`)
- Local master == origin/master (no divergence).
- Remote branches of interest: `origin/zcode/radiometric-spectral-workbench` (head of open PR #1008).
- Recent master history (top 10):
  - a5b11b7f10 fix: fail-closed fixes for review issues #994–#999 (#1000)
  - 1cea98921b Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)
  - c5d4aafe8e D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)
  - 77e178ac1e fix(ci): resolve macOS/Windows compile errors in d17 and Win32 paths (#993)
  - 08264801a0 docs(d19): record M6 hermetic scale/evolution/LeaveOne evidence
  - e57534384f test(d19): hermetic 100k catalog scale, version evolution, leakage Fail, LeaveOne*
  - ebcafb4d02 fix(ci): correct OSR WKT import in test_io_operators (#990)
  - b91753ffd1 Merge branch 'zcode/classification-change-studio' (#989)
  - f368b9fd78 Merge branch 'zcode/workflow-pipeline-designer' (#988)
  - 64418b720b Merge branch 'zcode/geometric-registration-workbench' (#987)

## Open PRs at start

- **PR #1008** `zcode/radiometric-spectral-workbench` — "feat(spectral): Day 13 radiometric
  calibration, 6S atmospheric correction & spectral workbench" (45 changed files, not draft,
  base master). **This is the dominant ownership fact for this track**; full overlap analysis in
  `PARALLEL_OWNERSHIP.md`.

## Open issues at start

All open issues #1001–#1007 are R2-review residuals of OTHER tracks (dataset QA CRS, workflow
synthetic-execute, georef GCP transform, dataset identity, joinFeatures null, registry artifact
fail-open, io:clip CRS override). **None concerns radiometric physics.** `ISSUES.md` on master is
the old D3 lab-content backlog (T-1..T-2, S-1..S-2, H-1..H-3, C-1..C-2); per GOAL it is treated as
a clue source only — none of its items fall in this track's primary scope (they are temporal /
SAR / hyperspectral / cartography operator gaps).

## Master capability audit (radiometric domain) — evidence

Existing on master (all verified by reading headers):

| Capability | Location | State |
|---|---|---|
| DN→Radiance/TOA/BT kernels + MTL/MTD/GDAL metadata parsing | `src/processing/algorithms/radiometric_calibration.{h,cpp}` (855 lines) | DONE — `RadiometricCalibration` namespace; fail-closed `hasSunElevation` |
| Radiometric state vocabulary + file metadata IO | `src/processing/algorithms/satellite_products.h:198-246` (`SICNU_RADIOMETRIC_STATE`, 5-state vocabulary, `SICNU_NUMERIC_SCALE`, set/readRadiometricState), ADR 0114 | DONE (vocabulary) |
| DOS1/DOS2 (Chavez, TOA-reflectance space) / QUAC | `src/processing/algorithms/atmospheric_correction.{h,cpp}` (820 lines) | DONE; histogram dark object; `dosReflectance` step |
| Terrain illumination correction (Cosine / C-correction / Minnaert, Horn slopes, self-shadow NaN) | `src/processing/algorithms/topographic_correction.{h,cpp}` + `rs_topographic_correction_operator.cpp` (475 lines) | DONE — Work package D largely pre-existing |
| QA masking kernels (Landsat QA_PIXEL bits, S2 SCL, generic bitmask) | `src/processing/algorithms/qa_mask.{h,cpp}` | DONE for masking; no radiometric QA *flags* vocabulary |
| Operators | `rs_radiometric_calibration_operator`, `rs_atmospheric_correction_operator` (+aliases), `rs_topographic_correction_operator`, SAR calibrate/backscatter/terrain | DONE for the above |
| **Solar-earth geometry computation** (declination, equation of time, sun position from acquisition time, earth-sun distance) | — | **ABSENT.** grep over `src/` finds no declination/EoT/hour-angle code (only unrelated QgsMagneticModel). `sunElevationDeg` is always read from metadata or defaulted |
| **BRDF / view-angle normalization** (kernels, anisotropy, view zenith) | — | **ABSENT.** grep finds no `viewZenith`/`BRDF`/`RossThick`/`LiSparse` code |
| **Conversion-legality authority binding transitions to required inputs + provenance chain** | — | **ABSENT.** Vocabulary exists (ADR 0114) but no authority decides "DN→TOA is satisfiable with these coefficients?" or emits a conversion provenance record |
| **Atmospheric provider seam** (optional LUT/6S-class provider interface, typed refusal) | — | **ABSENT.** `AtmosphericCorrection::Method` is a closed enum; no extension interface |
| **Radiometric QA flag vocabulary** (saturation / negative / over-range / invalid-metadata propagation) | — | **ABSENT** (QaMask only derives binary masks from QA bands) |

## Conclusion (drives PLAN)

Work packages A–H were re-scoped against the audit:

- **D (terrain illumination)**: pre-existing on master → this track only *consumes* it (solar
  geometry can feed it); no rework.
- **A (state machine)**: vocabulary exists; PR #1008 adds a layer-level FSM (`exp_radiometric`
  in `src/core/radiometric_state.*`) → this track builds the missing **conversion-legality +
  required-input binding + provenance record** authority on master's stable string vocabulary,
  NOT a second unit FSM.
- **B (solar-earth geometry)**: genuine gap, not covered by master or PR #1008 → implement.
- **C (atmospheric provider seam)**: master has closed-enum DOS/QUAC; PR #1008 adds a concrete
  6S lookup (unmerged) → this track builds the optional **provider interface + registry +
  typed-refusal**, keeping DOS/QUAC as built-ins; PR #1008's lookup could adapt onto it later.
- **E (BRDF seam)**: genuine gap → implement (kernel-driven Ross-Li + empirical pair
  normalization, angle-metadata preconditions).
- **F (uncertainty/QA)**: masking exists; **flag vocabulary + calibration-chain propagation**
  missing → implement.
- **G/H**: parity + known-answer tests for everything new.
