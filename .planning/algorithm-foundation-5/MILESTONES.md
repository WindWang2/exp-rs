# Milestones — Scientific Algorithm Foundation 5.0

Each major milestone ends with: self-check → targeted build → targeted test →
review notes → coherent commit. Sub-milestones are independently verifiable.
Status marks: [ ] todo, [~] started, [x] done, [!] blocked/deferred (with reason).

## Phase 0 — Baseline (this dossier)
- [x] 0.1 Architecture/doc audit (PROJECT/CONTEXT/TEST_INFRA/processing policies)
- [x] 0.2 Operator catalog + kernel inventory
- [x] 0.3 Capability matrix + gap plan
- [x] 0.4 Build tree configured (build-alg5, Release, tests ON, -j2)

## Milestone A — Scientific Primitives 5.0
- [x] A.1 `raster_histogram` primitive: shared binning + range policy; migrate
      duplicate implementations (image_enhancement, threshold, sar_change,
      atmospheric, feature_normalize, band_tools) incrementally
- [x] A.2 `percentile`/quantile contract (interpolation method enum, NaN/sentinel
      exclusion) with hand-derived fixtures
- [x] A.3 `window` edge-policy contract (replicated/reflect/constant/no-halo
      refusal) documented on top of gdal_block_stream halo; per-kernel audits
- [x] A.4 `morphology` primitive (erode/dilate/open/close, 4/8-conn, mask-aware)
- [x] A.5 `connected_components` primitive (union-find two-pass, bounded memory)
- [x] A.6 `distance_transform` primitive (exact Euclidean, float output)
- [x] A.7 `raster_stats` accessor consolidation audit (Welford/MathUtils owners
      stay; only document + kill true duplicates)
- [x] A.8 grid-contract gap check: rotation/axis-order coverage vs compareGrids

## Milestone B — Optical / Radiometric 5.0
- [x] B.1 Topographic correction kernel: C-correction + Minnaert + SCS+C
      (illumination cosine from DEM+slope+aspect+sun geometry), grid contract,
      known-answer synthetic DEM fixtures
- [x] B.2 Index family expansion: MSAVI, ARVI, GNDVI, NDMI, NBR, NDRE, NDSI,
      BAI, NDBaek, UI/BUI (built-up), via the existing single-index alias seam
- [x] B.3 Spectral derivatives operator (1st/2nd order, wavelength-aware)
- [x] B.4 Radiometric domain validation hardening (reflectance range refusals
      where mathematically required, declared-scale consumption audit)
- [~] B.5 Cloud/shadow post-processing: mask morphology cleanup on qa_mask path
      (uses A.4)

## Milestone C — Spectral 5.0
- [x] C.1 (audit: already callable via rs:sam_classify metric=sid) SID callable primitive + operator (reuses change-streaming seam)
- [x] C.2 Matched filter operator (whitening + matched filter, background stats
      streaming)
- [x] C.3 ACE detector (covariance-based, sample stats audited)
- [x] C.4 (audit: contract sound, no change) MNF noise estimation audit (document/fix statistics contract)
- [x] C.5 (audit: documented approximation stands) Constrained unmixing audit: NNSLO + sum-to-one mode surface; FCLS
      decision documented
- [x] C.6 (audit: linear interp contract documented) Spectral resampling contract audit (SRF vs linear)

## Milestone D — SAR 5.0
- [x] D.1 (audit: contract already covers the list) SAR metadata/radiometric-domain contract hardening
      (sigma0/gamma0/beta0 × linear/dB; dB-domain refusals audited)
- [x] D.2 Dual-pol feature operator: RVI(2), VV/VH ratio & ND, log-ratio,
      local-stats features — built on existing ratio/texture kernels
- [x] D.3 (executable subset shipped; full RTC refused + orbit contract) Range-Doppler research note + executable subset: orbit state vector
      contract (additive), zero-Doppler range time computation, DEM-based
      incidence/local-incidence + layover/shadow mask where metadata permits;
      typed refusal + extension contract where it does not. NO fake RTC.
- [x] D.4 (audit) Terrain-flatten/terrain-correction audit against D.1 contract

## Milestone E — Temporal 5.0
- [x] E.1 CUSUM + EWMA operators (hand-derived fixtures)
- [x] E.2 Seasonal Mann-Kendall (season assignment from day-of-year contract)
- [!] E.3 (deferred: multi-season phenology needs a phenology-formalism decision) Multi-season phenology + double-cropping detection
- [!] E.4 (deferred with E.3) Dynamic-threshold phenology option (per-pixel amplitude-scaled)
- [!] E.5 (deferred: harmonic+breakpoint honest-approximation documented, composition deferred) BFAST-like decomposition (honest approximation note: seasonal-trend
      via existing harmonic+breakpoint kernels) with complexity guards
- [x] E.6 (audit: machinery enforces twin exclusion) Duplicate-timestamp + timezone normalization audit in temporal_time
- [x] E.7 (audit: best-pixel-only documented) QA-weighted compositing audit (weights beyond best-pixel)

