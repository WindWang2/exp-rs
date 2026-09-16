# D13 · Decisions Log

## D-01 New `exp_*` namespace seams coexist with legacy kernels (no breaking changes)

Master already ships `RadiometricCalibration`, `SpectralIndices`, `SpectralUnmixing`,
`AtmosphericCorrection`, `SpectralLibrary` (global namespaces) and a global
`SpectralProfileWidget` widget. The D13 contract specifies `exp_radiometric` /
`exp_spectral` / `exp_gui` / `exp_agent` seams. **Decision:** add the namespaced seams
exactly as contracted, appended to the existing owner files; legacy APIs stay untouched
so all 539 existing tests remain authoritative. Where a legacy kernel already implements
the contracted math (e.g. `SpectralUnmixing::unmixFcls`, PPI), the `exp_spectral` seam
delegates to it instead of duplicating solvers; VCA is net-new code.

## D-02 6S LUT: analytic physical grid + trilinear interpolation (no external binary)

No offline 6S executable exists in this repo and remote fetches are forbidden. **Decision:**
`Fast6sLookup::interpolateCoefficients` evaluates a compact, deterministic
physics parameterization — Rayleigh scattering (Þórsson/EOSEL-style ρ_r ∝ P/λ^4 with
2π-phase-function correction) plus Ångström aerosol ρ_a = β·(550/λ)^α with α derived from
AOD550, aerosol spherical albedo S bounded via S ≈ s_max·(1−e^{−τ}), and
two-stream total transmittances T = exp(−(τ_R+τ_a)·M) with Kasten-Young airmass —
on a fixed 6-D grid (wavelength 350–2500 nm step 50; AOD550 ∈ {0.01,0.1,0.3,0.6,1.0,2.0};
water vapor ∈ {0.2,0.8,1.5,2.5,4.0,5.0}; θ_s ∈ {0,15,30,45,60,75}; θ_v ∈ {0,15,30,45,60};
Δφ ∈ {0,45,90,135,180}) and interpolates trilinearly. The grid exists so the public
contract ("precomputed LUT grid + trilinear") is genuinely exercised, while values stay
reproducible offline. Closure accuracy vs the direct analytic evaluation ≤ 1e-4 (asserted).

## D-03 FCLS: penalty-augmented Lawson–Hanson NNLS (ρ = 10⁶·mean(diag(EᵀE)))

Keeps one finite-terminating active-set NNLS for both constraints. Sum-to-one holds to
~1e-6 relative; `meanSumConstraintViolation` is reported as an explicit QA metric rather
than silently assumed. Rank-deficient/zero-norm endmember sets fail closed with named
errors before any pixel is processed. Matches the proven master kernel (ADR-consistent);
the exp_spectral seam adds the QA metric + `extractEndmembers` (PPI delegate + new VCA).

## D-04 Radiometric state storage: QgsMapLayer custom property + GDAL metadata, DN default

`setLayerRadiometricState` writes `SICNU_RADIOMETRIC_STATE` as a `QgsMapLayer` custom
property (authoritative, in-session) and best-effort `GDALSetMetadataItem` on the
underlying dataset (persisted). `validateBandPreflight` reads custom property first,
then GDAL metadata; missing marker ⇒ `DigitalNumber` (fail-safe: rawest interpretation).
Identity transitions are lawful no-ops. Forbidden edges (DN→BOA, DN→BT, TOA→BT, any
backwards inversion) throw `RadiometricStateMismatchException` — fail closed, no silent
science.

## D-05 Zero-norm spectra match at θ = π/2 (not NaN)

`SpectralLibrary::matchSpectrum` maps zero-norm query/reference vectors to the maximum
angle π/2: deterministic, sortable, and conservatively "worst match". NaN would poison
top-K ordering comparisons.

## D-06 Widgets: namespaced duplicates, not invasive rewrites of the global widgets

`exp_gui::SpectralProfileWidget` is a new QWidget in the existing owner file with the
contracted API (`currentValues()`, crosshair `mouseMoveEvent`, `featureSelected`,
`QPointer` auto-clear). The legacy global `SpectralProfileWidget` keeps its exact behavior
for its existing call sites. `BandCompositePalette` is net-new. Widget tests compile the
widget .cpp files directly into the test executable (`test_histogram_widget` pattern,
AUTOMOC ON, `QT_QPA_PLATFORM=offscreen`) — no link against the `sicnu_geo_rs` executable.

## D-07 Agent tools resolve layers through the project layer tree by id

`spatial:spectral_inspect` / `spatial:validate_boa_physics` accept `layer_id` and resolve
via `QgsProject::instance()->mapLayer(id)` (same resolution idiom as the built-in
spatial tools). Synthetic fixtures in tests register in-memory `QgsRasterLayer`s with the
project, so no file I/O and no GDAL fixture files are needed. Physics audit rules are
wavelength-driven (NIR/Red chosen by band metadata), never hard-coded band ordinals;
when wavelengths are absent the tool reports `MISSING_WAVELENGTH_METADATA` instead of
guessing.

## D-08 Lab rubric stays data-driven

Lab02/Lab08 JSON extend the existing Labspec + grading schema (ADR 0146/0150); new checks
reuse the established check kinds. No new grading engine code unless a check kind is
genuinely missing (then it is added next to the existing ones and covered by the E2E).

## D-09 (added during Phase 2) Lab rubric grading lives in the D13 E2E

The mission's Lab02/Lab08 100-point rubrics (20/30/30/20 and 40/40/20) are
computed by `test_d13_radiometric_spectral_e2e.cpp` directly from the D13
kernel outputs, so the grading loop is executable evidence rather than prose.
The shipped lab JSONs are validated for consistency (ids, NDVI band params,
step tables) instead of extended with check kinds the data-driven engine does
not ship. Uncalibrated-DN contract: the grader applies a flat −30 state-
preflight penalty, subsuming the 20-point range deduction (one root cause),
landing at exactly 70 with an improvement directive naming
`rs:radiometric_calibration`.
