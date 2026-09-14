# PROGRESS — temporal-phenology-timeline (D16)

## Commit ledger (tracer-bullet vertical slices, oldest first)

| Commit | Slice | Content |
|---|---|---|
| `fee56ac61d` | Phase 0 | ADR 0161 + PLAN + DECISIONS + BASELINE, .planning whitelist |
| `a582e21f79` | A-1 | TemporalCube seam + 16-day calendar tracer (137 assertions) |
| `8d21fe5cea` | A-2/3 | Out-of-core LRU tile engine, BestPixel/WeightedMean, 45-day gap guard |
| `3446866e1f` | A-3 | Cloud-mask/WeightedMean/gap-NaN/NoData contracts (396 total) |
| `904c229be9` | B-1 | Whittaker banded Cholesky core (35 assertions) |
| `7de2ce9d40` | B-2 | Cauchy IRLS robust Whittaker (40) |
| `3e8ac1cee1` | B-3 | Savitzky-Golay + degenerate guards (69) |
| `e0b4cf490f` | C-1 | Dynamic threshold phenology, closed-form truth (13) |
| `94b181038d` | C-2 | Double-logistic LM fit (27) |
| `3a15ee4679` | C-3 | Multi-cycle peak/valley segmentation (46) |
| `6ad06b104d` | D-1 | BFAST core: exact F/BIC gates, MOSUM screen (100) |
| `2b356ac820` | D-2 | Deforestation step exact hit (index 46, Δ −0.35, p<0.01) (109) |
| `5e27432d72` | D-3 | Model bounds, NaN-transparent outputs, refusals (127) |
| `0f57c08500` | E-1 | Theil-Sen / Mann-Kendall core (14) |
| `22e8520146` | E-2 | Gilbert benchmark + corrected S erratum chain (29) |
| `af5eeccf66` | E-3 | Raster trend batch, NaN penetration (70) |
| `afab85bbf2` | F-1/2 | STARFM kernel + homogeneity gate (232) |
| `edf2b4ae82` | F-3 | NaN sentinels, clamping, edge truncation (565) |
| `f5c90dde41` | G | Qt timeline scrubber + temporal profile widgets (35) |
| `906ed7025c` | H | Agent temporal tools: schema, drought screen, guards (48) |
| `bf4b667043` | I | E2E chain + Lab08 100-point grading + courseware (675) |

## Acceptance gate (final form)

```bash
QT_QPA_PLATFORM=offscreen ctest \
  -R "test_d16_|test_whittaker|test_bfast|test_phenology|test_virtual_cube" \
  -j1 --output-on-failure
# => 100% tests passed out of 10
```

Targets: test_virtual_cube_memory, test_whittaker_smooth, test_phenology_extraction,
test_bfast_harmonic_breaks, test_d16_temporal_trend, test_d16_starfm,
test_d16_timeline_scrubber_widget, test_d16_temporal_profile_widget,
test_d16_temporal_tools, test_d16_temporal_phenology_e2e.

Build discipline held throughout: `cmake --build build-dev -j2` (Unix Makefiles
generator), `ctest -j1`, `QT_QPA_PLATFORM=offscreen`; no `-j$(nproc)`; no
qgis_core/heavy-chain target ever built in this worktree (DECISIONS D-160-1).

## Notable in-flight findings (fixed during the track, evidenced in commits)

- D-A: `inspectRaster` (not `RasterReader::metadata`) populates band metadata;
  filename-date parsing needed real `QDate` dates (integer `YYYYMMDD+16k`
  arithmetic produced invalid dates).
- D-B: `0 × NaN = NaN` poisoned the weighted RHS — guarded; all-NaN input
  contract added (same-size NaN, not empty).
- D-D: RSS-vs-reduction comparison defect in the greedy split scan (fixed
  before any break-placement test existed); F-statistic absolute-RSS guard
  replaced by strict positivity (noise-free series carry sub-epsilon RSS).
- D-E: the D16 spec's Gilbert numbers (S=43, Var=124.6667, Z=3.7616) form an
  erratum chain; formula-exact values asserted (S=42, Var=124.0, Z=3.6818).
- D-I: the Knuth-hash spike pattern degenerated to 3-consecutive-sample runs
  (`K mod 10 = 1`), which a MAD-scale IRLS cannot separate from model misfit —
  scenario replaced with isolated spikes (the legitimate robust-smoothing
  regime); phenology hop moved to a step-free pixel (smoothing across a
  regime step sags the tail — that is BFAST's job, hop 3).

## Subagent budget

2 of ≤3 read-only reviewers dispatched (Standards axis, Spec axis) — Phase 6.
