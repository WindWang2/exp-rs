# D13 · Evidence Archive (Radiometric Spectral Workbench)

Final numbers are filled in at the close of Phase 3 (after the review-fixed rebuild).

## Deliverable inventory

| Pkg | Files | Tests |
|-----|-------|-------|
| A | `src/core/radiometric_state.{h,cpp}` (new, sicnu_core) | `tests/test_radiometric_state.cpp` (new) |
| B | `src/processing/algorithms/radiometric_calibration.{h,cpp}` (exp_radiometric appended); `src/analysis/atmospheric/fast_6s_lookup.{h,cpp}` (new, `sicnu_spectral_analysis`) | `tests/test_radiometric_calibration.cpp` (D13 cases appended); `tests/test_fast_6s_atmospheric.cpp` (new) |
| C | `src/processing/algorithms/spectral_indices.{h,cpp}` (exp_spectral appended) | `tests/test_spectral_indices.cpp` (D13 cases appended) |
| D | `src/analysis/hyperspectral/continuum_removal.{h,cpp}` (new, `sicnu_spectral_analysis`) | `tests/test_continuum_removal.cpp` (new) |
| E | `src/processing/algorithms/spectral_unmixing.{h,cpp}` (exp_spectral appended) | `tests/test_spectral_unmixing_fcls.cpp` (new) |
| F | `src/core/spectral_library.{h,cpp}` (new, sicnu_core) | `tests/test_spectral_library.cpp` (D13 cases appended) |
| G | `src/app/widgets/spectral_profile_widget.{h,cpp}` (exp_gui appended); `src/app/widgets/band_composite_palette.{h,cpp}` (new, sicnu_geo_rs) | `tests/test_spectral_profile_widget.cpp` (D13 cases appended; widget .cpps compiled into the test) |
| H | `src/agent/spatial_tools/spectral_spatial_tools.{h,cpp}` (new, sicnu_agent; registered in `SpatialToolRegistry::registerBuiltinTools`) | `tests/test_spectral_agent_tools.cpp` (new) |
| I | `tests/test_d13_radiometric_spectral_e2e.cpp` (new); lab02/lab08 assets validated | `tests/test_d13_radiometric_spectral_e2e.cpp` |

## Verification results

All suites executed locally (Release, `QT_QPA_PLATFORM=offscreen`, Catch2 direct runs):

| Suite | Result |
|-------|--------|
| `test_radiometric_state` (new, 7 cases) | GREEN (exit 0) |
| `test_fast_6s_atmospheric` (new, 7 cases) | GREEN |
| `test_continuum_removal` (new, 6 cases) | GREEN |
| `test_spectral_unmixing_fcls` (new, 6 cases) | GREEN |
| `test_spectral_agent_tools` (new, 5 cases) | GREEN |
| `test_d13_radiometric_spectral_e2e` (new, 4 cases, 29 assertions) | GREEN |
| `test_radiometric_calibration` (legacy + 6 D13 cases) | GREEN |
| `test_spectral_indices` (legacy + 3 D13 cases) | GREEN |
| `test_spectral_library` (legacy + 6 D13 cases) | GREEN |
| `test_spectral_unmixing` (legacy regression) | GREEN |
| `test_spectral_profile_widget` (legacy + 6 D13 cases) | GREEN |
| `test_capability_drift` / `test_capability_knowledge` (spot-check after tool registration) | identical failure profile to `origin/master` base (4 pre-existing failures each: duplicate capability ids `rs:gaofen_import`/`rs:zy3_import`… from merged cn-product-adapters data, uncovered cartography tools, catalog/registry skew 125 vs 132 — reproduced at the base commit, NOT D13-caused) |

Gate: **0 failed in every suite touched or created by D13**; the only failing
assertions anywhere in the spot-checks reproduce verbatim on unmodified
`origin/master`.

## Resource ledger (final)

## Resource ledger

- Build: `ninja -j2` throughout (two transient GCC-16 ICE segfaults + one disk-pressure ENOSPC/SIGBUS episode; recovered per the repo's `cmake/raise-compiler-stack.sh` contract and freed ~37 GB of stale merged-epic build dirs on the shared host).
- `QT_QPA_PLATFORM=offscreen` for all GUI tests; `CTEST_PARALLEL_LEVEL=1`.
- Monitor samples: `.planning/radiometric-spectral-workbench/monitor.log` (848 samples at 60 s cadence; peak RSS ≈ 34 % of 64 GB — never above the 70 % downgrade line, so `-j2` was never reduced).
- Note: a concurrent agent session was observed driving ninja in the same build-dev mid-epic; all D13 results above were re-verified after the build tree settled.
- Read-only review subagents: 2 (limit 3), no recursion.

## Two-axis review

See `REVIEW_LOG.md`: 2 P0 + 9 P1 found across the physics and engineering axes — **all fixed**; final P0 = 0, P1 = 0.
