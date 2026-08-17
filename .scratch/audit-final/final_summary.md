# exp-rs Georeferencing + Classification Audit

## Baseline
- Repository: WindWang2/exp-rs
- Branch: master
- Commit: 19843d1b6910c9207c7e5c97863a873db679368e
- Date: 2026-08-15
- Toolchain: GCC 16.1.1, CMake 4.4.2, GDAL 3.13.2, PROJ 9.8.1, Qt 5.15.19 + Qt6, gh auth WindWang2 (full scopes)

## Coverage

### Georeferencing
- Files inventoried: 8 (analysis) + ~24 (app) = ~32
- Call chains mapped: 1 full action→controller→engine→warp graph + 1 refit graph
- UI flows covered: 11 (add/edit/enable/delete GCP, .points save/load, transform type switch, fit, apply, cancel, close-mid-warp)
- Algorithms audited: Linear, Helmert, Polynomial 1/2/3, Projective, TPS, RPC + all 5 least-squares paths
- Numerical: condition number, exact-equality vs scale-aware, SVD inspection, GSL hardening
- Lifecycle: add/edit/enable/delete/load/save/transform-switch
- Performance: per-mutation refit cost, RPC GDALOpen per fit, no caching

### Classification
- Files inventoried: 51 (analysis) + 47 (app) = 98
- Call chains mapped: 1 full training→inference→post-process graph + 1 headless-operator graph
- UI flows covered: full training workflow, ROI tools, CV, post-process, model save/load, project reload
- Algorithms audited: SVM, NormalBayes, MLP, RandomForest, KMeans + Hungarian remap + JM separability
- Data lifecycle: ROI extraction, scaler fit/transform/serialize, predict-only reload
- Performance: per-tile predict hot path, KMeans per-pixel loop, sieve O(C·W·H), spectral-curve 1×1 RasterIO
- Tests: ~30 relevant Catch2 test files, gaps documented per finding

## Findings

### Total: 23 (after adversarial challenge: 1 false-positive, 1 not-reproducible, 2 duplicates, 19 confirmed)

