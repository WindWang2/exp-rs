# PLAN — F14 radiometric-physics-11

Baseline: `origin/master` = `a5b11b7f10`. Branch `zcode/radiometric-physics-11`, worktree
`../exp-rs-radiometric-physics-11`.

## Scope (post-audit)

Pre-existing on master and NOT re-implemented: DN→radiance/TOA/BT calibration,
SICNU_RADIOMETRIC_STATE vocabulary (ADR 0114), DOS1/DOS2/QUAC, topographic correction
(Cosine/CC/Minnaert), QA masking kernels, their operators.

Open PR #1008 (spectral workbench) owns a layer-level unit FSM + typed calibrator + concrete
6S lookup → NOT duplicated; see PARALLEL_OWNERSHIP.md.

## Deliverables (vertical slices, each with known-answer + negative tests before moving on)

1. **`SolarGeometry` (Package B)** — new module:
   - Day-of-year → Earth-Sun distance (AU) via the Astronomical Almanac / Spencer series.
   - Solar declination + equation of time (Spencer 1971 Fourier series).
   - Solar zenith / azimuth / elevation from lat/lon/date/UTC-time (NOAA solar calculator
     closed form); hour angle from true solar time.
   - Typed refusal for invalid inputs (lat/lon out of range, nonexistent dates, sun below
     horizon where caller requires a day-lit scene).
   - Scene-level seam (scalar angles from acquisition metadata) + optional per-pixel seam left
     as inputs to kernels (per-pixel rasters feed BRDF/topographic kernels; no new raster
     framework).
2. **`RadiometricTransition` (Package A)** — new authority over master's string vocabulary:
   - Closed transition table: digital_number→radiance→toa_reflectance→surface_reflectance and
     →brightness_temperature branch; identity lawful; inversions/shortcuts unlawful.
   - Per-edge **required-input predicates** bound to `RadiometricCalibration::CalibrationMetadata`
     fields + atmospheric provider availability (e.g. DN→TOA needs reflectance coefficients or
     radiance+ESUN+geometry; TOA→surface needs a provider).
   - `TransitionPlan`: satisfiability check producing missing-metadata reasons (typed refusal)
     + a **provenance record** (JSON: chain of steps, coefficients used, unit/scale before/after)
     written into operator results / dataset metadata.
3. **`AtmosphericCorrectionProvider` (Package C)** — new seam:
   - Abstract provider interface: capability descriptor (required auxiliary inputs: sun/view
     angles, AOD, water vapour, target elevation), availability, per-band correction
     (TOA reflectance + geometry → surface reflectance) returning typed errors.
   - Registry (in-process, no plugin framework); built-in adapters wrap existing DOS1/DOS2/QUAC
     so the chain `DN→TOA→surface` runs with zero new dependencies.
   - Requesting an unregistered provider (e.g. "6s") → typed refusal naming the missing
     provider and the fallbacks (fail-closed, never silently degrade to DOS).
4. **`BrdfNormalization` (Package E)** — new module:
   - Ross-Thick + Li-Sparse-Reciprocal kernel closed forms; anisotropy factor
     f(θs,θv,Δφ)/f(θ₀,θv₀,Δφ₀); normalize to nadir-view/reference geometry.
   - Requires per-band sun/view zenith/azimuth (scene scalars or per-pixel seam inputs);
     typed refusal when angle metadata is missing/degenerate (sun below horizon, view zenith
     out of [0,90)).
   - Empirical c-factor pair normalization for two-date same-sensor pairs (explicit pair of
     clear samples; refuses without valid pair statistics).
   - Kernel value unit tests use hand-computed published-formula values (independent oracle).
5. **`RadiometricQa` (Package F)** — new module:
   - Per-pixel QA flag bitset vocabulary: saturation, negative reflectance, reflectance > 1,
     NaN/no-data, cloud/shadow/snow (propagated from QaMask inputs), invalid-geometry.
   - `evaluate` over calibrated reflectance buffers producing a uint16 flag band + counts
     summary; flag bits are stable named constants (documented, tested).
   - Propagation helper: combine per-step flags through the calibration chain (union of
     anomalies with step provenance).
6. **Operators (Package G)** — `rs:solar_geometry` (scene/per-scene angle computation with
   provenance output + optional per-pixel geometry rasters), `rs:brdf_normalization`,
   `rs:radiometric_qa`; registered append-only in `rs_operators_init.cpp`; schema/result/help
   catalog parity; SICNU_RADIOMETRIC_STATE parity: BRDF/QA outputs inherit the input state
   (they don't change units) and record it.
7. **Docs** — `docs/processing/radiometric-physics-11.md` (domain doc: state chain, formulas
   with citations, provider seam contract, QA vocabulary) + CHANGELOG entry.
8. **Tests (Package H)** — one executable per module with independent closed-form oracles
   (formulas re-implemented in the test from the cited publications, never by calling the
   production function) + negative tests (missing sun elevation, missing coefficients,
   unknown provider, degenerate geometry, NaN propagation).

## Execution order

Phase 1: solar_geometry (+tests). Phase 2: radiometric_transition + atmospheric_provider
(+tests). Phase 3: brdf_normalization + radiometric_qa (+tests). Phase 4: operators/registry/
help/docs wiring. Phase 5: targeted suites, hardening. Phase 6: full-diff self-review +
independent review, remediation. Phase 7: rebase, double-run gates, push, PR.

## Resource envelope

Configure with `CMakePresets.json` `build-dev` preset; build `-j2` (`-j1` under pressure),
tests `-j1`, `QT_QPA_PLATFORM=offscreen`. New files are self-contained (Qt core + GDAL only via
existing wrappers where needed; kernels are plain C++ over float buffers to stay testable).
