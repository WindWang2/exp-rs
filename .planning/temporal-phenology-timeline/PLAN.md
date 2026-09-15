# PLAN — temporal-phenology-timeline (D16 · Temporal Phenology Timeline Studio)

9+ hour unattended track. Three iron phases: spec docs → Matt Pocock TDD vertical slices → dual-axis review.
Every package: **Public Seam → Catch2 Red → minimal C++ Green → atomic commit**. No horizontal slicing,
no tautological assertions (expected values from independent analytic references), no implementation coupling.

## Common build/verify loop

```bash
cd /home/kevin/projects/rs-studio/exp-rs-temporal-phenology-timeline
cmake --build build-dev --target <test_target> -j2          # green gate per slice
ctest --test-dir build-dev -R <target> -j1 --output-on-failure
export QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1      # headless discipline
```

New static lib `sicnu_temporal_timeline` (DECISIONS D-160-1). Final acceptance filter:
`ctest --test-dir build-dev -R "test_d16_|test_whittaker|test_bfast|test_phenology|test_virtual_cube" -j1`.

## Package A — Regularized temporal virtual cube (out-of-core)

- Seams: `src/core/temporal_cube.h` (+`.cpp`), consumed over `geospatial/raster/raster_reader.h`.
- API: `TemporalCube::open(scenePaths, TemporalCubeConfig)`, `timeline()→vector<CalendarPoint{tDays,isoDate}>`,
  `sliceCount/width/height`, `readChunk(x,y,w,h,tStart,tCount)→vector<float>` (t-major), `extractPixelSeries(x,y)`.
- Math: 16-day grid `t_k = t0 + 16k`; Q_i = (1−cloud_i)·exp(−(t_i−t_k)²/2σ_t²); BestPixel | WeightedMean;
  NaN guard per D-160-8; 256×256 tile LRU (≤10 tiles ≈ 120 MB), memory decoupled from full raster extent.
- Slices:
  1. *Tracer*: header skeleton + 16-day calendar mapping (3 mock scenes → sliceCount/timeline exact).
  2. *Tile LRU out-of-core engine*: open() metadata-only index, readChunk window assembly, peak RSS < 1.5 GB
     on 50×(2048×2048) synthetic scenes (asserted via `getrusage` ru_maxrss in-test).
  3. *Best-Pixel composite + gap guard*: cloud masking (band 2), >45 d hole → NaN, pixel series penetration.
- Test: `tests/test_virtual_cube_memory.cpp` — fixtures written at runtime with GDAL (`GTiff`), float32.
- Independent truth: grid arithmetic by hand (epoch 2020-01-01 + 16k), composite picks constructed winners
  (max-cloud-free, nearest-time), hole geometry hand-placed.

## Package B — Robust smoothing (Whittaker & Savitzky-Golay)

- Seams: `src/processing/algorithms/temporal_smoothing.h` (+`.cpp`); overload-adjacency rule D-160-4.
- API: `whittakerSmooth(y,w,λ,d=2)`, `whittakerSmoothRobust(y,w,λ,iters=3)`, `savitzkyGolay(y,window,degree)`.
- Math: (W+λDᵀD)z=Wy, banded Cholesky O(n); Cauchy IRLS c=3, scale = 1.4826·MAD; SG shrink-at-boundary.
- Slices: 1. *Tracer* pentadiagonal solve (10-pt unit-pulse, residual ‖Az−Wy‖ < 1e-4).
  2. *Robust IRLS*: analytic sine truth y=0.5+0.3·sin(2πt/365), n=365, 30% negative spikes (−0.3..−0.5) →
     MAE(fit, clean) < 0.02; λ→1e-6 interpolation max|Δ| < 1e-5.
  3. *SG + edge guards*: polynomial reproduction up to degree; all-NaN and flat-series no-crash contracts.
- Test: `tests/test_whittaker_smooth.cpp`.

## Package C — Phenology metrics (dynamic threshold, double logistic, multi-cycle)