## Milestone F — Terrain/DEM 5.0
- [x] F.1 (audit: #612 conversion exists; 3 inline copies tracked as debt) CRS distance-semantics audit + fix (degree-vs-meter guard)
- [x] F.2 Curvature products (profile/plan/total via Zevenbergen-Thorne)
- [x] F.3 Multidirectional hillshade (225°/315° standard)
- [x] F.4 Local relief (focal min-max, window contract from A.3)
- [x] F.5 Depression fill (priority-flood) foundation
- [x] F.6 Flow direction (D8) + flow accumulation (topological, tile-streamed)
- [ ] F.7 TPI-based geomorphometric classification operator
- [ ] F.8 Viewshed: dependency/algorithm decision gate (documented go/no-go)

## Milestone G — Raster Spatial 5.0
- [!] G.1 `rs:resample` (deferred: wrap vs new decision needs an ADR-level grid-target contract) explicit operator (wrap GDAL warp; method enum; no
      implicit use anywhere)
- [!] G.2 `rs:align` (deferred with G.1) snap-to-grid operator (extent/resolve to reference grid;
      typed refusals per grid policy)
- [!] G.3 `rs:rasterize` (deferred: vector seam) (burn vector attribute; GDAL rasterize seam)
- [x] G.4 `rs:sieve` standalone (from post_classification kernel)
- [x] G.5 `rs:proximity` (A.6 distance transform + units contract)
- [x] G.6 (fill_holes; clump = documented alias of connected_components) `rs:fill_holes`, `rs:clump` standalone exposure
- [!] G.7 `rs:zonal_stats` (deferred: vector seam) (polygon/polygon-vector zones; streaming per zone)
- [x] G.8 `rs:focal_stats` (A.3 window contract; mean/sum/min/max/stddev/var/range/majority)
- [x] G.9 `rs:local_extrema` (maxima/minima, neighborhood contract)
- [x] G.10 morphology operator `rs:morphology` (A.4 primitive)
- [x] G.11 `rs:connected_components` / labeling operator (A.5)

## Milestone H — Classification 5.0
- [x] H.1 kNN backend (OpenCV) + schema
- [x] H.2 Minimum-distance + Mahalanobis backends (own kernels, class stats)
- [x] H.3 (audit: NormalBayes covers MLC) Gaussian maximum-likelihood backend (priors option)
- [!] H.4 (documented refusal: no OpenCV LR backend; own solver deferred) Logistic-regression backend (or documented refusal if dep mismatch)
- [!] H.5 (documented debt: kmeans covers unsupervised; ISODATA split/merge deferred) ISODATA operator (iterative, convergence contract, determinism grade)
- [x] H.6 (audit: per-class metrics incl. IoU present) Per-class metrics audit: precision/recall/F1/IoU/OA/AA/Kappa parity
- [x] H.7 (audit) Spatial cross-validation seam audit (block split)
- [x] H.8 (audit) Training-sample contract hardening (stratified/balanced/RNG doc)

## Milestone I — Change Detection contract
- [x] I.1 (done in Milestone A: otsu/percentile delegated) Threshold/histogram sharing with A.1/A.2 (delete duplicates)
- [x] I.2 (audit: transition matrix in post-classification change verified) Transition matrix audit (post-classification change)
- [ ] I.3 Change confidence surface decision (difference magnitude vs learned)
- [x] I.4 (audit: provenance stamps verified) before/after metadata provenance audit

## Milestone J — Operator Contract 5.0
- [x] J.1 New operators: schema/x-ui-type/estimate/cancel/metadata complete
- [x] J.2 (drift test updated; sidecars regenerated 7->25) Catalog drift tests extended (test_algorithm_meta_drift,
      test_catalog_size, test_algorithm_schema updated with every family)
- [x] J.3 (audit: new ops emit bounded result JSON) Bounded-result JSON audit (no unbounded arrays in results)

## Milestone K — Certification Matrix
- [x] K.1 Per-family known-answer suites for every new operator (hand-derived)
- [x] K.2 Synthetic-scene suites (planted signals, exact geometry)
- [x] K.3 Degenerate matrix (all-NoData/NaN/Inf/constant/1px/tiny/odd dims/
      mismatched grid/missing CRS/duplicate timestamps)
- [x] K.4 Execution matrix (cancel/no-partial-output/retry/deterministic rerun)
- [x] K.5 docs/processing/validation-policy.md family table updated

## Milestone L — Performance / Memory / Scale
- [x] L.1 (ebench workloads: focal_stats/proximity/spectral_derivative/topographic_correction/terrain_curvature) Benchmark baselines: terrain family, SAR neighborhood, zonal/focal,
      classification training, long temporal series (extend benchmarks/*.json)
- [x] L.2 (SICNU_BENCH_LARGE documented; default ctest-friendly sizes recorded) 4096² + tiled synthetic scale runs (no O(raster) RSS regressions)
- [ ] L.3 Cancellation latency evidence for new streaming operators
- [ ] L.4 PERFORMANCE_PLAN.md results recorded

## Review & Close
- [ ] R.1 5 lenses: scientific correctness, numerical stability/edge,
      architecture/duplication, perf/memory/cancellation, schema/docs claims
      (main agent + ≤2 fixed subagents, phased reuse; REVIEW_LOG.md)
- [ ] R.2 P0/P1 = 0 with verification evidence
- [ ] R.3 Docs sync (PROJECT/CONTEXT/CHANGELOG/docs/processing, ADR if needed)
- [ ] R.4 Coherent commit series; push; PR
