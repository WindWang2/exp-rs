# EVIDENCE — temporal-phenology-timeline (D16) · final gate

Date: 2026-09-15 · Worktree: `/home/kevin/projects/rs-studio/exp-rs-temporal-phenology-timeline`
Branch: `zcode/temporal-phenology-timeline` · Post-review head: remediation commit (see git log).

## 1. Build discipline evidence

- Configure: `/usr/bin/cmake --preset dev-default` (+ offline FetchContent source
  overrides for pybind11/catch2) → `Generating done`, generator **Unix Makefiles**,
  `CMAKE_BUILD_TYPE=Debug`. (`~/.local/bin/cmake` on this host is a broken shim —
  all invocations used `/usr/bin/cmake`.)
- Every build: `cmake --build build-dev --target <targets> -j2` with
  `CMAKE_BUILD_PARALLEL_LEVEL=2`; tests `ctest -j1`; `QT_QPA_PLATFORM=offscreen`.
  No `-j$(nproc)` anywhere; no `qgis_core`/`sicnu_processing`/`sicnu_agent`
  target ever built in this worktree (D-160-1 lightweight-library decision).
- Remaining compile warnings in D16 translation units: none of substance
  (the recurring `cc1plus: warning: .../Qca-qt6/QtCrypto: not a directory` is a
  pre-existing host include-path noise affecting the whole repo).

## 2. Acceptance gate (verbatim command from the D16 spec)

```
$ QT_QPA_PLATFORM=offscreen ctest -R "test_d16_|test_whittaker|test_bfast|test_phenology|test_virtual_cube" -j1 --output-on-failure
 1/10 Test #512: test_virtual_cube_memory ............   Passed    3.21 sec
 2/10 Test #513: test_whittaker_smooth ...............   Passed    0.04 sec
 3/10 Test #514: test_phenology_extraction ...........   Passed    0.04 sec
 4/10 Test #515: test_bfast_harmonic_breaks ..........   Passed    0.06 sec
 5/10 Test #516: test_d16_temporal_trend .............   Passed    0.04 sec
 6/10 Test #517: test_d16_starfm .....................   Passed    0.04 sec
 7/10 Test #518: test_d16_timeline_scrubber_widget ...   Passed    3.30 sec
 8/10 Test #519: test_d16_temporal_profile_widget ....   Passed    0.08 sec
 9/10 Test #520: test_d16_temporal_tools .............   Passed    0.05 sec
10/10 Test #521: test_d16_temporal_phenology_e2e .....   Passed    0.21 sec
100% tests passed out of 10
Total Test time (real) =   7.12 sec
```

Per-binary assertion totals (direct runs): 396 + 69 + 46 + 127 + 70 + 565 +
27 + 8 + 48 + 675 ≈ **2 531 assertions, 0 failures** (post-review counts).

## 3. Memory audit (hardware redline)

- Peak RSS of the heaviest flow (`test_d16_temporal_phenology_e2e`, Lab08
  grading incl. 46-scene 48×48 cube, smoothing, BFAST, phenology, agent):
  **66 752 512 bytes ≈ 64 MB** — 2.3% of the 1.5 GB redline
  (`REQUIRE(peakRssBytes() < 1536ull*1024*1024)` inside the suite).
- Peak RSS of the 50-scene 2048×2048 cube flow
  (`test_virtual_cube_memory [memory]`): asserted < 1.5 GB in-test (passed;
  ru_maxrss-based, process lifetime peak).
- Tile pool ceiling: 10 × (256×256×slices×4 B) ≈ 120 MB worst case, budget-capped.

## 4. Review closure

- 2 read-only reviewer subagents (Standards + Spec axes), no recursion.
- Findings: 1×P0, 7×P1 (unique), 16×P2 → **P0 = 0, P1 = 0 open, P2 closed or
  documented** (REVIEW_LOG.md ledger; remediation commits in git).
- Reviewer-independent re-derivations confirmed: Whittaker band assembly,
  incomplete-beta F p-value vs numerical F-density integration, Gilbert chain
  (S=42, Var=124.0, Z=3.6818), 45-day gap guard on every compositing path.

## 5. Lab08 courseware gate

- `data/labs/lab8_temporal_analysis.md` + `lab8_temporal_analysis.lab.json`
  (id `temporal_phenology_timeline`; the pre-existing `labspec` id
  `temporal_analysis` untouched).
- Auto-grading TEST_CASE `Lab08 auto-grading reaches the 100-point baseline`:
  4 × 25 points all at maximum, `total == 100` asserted headless.

## 6. Deliverable inventory (spec seam → file)

| Package | Seam / artifact | Tests |
|---|---|---|
| A | `src/core/temporal_cube.{h,cpp}` + LRU tile engine | test_virtual_cube_memory (396) |
| B | `src/processing/algorithms/temporal_smoothing.{h,cpp}` (namespace `d16`) | test_whittaker_smooth (69) |
| C | `src/processing/algorithms/phenology_metrics.{h,cpp}` | test_phenology_extraction (46) |
| D | `src/processing/algorithms/breakpoint_detection.{h,cpp}` | test_bfast_harmonic_breaks (127) |
| E | `src/processing/algorithms/trend_analysis.{h,cpp}` | test_d16_temporal_trend (70) |
| F | `src/processing/algorithms/spatiotemporal_filter.{h,cpp}` | test_d16_starfm (565) |
| G | `src/app/widgets/timeline_scrubber_widget.*`, `src/app/workbench/temporal_timeline_widget.*` | test_d16_timeline_scrubber_widget (27), test_d16_temporal_profile_widget (8) |
| H | `src/agent/spatial_tools/temporal_spatial_tools.*`, `src/agent/tools/temporal_tool.h` | test_d16_temporal_tools (48) |
| I | `tests/test_d16_temporal_phenology_e2e.cpp`, `data/labs/lab8_temporal_analysis.{md,lab.json}` | test_d16_temporal_phenology_e2e (675) |

ADR: `docs/adr/0161-spatiotemporal-datacube-timeline.md`.
Planning: `.planning/temporal-phenology-timeline/{BASELINE,PLAN,DECISIONS,PROGRESS,REVIEW_LOG,EVIDENCE}.md`.