- Seams: `src/processing/algorithms/phenology_metrics.h` (+`.cpp`).
- API: `PhenologyExtractor::extractDynamicThreshold(y,tDays,frac=0.2,sosDoy=1,eosDoy=365)`,
  `fitDoubleLogistic(y,tDays)→pair<DoubleLogisticParams,PhenologyMetrics>`,
  `extractMultiCycle(y,tDays,cycles=2,frac=0.2)`.
- Math: Ratio(t)=(z−z_min)/(z_max−z_min), crossings linearly interpolated; POS=argmax; LOS=eos−sos (wrap+365);
  6-param double logistic f(t)=y_min+(y_max−y_min)·[σ(k1(t−τ1)) + σ(−k2(t−τ2)) − 1], Levenberg–Marquardt
  (numeric Jacobian), init from threshold metrics; integral by composite Simpson; monotonicity guard
  SOS<POS<EOS else `valid=false` + reason.
- Slices: 1. *Tracer* single-season threshold (synthetic asymmetric Gaussian, SOS/POS/EOS error ≤ 1 d).
  2. *Double logistic*: parameters injected into the analytic model, recovered by fit (τ ±1.5 d, k ±10%,
     integral within 2%); monotonicity asserted.
  3. *Multi-cycle*: double-crop paddy analytic truth (SOS₁95/POS₁140/EOS₁190; SOS₂215/POS₂265/EOS₂315),
     per-cycle error ≤ 1 d, cycle separation by derivative peak-valley pairing; reversed-input refusal.
- Test: `tests/test_phenology_extraction.cpp`.

## Package D — Breakpoint detection (BFAST-simplified)

- Seams: `src/processing/algorithms/breakpoint_detection.h` (+`.cpp`).
- API: `BreakpointDetector::detectHarmonicBreaks(y,tDays,harmonics=3,maxBreaks=3,minSeg=23,α=0.05)→BfastResult`.
- Math: per-segment intercept+trend+3 harmonics (T=365.25); greedy RSS split; F-test p via regularized
  incomplete beta; BIC = n·ln(RSS/n)+p·ln(n) strict-decrease gate; MOSUM h=⌊0.15n⌋ reported (D-160-6).
- Slices: 1. *Tracer* single-break harmonic+piecewise solve & decomposition fields.
  2. *Detection*: 4-year 92-pt series, deforestation step Δα=−0.35 at index 46 → break index exact (46),
     |magnitude−(−0.35)| < 0.03, p < 0.01.
  3. *BIC + bounds*: stationary series → breakCount == 0; maxBreaks/minSegment respected.
- Test: `tests/test_bfast_harmonic_breaks.cpp`.

## Package E — Theil-Sen / Mann-Kendall trend

- Seams: `src/processing/algorithms/trend_analysis.h` (+`.cpp`).
- API: `TrendAnalyzer::computeMannKendall(y,tDays)→MannKendallResult`, `computeRasterTrend(inSeries,w,h,t,tOut...)`.
- Math: median pairwise slope; S = Σ sgn; tie-corrected Var(S); continuity-corrected Z; p = erfc(|Z|/√2).
  **Corrected Gilbert values** (D-160-5): S=43, Var=124.0, Z≈3.7717, p≈1.64e-4.
- Slices: 1. *Tracer* 5-pt strict monotonic → slope exact median. 2. *Gilbert benchmark* + erfc significance.
  3. *Raster batch*: NaN/NoData mask penetration, streaming tile reuse (no per-pixel allocation churn).
- Tests: `tests/test_d16_temporal_trend.cpp` (+ e2e coverage).

## Package F — STARFM spatiotemporal fusion (simplified kernel)

