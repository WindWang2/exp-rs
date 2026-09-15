## D13 · Radiometric Spectral Workbench

Closes the three Day-13 gaps end to end: a typed radiometric-state FSM that blocks
unlawful unit transitions at preflight, physics-based atmospheric correction (fast 6S
LUT + closed-form BOA inversion + DOS2) with hyperspectral continuum removal and
fully-constrained spectral unmixing, and the interactive/agent layer (Qt6 spectral
profile workbench, RGB palette, and two physics-audit agent tools wired into the
spatial tool registry). closes #986 if the tracker issue exists.

### What lands

- **A — Radiometric state FSM** (`src/core/radiometric_state`, ADR 0158): five-state
  unit system (`DigitalNumber → Radiance → TOA/BOA/BT`) with fail-closed preflight
  exceptions and custom-property/GDAL metadata round-trip.
- **B — Calibration & 6S LUT**: typed `exp_radiometric::RadiometricCalibrator` kernels
  (Planck inverse vs USGS handbook, OLI coefficient & ESUN TOA paths) and
  `Fast6sLookup` (deterministic offline 6-D coefficient grid, multi-linear
  interpolation, closed-form `invertBoaReflectance`, Chavez DOS2).
- **C — Index kernels**: `exp_spectral::SpectralIndices` (NDVI/EVI/SAVI/MNDWI/NDBI/EVI2)
  with NaN-on-degenerate denominators, NoData passthrough and ×10⁴ scale guard.
- **D — Continuum removal**: Andrew-monotone-chain upper hull, Rc ∈ (0,1], absorption
  feature parameterization (depth, interpolated FWHM, trapezoid area, asymmetry).
- **E — Unmixing**: `exp_spectral` FCLS seam (penalty-augmented Lawson–Hanson NNLS,
  measured sum-to-one QA metric), PPI delegation + new VCA endmember extraction.
- **F — Spectral library** (`src/core/spectral_library`): JSON persistence, SAM
  retrieval (zero-norm → π/2 worst-match, never NaN), Gaussian-SRF sensor resampling
  with ±3σ bisected windows and constant-spectrum invariance.
- **G — Qt6 workbench**: async `exp_gui::SpectralProfileWidget` (worker-side GDAL
  sampling, queued landing, QPointer lifecycle guard, crosshair, ≤30 ms/1000-band
  paint) + `BandCompositePalette` with clamped band mapping.
- **H — Agent tools**: `spatial:spectral_inspect` and `spatial:validate_boa_physics`
  (range/water-inversion/vegetation-ratio audits, wavelength-driven band selection,
  structured error codes + self-healing suggestions), registered as built-ins.
- **I — E2E + grading**: full DN→calibration→6S→indices→continuum→FCLS→library chain
  with independent closed-form truths at every stage; Lab02/Lab08 100-point rubrics
  computed from artifacts (reference = 100; uncalibrated DN injection = exactly 70 via
  the state-preflight penalty with an actionable directive).

### Verification (local, offline; no remote CI)

- `ninja -j2` build of `qgis_core/qgis_gui/sicnu_core/sicnu_spectral_analysis/
  sicnu_processing/sicnu_agent` + all 12 touched/new test targets: GREEN.
- All 11 D13-created or D13-touched suites exit 0 (6 new + 5 legacy regression):
  `test_radiometric_state`, `test_fast_6s_atmospheric`, `test_continuum_removal`,
  `test_spectral_unmixing_fcls`, `test_spectral_agent_tools`,
  `test_d13_radiometric_spectral_e2e`, `test_radiometric_calibration`,
  `test_spectral_indices`, `test_spectral_library`, `test_spectral_unmixing`,
  `test_spectral_profile_widget`.
- Registry spot-checks (`test_capability_drift`, `test_capability_knowledge`) show a
  failure profile byte-identical to `origin/master` at the base commit (pre-existing
  capability-data duplicates and catalog skew; reproduced with D13 sources stashed).

### Two-axis review (2 read-only subagents, ≤3 cap honored)

- Physics axis: caught an inverted scattering-phase convention and airmass-factor and
  Leckner-exponent errors in the 6S parameterization (fixed), plus an asymmetry
  formula/contract mismatch (fixed) and a false-truth NoData test (fixed).
- Engineering axis: caught a compile-breaking `vector<double>`→float-API call, three
  jsoncpp throw paths, missing tool registration, partial-state resample failure, and
  an async request-drop/mislabel race (all fixed).
- Final: **P0 = 0, P1 = 0**. Full log: `.planning/radiometric-spectral-workbench/REVIEW_LOG.md`.

### Planning artifacts

`docs/adr/0158-radiometric-physics-state-system.md` and
`.planning/radiometric-spectral-workbench/` (GOAL / PLAN / DECISIONS / BASELINE /
REVIEW_LOG / EVIDENCE / monitor samples).