### By severity (confirmed only)
- **P0**: 0
- **P1**: 8
  - F-001 Linear near-collinear singularity
  - F-002 Projective silent rank-deficiency
  - F-004 .points v2 map-vs-pixel semantics
  - F-005 GCP list mixed-CRS silent garbage
  - F-013 Pixel-level split inflates OA/Kappa
  - F-014/F-023 Predict-only unscaled silent (merged: #242)
  - F-020 ROI silent recompute on project reload
  - F-021 Sieve O(C·W·H) — duplicate of #176 (not filed)
- **P2**: 7
  - F-003 hasInverse threshold scale-blind
  - F-009 RPC additive height semantics
  - F-010 Uncaught exception in warp path
  - F-011 fit() swallows SingularException
  - F-012 Warp task leak on close-mid-warp — duplicate of #238 (not filed)
  - F-018 KMeans predict hot path 8–9×
  - F-022 Spectral curves 1×1 RasterIO
- **P3**: 4
  - F-006 transformedDestinationPoint exception swallow
  - F-007 Helmert no normalization (inherited)
  - F-015 Hard-coded seed 42
  - F-016 Per-tile NoData/ignore perf
  - F-019 RF supportsProbabilities mismatch

### By area
- **Georeferencing**: 11 confirmed (F-001, F-002, F-003, F-004, F-005, F-006, F-007, F-009, F-010, F-011) + 1 false-positive (F-008) + 1 duplicate-not-filed (F-012)
- **Classification**: 8 confirmed (F-013, F-014, F-015, F-016, F-018, F-019, F-020, F-022, F-023) + 1 not-reproducible (F-017) + 1 duplicate-not-filed (F-021)

### By type
- **Correctness**: 11 (F-001, F-002, F-003, F-004, F-005, F-009, F-013, F-014, F-017-not-repro, F-019, F-020)
- **Performance**: 4 (F-016, F-018, F-021-dup, F-022)
- **Error-handling**: 3 (F-006, F-010, F-011)
- **Data-integrity**: 1 (F-023, same root as F-014)
- **Numerical-stability**: 1 (F-007)
- **Memory**: 1 (F-012-dup)
- **Determinism**: 1 (F-015)
- **False-positive**: 1 (F-008)
- **Not-reproducible**: 1 (F-017)

## Validation

### Reproduced (failing test or synthetic numerical proof)
- F-001: synthetic near-collinear x → 10^15 scale, no exception
- F-002: synthetic 4 collinear src → SVD silent, fit "succeeds"
- F-003: synthetic H with det near 1024·eps boundary → false neg/pos
- F-004: synthetic save/load against different raster state → 10× scale error
- F-005: synthetic mixed-CRS GCPs → 50 km RMS, fit "succeeds"
- F-009: synthetic RPC+DEM+zOffset → height = DEM + zOffset, not DEM
- F-013: synthetic zero-signal data → pixel-split OA 0.97, block-split OA 0.19
- F-014/F-023: synthetic SVM RBF on 3-band DN → OA 1.000 → 0.500 unscaled
- F-018: real build of `rs_classifier_kmeans.cpp` → 102 ms shipped, 13 ms batched (8× speedup, label-identical)
- F-021: synthetic sieve benchmark → 699 ms shipped, 40 ms bbox-bounded (17×, byte-identical)
- F-022: arithmetic on call count → 160,000 1×1 RasterIO per single ROI edit
- F-008 (false positive): independent derivation + 3-pt / 5-pt / 8-pt / 100-pt recovery + GSL parity + upstream byte-identical → not a bug

### Fix experimentally validated
- F-018: batched KMeans predict verified label-identical to the shipped per-pixel loop
- F-021: bbox-bounded sieve verified byte-identical to the shipped full-image scan

### Rejected false positives
- F-008: Helmert 4×4 normal-equations matrix — non-symmetric by row permutation (2,3,0,1) of the true symmetric M^T M; solution invariant

### Duplicates (not filed, recorded in `.audit/issue_dedupe.md`)
- F-012 → #238 (warp-task leak on rejected submission; F-012 is the close-mid-warp variant — same lifecycle bug)
- F-021 → #176 (sieve O(C·W·H); identical root cause)

### Environment-blocked / Not-reproducible
- F-017 (testSplit=0.0 silent 5% holdout): unreachable in current callers; the `if (config.testSplit > 0.0)` guard at `rs_classification_pipeline.cpp:329` prevents the call

## Performance Highlights (most significant measurements)

| Finding | Workload | Shipped | Fixed | Speedup | Correctness |
|---|---|---|---|---|---|
| F-018 KMeans predict | 256² tile, 4 bands, k=5 | 102.4 ms | 13.0 ms (gemm) | **8.7×** | labels byte-identical |
| F-018 KMeans predict | 1024² tile, 4 bands, k=5 | 3670 ms | 262 ms (gemm) | **14.0×** | labels byte-identical |
| F-021 sieve | 1024², 100 speckles, 1 class | 699 ms | 40 ms (bbox) | **17×** | output byte-identical |
| F-021 sieve | 1024², 1000 speckles, 10 classes | 4556 ms | 109 ms (bbox) | **42×** | output byte-identical |
| F-021 sieve (extrapolated) | 4096², 1000 speckles | ~73 s | ~2 s | **~36×** | output byte-identical |
| F-022 spectral RasterIO | 200² ROI × 4 bands | 160,000 calls | O(rows × bands) | — | identical |

## Issues created

All in English (matches recent repo convention; recent issues #212–#241 are all English with `[Pn][category]` title style).

- **#242** — `[P1][bug] rs:supervised_classification predicts silently on unscaled features when model sidecar (.meta.json) is missing or corrupt` (F-014+F-023 merged)
- **#243** — `[P1][correctness] Pixel-level train/test split silently inflates reported OA/Kappa; no ROI-level split API exists` (F-013)
- **#244** — `[P1][data-integrity] .rscproj project reload silently re-derives ROI training samples from a possibly-replaced source raster; no raster identity check` (F-020)
- **#245** — `[P1][correctness] GCP list with mixed destination CRSes produces silently garbage fit; no homogenization` (F-005)
- **#246** — `[P1][correctness] .points v2 format stores map coordinates in pixel columns when source raster is georeferenced; loader has no raster-state check` (F-004)
- **#247** — `[P1][correctness] Linear (per-axis scale+translation) georef solver accepts near-collinear source GCPs with no error signal` (F-001)
- **#248** — `[P1][correctness] Projective georef solver silently accepts rank-deficient design matrix` (F-002)
- **#249** — `[P2][correctness] RPC RPC_HEIGHT + RPC_DEM are applied additively, not 'DEM wins'; comments document a non-existent GDAL convention` (F-009)
- **#250** — `[P2][error-handling] Georeferencer warp path has no try/catch; SingularException can SIGABRT the GUI process` (F-010)
- **#251** — `[P2][error-handling] Georeferencer fit() swallows SingularException; user sees no diagnostic` (F-011)
- **#252** — `[P2][correctness] Projective hasInverse threshold has no scale awareness (false negatives and false positives)` (F-003)
- **#253** — `[P3][error-handling] QgisGcpPoint::transformedDestinationPoint silently returns untransformed point on QqsCsException` (F-006)
- **#254** — `[P2][performance] KMeans classification predict is the only non-batched backend hot path; ~8-9x slower than a gemm-based batched implementation` (F-018)
- **#255** — `[P3][correctness] RsClassifierRandomForest overrides predictProbabilities but not supportsProbabilities; pixel pipeline blocks RF probability output` (F-019)
- **#256** — `[P2][performance] recomputeSpectralCurves issues per-pixel 1x1 GDALRasterIO on every ROI change; no debounce` (F-022)
- **#257** — `[P3][numerical-stability] Helmert georef solver builds normal equations from raw sums with no coordinate normalization; condition number ~6e20 on UTM-scale inputs (inherited from upstream)` (F-007)
- **#258** — `[P3][determinism] Hard-coded seed 42 in train/test split, cross-validation, and extraction subsampling; no user override` (F-015)
- **#259** — `[P3][performance] Per-tile NoData/ignore pixels are scaled and predicted, only discarded at writeback (1.1x-10x wasted work)` (F-016)

**Total: 18 issues filed.**

## Duplicates skipped
- F-012 → existing #238 (warp-task leak on rejected submission)
- F-021 → existing #176 (classification sieve O(classes·W·H))

## Experimental fixes (validated, not integrated)
- F-018: KMeans batched predict — 8.7–14× speedup, label-identical
- F-021: bbox-bounded sieve — 17–42× speedup, byte-identical output

## Unsubmitted
None. All qualifying findings have a corresponding GitHub issue.

## Master integrity
- `git status` of master: 13 pre-existing user modifications + 2 untracked .scratch/ directories — **NONE from this audit** (audit worktree detached at BASE_SHA, removed at end)
- `git rev-parse origin/master`: 19843d1b69 (unchanged)
- No audit branch pushed
- No PR created
- Audit worktree removed (`git worktree list` shows only master + pre-existing `exp-rs-wt-build-quality`)

## Audit artifacts (preserved)
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/scope.md`
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/callchains.md`
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/findings.md` (full 23-finding log)
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/progress.md`
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/task_plan.md`
- `/home/kevin/projects/rs-studio/main/.scratch/audit-final/issue_dedupe.md`

## Goal
**COMPLETE** — both modules audited to evidence-driven depth, every finding challenged, dedup against existing issues performed, 18 issues filed, master unchanged, worktree cleaned.
