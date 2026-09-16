# D13 · Radiometric Spectral Workbench — Goal

**Track branch:** `zcode/radiometric-spectral-workbench` (worktree `../exp-rs-radiometric-spectral-workbench`, off `origin/master` @ `007e70cff6`)

## Mission

End-to-end radiometric & spectral science chain for exp-rs:

```
Raw DN ──(Pkg A FSM guard)──▶ Radiance/TOA ──(Pkg B 6S/DOS2)──▶ BOA reflectance
      ──(Pkg C indices)──▶ NDVI/EVI/MNDWI/… ──(Pkg D continuum)──▶ absorption features
      ──(Pkg E FCLS)──▶ abundance maps ──(Pkg F library SAM/SRF)──▶ material identification
      ──(Pkg G Qt6 workbench)──▶ interactive profile/palette ──(Pkg H agent tools)──▶ physics self-checks
      ──(Pkg I E2E + lab grading)──▶ teaching evaluation (Lab02 / Lab08, 100-point rubric)
```

Three gaps closed:

1. **Radiometric state contract missing** → typed FSM (`exp_radiometric::RadiometricState`) with
   preflight exceptions so uncalibrated DN can never silently reach atmospheric correction
   or vegetation-index operators.
2. **Physics-based atmospheric correction insufficient** → fast 6S LUT (trilinear) with
   closed-form BOA inversion + DOS2 baseline; hyperspectral continuum removal; FCLS unmixing
   with sum-to-one/non-negativity enforced.
3. **Interactive viewport & agent loop disconnected** → 60 FPS QPainter spectral profile with
   QPointer lifecycle guard, band composite palette, and `spatial:spectral_inspect` /
   `spatial:validate_boa_physics` agent tools.

## Hard constraints

- `master` root repo stays pristine; all work in the worktree.
- `subagents <= 3`, read-only review only, no recursive spawning.
- `ninja -j2` hard cap (downgrade `-j1` when RSS > 70%); `CTEST_PARALLEL_LEVEL=1`;
  `QT_QPA_PLATFORM=offscreen`.
- No remote CI dependency: local CMake + Catch2 only.
- TDD vertical slices (tracer bullets), no tautological assertions, black-box public seams only.

## Exit criteria

- `ctest -R "test_radiometric|test_spectral|test_fast_6s|test_continuum|test_d13" -j1` 100% green.
- All GUI tests run headless (`offscreen`), no crashes.
- Planning tree complete: ADR-0158, PLAN, DECISIONS, BASELINE, REVIEW_LOG, EVIDENCE.
- Two-axis review (Standards / Spec) P0 = 0, P1 = 0.
- PR opened: `feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench`.