- Seams: `src/processing/algorithms/spatiotemporal_filter.h` (+`.cpp`).
- API: `SpatiotemporalFilter::predictStarfm(fine0,coarse0,coarseK,w,h,StarfmOptions)→vector<float>`.
- Math: C_ij = S_ij·T_ij·D_ij inverse-weighted kernel; spectral gate |F−F₀| ≤ threshold; [0,1] clamp; NaN sentinels.
- Slices: 1. *Tracer* 3×3 homogeneous normalization (ΣW=1, identity prediction). 2. *Full kernel*: 15×15 grid,
  uniform coarse step +0.12 → center delta exactly 0.12 (< 1e-4). 3. *Boundary + clamping*: edge windows
  truncated, no OOB, physical range respected.
- Test: `tests/test_d16_starfm.cpp`.

## Package G — Qt 6 timeline scrubber & temporal profile widgets

- Seams: `src/app/widgets/timeline_scrubber_widget.h/.cpp` (`TimelineScrubberWidget`),
  `src/app/workbench/temporal_timeline_widget.h/.cpp` (`TemporalProfileWidget` + `TemporalTimelineWidget` composite).
- Contracts: signals `dateChanged(int,QString)`, `playbackFinished`, `sampleHovered(double,float)`; states
  Paused/Playing/Seeking; QTimer 60 fps; snap-to-acquisition ≤ 5 px; static background cached to QPixmap,
  dynamic foreground light-weight.
- Slices: 1. *Tracer* scrubber signal contract (offscreen). 2. *Profile double buffering* (raw scatter +
  smoothed curve + SOS..EOS band + breakpoint marks; background cache hit on hover-only repaints).
  3. *60 fps + snap*: 100 index switches + processEvents < 1600 ms; magnet snapping within 5 px.
- Tests: `tests/test_timeline_scrubber_widget.cpp`, `tests/test_temporal_profile_widget.cpp` (QApplication offscreen).

## Package H — Agent temporal tools

- Seams: `src/agent/spatial_tools/temporal_spatial_tools.h/.cpp`, `src/agent/tools/temporal_tool.h/.cpp` (catalog wrapper).
- API: `TemporalSpatialTool::toolSchema()`, `executeTool(name,args)` for `temporal:phenology_query`,
  `temporal:trend_inspect`, `temporal:anomaly_alert`; injectable series catalog (D-160-9); Z_t=(x−μ)/σ per month,
  ≥3 consecutive months Z ≤ −1.5 → drought alert; structured refusals for reversed phenology / bad coordinates.
- Slices: 1. *Tracer* schema shape (names, required args). 2. *Drought engine*: 5-year synthetic monthly NDVI,
  year-3 spring 3-month negative anomaly → `is_drought_alert:true`, `negative_months ≥ 3`.
  3. *Guards*: EOS<SOS refusal, lon/lat out-of-range refusal — structured errors, no hallucinated metrics.
- Test: `tests/test_temporal_agent_tools.cpp`.

## Package I — E2E suite + Lab 08 courseware

- Seams: `tests/test_d16_temporal_phenology_e2e.cpp`; `data/labs/lab8_temporal_analysis.md` +
  `data/labs/lab8_temporal_analysis.lab.json` (id `temporal_phenology_timeline`, D-160-7).
- Chain: VirtualCube → 16-day regularization → robust Whittaker → BFAST → phenology → agent diagnosis;
  dimensional drift / illegal-extrapolation assertions at each hop.
- Slices: 1. *Tracer* synthetic multi-temporal scene factory + minimal load/regularize.
  2. *Full pipeline* integration. 3. *Lab08 100-point auto-grading* (25×4 items, headless, RSS-bounded).
- Test: `tests/test_d16_temporal_phenology_e2e.cpp`.

## Review & close-out (Phase 6/7)

- ≤3 read-only reviewer subagents (Standards axis: C++20/Qt6/Karpathy; Spec axis: dimensions, no-fake-interpolation,
  phenology monotonicity, detection confidence). Findings in `REVIEW_LOG.md`; P0/P1 must reach zero.
- `EVIDENCE.md`: full green ctest log, peak-RSS audit, commit ledger. Then PR `zcode/temporal-phenology-timeline` → master.
