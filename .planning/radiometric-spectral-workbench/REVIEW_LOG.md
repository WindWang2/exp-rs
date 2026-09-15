# D13 · Review Log (Phase 3, two-axis)

Status: IN PROGRESS (build wave 1 still compiling; reviewers dispatched).

| Axis | Reviewer | Scope | Status | P0 | P1 | P2 |
|------|----------|-------|--------|----|----|----|
| Spec (physics) | read-only subagent #1 | 6S LUT, calibration, indices, continuum, FCLS + test truths | running | – | – | – |
| Standards (C++20/Qt6) | read-only subagent #2 | radiometric_state, spectral_library, exp_gui widgets, agent tools, tests | running | – | – | – |
| Integration (self) | main agent | CMake registration, target graph, NoData contract consistency | done below | 0 | 0 | 1 |

## Integration self-review (main agent)

- All 12 CMake-referenced new files exist; no duplicate test-target
  registrations (the duplicate `test_spectral_profile_widget` target created
  mid-session was caught and removed before building; the existing master
  target was extended instead).
- P2 (carried): `SpectralInspectTool::readSpectrum` writes a constant into an
  unused out-param (`noDataSentinel`) — harmless but noisy; candidate for the
  Phase 3 cleanup pass if reviewers do not flag stronger issues.

## Findings from dispatched reviewers

(appended when they report)

## Findings from dispatched reviewers (Phase 3, both read-only)

### Standards axis (C++20/Qt6) — verdict FAIL → all P0/P1 fixed

| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| S-P0-1 | P0 | `SpectralInspectTool` passed `std::vector<double>` to the float-typed `matchSpectrum` — compile break | FIXED: explicit float down-conversion at the call site |
| S-P1-2/3 | P1 | unguarded jsoncpp `asDouble`/`asBool` on agent input could throw out of `execute()` | FIXED: `isNumeric()`/`isBool()` guards → `INVALID_PARAMETER` |
| S-P1-4 | P1 | D13 agent tools never registered — dead seam | FIXED: registered in `SpatialToolRegistry::registerBuiltinTools()` (drift test only pins cartography:/workflow:/style:/template:/solution: prefixes — safe) |
| S-P1-5 | P1 | `resampleToSensor` left partial state in the out-param on mid-loop failure | FIXED: staged local library, swap on full success |
| S-P1-6 | P1 | async `setProfile` mutated state before the in-flight guard → dropped newest request + stale landing mislabeled with new layer name | FIXED: guard moved above all mutation; layerName captured into the worker payload and landed from the sample, never re-read from the widget |
| S-P1-7 | P1 | inspect test compared Float32 read-back with `==` and asserted an impossible confidence bound | FIXED: `Catch::Approx` + reviewer-derived confidence window (0.15–0.35) |
| S-P2-8..13 | P2 | unsorted wavelength grid accepted; dead `noDataSentinel` out-param; nodata sentinel applied without `hasNoData`; dead `m_updatingCombos`; fixture-order dependence in D13 widget tests; NaN pass-through in inspect output | FIXED: sorted-grid validation in `isConsistentEntry`; dead param removed; `hasNoData` honored; dead flag removed; `QgisFixture fixture;` added to every D13 widget TEST_CASE; non-finite pixels fail the read. Kept by design: 30 ms paint budget (mission contract); agent-supplied paths opened read-only (same idiom as built-in tools) |

### Spec axis (physics/math) — verdict FAIL → all P0/P1 fixed

| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| P-P0-1 | P0 | aerosol HG phase evaluated with inverted scattering-angle convention (overhead sun + nadir = 180° backscatter evaluated as forward lobe, ~30–180× error) | FIXED: phase functions evaluated at −(μsμv + sinθs·sinθv·cosΔφ) |
| P-P1-2 | P1 | single-scattering airmass factor mixed exact prefactor with bare τ (low by 1/μs+1/μv) | FIXED: thin-limit exact form ω₀·τ·P/(4μsμv) |
| P-P1-3 | P1 | Leckner depolarization correction exponents inverted (λ⁺² instead of λ⁻²) | FIXED: (1 + 0.0113·λ⁻² + 0.00013·λ⁻⁴) |
| P-P1-4 | P1 | asymmetry implemented as (λ0−λleft)/(λright−λleft) ≡ 0.5 for symmetric troughs, contradicting the header formula (≈1.0) and its own Gaussian test | FIXED: code now implements (λ0−λleft)/(λright−λ0) |
| P-P1-5 | P1 | EVI NoData test never injected the sentinel (sentinel was at an unread index) → false-truth failure | FIXED: sentinel at index 0, count 1 |
| P-P2 | P2 | NaN wavelength UB in uniform axis locate; header comment arithmetic; Rayleigh phase normalization ~4%; Planck mission truth 302.79274 vs exact 302.79469 (tolerance covers both); VCA float index storage; truncated-trough FWHM extrapolation; NaN→NoData policy divergence across seams | NaN guard added to `locateUniform`; comment fixed; VCA now fails closed on collapsed vertex direction. Accepted & documented: Rayleigh normalization constant (approximation), mission Planck number inside 1e-5 tolerance, float PPI indices (exact to 2²⁴ pixels), NaN→NoData sentinel mapping (mission's NoData-passthrough rule) |

Both reviewers re-verified independently: 6S closed-form inversion algebra, DOS2 hand truth, OLI TOA arithmetic, all six index formulas, FWHM/area Gaussian truths, FCLS mixture algebra (y ≡ Σfᵢmᵢ verified band-wise), VCA mechanics, hull pop condition and segment-advance indexing — all CORRECT.

Final status after fixes: **P0 = 0, P1 = 0** (pending green rebuild + test run as evidence).

## External verification pass (independent runner, 15:10)

Ran the freshly-linked D13 test binaries (binaries newer than sources; results reflect current tree).

| Target | Result | Root cause (diagnosed) |
|--------|--------|------------------------|
| test_radiometric_state | PASS 87/87 | — |
| test_radiometric_calibration | PASS 195712 | — |
| test_fast_6s_atmospheric | PASS 1186 | — |
| test_continuum_removal | PASS 63 | — |
| test_spectral_indices | PASS 75 | — |
| test_spectral_library | 4 FAIL | `addEntry` requires spectrum==wavelengths==fwhm sizes (S-P2-8 grid check); D13 SAM tests add gridless entries via `makeEntry` (no wavelengths/fwhm) → `isConsistentEntry` false. Relax: gridless (both empty or wavelengths empty) OK for SAM; validate strictly-increasing only when a grid is present. Mirrors the spec's zero-norm-scored-at-π/2 requirement. |
| test_spectral_agent_tools | 5 FAIL | `execute` returns success=false / `NO_DATA` where tests expect success or `MISSING_WAVELENGTH_METADATA`. Layers are registered in `QgsProject::instance()` by `registerProjectLayer`; the read path fails before the wavelength check. Debug the layer lookup + point mapping against the fixture geotransform. |
| test_spectral_unmixing_fcls | 1 FAIL (VCA) | Algorithm–test inconsistency, not a threshold bug: for a noiseless linear-mixture cube the data spans exactly p−1=2 affine dims. Scoring "farthest in complement of span{mean-relative found directions}" spans the whole triangle plane after 2 picks → residual ≡ 0 → fail-closed. Either (a) score distance from the affine hull of found endmembers (project out span{e_i − e_0}; k=0 → ||x − mean||), which recovers the 3rd vertex deterministically, or (b) re-spec the test (noise/extra pixel). (a) keeps the stated VCA intent ("已有端元张成子空间的正交补") under its affine reading. |
| test_spectral_profile_widget | 4 FAIL | a) crosshair `featureSelected` not emitted (or out-of-range) on synthetic mouse move; b) `pointSpy.count()==1` right after `setProfile` — async landing timing; c) `currentValues()[0]≈0.05` after async sample; d) `mapSpy.count()==1` after `setRgbMapping` with no layer. Align widget signal/async contracts with these D13 cases. |
| test_d13_radiometric_spectral_e2e | not linked yet | Build graph had stale/missing .o (concurrent ninja episodes); full rebuild (build12) in flight. |

Also note two earlier link failures (test_spectral_library/agent_tools missing `sicnu_core`/`sicnu_agent` symbols) — already fixed in tests/CMakeLists.txt.
