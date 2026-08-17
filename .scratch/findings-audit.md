# Findings — exp-rs Audit (Georeferencing + Classification)

Format per finding:
```
## F-XXX [Area] [Type] [Severity]
- Status: <CONFIRMED_AND_ISSUED / DUPLICATE / FALSE_POSITIVE / LOW_CONFIDENCE / FIXED_IN_MASTER / NOT_REPRODUCIBLE / ENVIRONMENT_BLOCKED>
- Confidence: 0.00
- File:line: path:line
- Function/class: name
- Phase: discovered in phase N
- Evidence: <one-line summary>
- Reproduction: <minimal steps or "N/A">
- Fix experiment: <validated / attempted / not feasible / N/A>
- Issue: <URL or N/A>
```

---

## F-001 [Georeferencing] [Correctness] [P1] — Linear solver accepts near-collinear source GCPs with no error signal
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/georeferencing/qgsleastsquares.cpp:53-67
- **Function**: `QgsLeastSquares::linear`
- **Phase**: 3
- **Evidence**: Singularity check is exact-equality `deltaX == 0.0 || deltaY == 0.0` only. With near-equal source x-coords (spread ~1e-15) the solver returns `scaleX ≈ 3.7e16` with no exception; `QgsLinearGeorefTransform::updateParametersFromGcps` returns `true`; the inverse-direction guard at qgsgcptransformer.cpp:192-200 only rejects `|scale| < eps` and accepts the garbage.
- **Reproduction**: Source x = [i·1e-15 for i in 0..10], y = [i for i in 0..10], any dest; recovered scaleX = 3.7e16 (≈10^15× wrong).
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-002 [Georeferencing] [Correctness] [P1] — Projective solver silently accepts rank-deficient design matrix
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/georeferencing/qgsleastsquares.cpp:376-382; qgsgcptransformer.cpp:466-520
- **Function**: `QgsLeastSquares::projective`; `QgsProjectiveGeorefTransform::updateParametersFromGcps`
- **Phase**: 3
- **Evidence**: SVD singular values are computed but never inspected. With 4 collinear source points, solver returns "success" and `updateParametersFromGcps` returns `true`; the recovered H maps every source GCP to homogeneous zero, every GCP fails the forward Z-guard at transform time, and off-line points collapse to a single location. `hasInverse` happens to come out false in this case, so the inverse is refused — but the forward fit was silently accepted.
- **Reproduction**: src = [[0,0],[1,0],[2,0],[3,0]], dst = [[0,0],[10,20],[30,40],[50,60]] → fit "succeeds", forward transform maps every GCP to infinity.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-003 [Georeferencing] [Correctness] [P2] — `hasInverse` threshold has no scale awareness (false negatives and false positives)
- **Status**: CONFIRMED
- **Confidence**: 0.85
- **File:line**: src/analysis/georeferencing/qgsgcptransformer.cpp:504-518
- **Function**: `QgsProjectiveGeorefTransform::updateParametersFromGcps`
- **Phase**: 3
- **Evidence**: `hasInverse = |det| >= 1024*epsilon` (absolute). Three failure modes demonstrated: (a) near-rank-1 H with `det = 1025*eps` is accepted with a forward-warp inverse that produces up to ~0.59 absolute error; (b) `diag(1e-7, 1e-7, 1)` is refused despite being trivially invertible (det = 1e-14 < threshold); (c) `diag(1, 1, 1e14)` is accepted but its inverse fails the Z-guard at every point. The same geometric family under a different homogeneous scale flips `hasInverse` — pure scale blindness.
- **Reproduction**: synthetic H matrices per Section 3 of the verification report.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-004 [Georeferencing] [Correctness] [P1] — `.points` v2 format stores map coordinates in pixel columns when source raster is georeferenced; loader has no raster-state check
- **Status**: CONFIRMED
- **Confidence**: 0.90
- **File:line**: src/app/georeferencer/qgsgcplist.cpp:22-96; src/app/georeferencer/qgsgeoreftransform.cpp:180-189
- **Function**: `rsSaveGcpPointsFile`, `rsLoadGcpPointsFile`, `QgsGeorefTransform::updateParametersFromGcps`
- **Phase**: 3
- **Evidence**: The save path writes raw `sourcePoint().x(), -sourcePoint().y()` regardless of whether the source raster has a geotransform. The loader takes no raster argument and applies no pixel↔map conversion. When the source raster is georeferenced, `sourcePoint()` is a map coordinate in the source CRS (per qgsgcppoint.h:44-69 + qgsgeoreftooladdpoint.cpp:39-42 path). The fit engine converts these to pixels via `QgsRasterChangeCoords::getPixelCoords` only when the load-time raster is the *same* georeferenced raster. Any save/load raster-state mismatch silently produces a mis-scaled / mislocated georeferencing (10× scale, 1.2 km origin shift in synthetic test). External consumers reading the file per its column names get garbage.
- **Reproduction**: save .points against raster A (origin 1000,2000, 10m pixels), load against unreferenced raster B → fitted origin (500, -300), scale (1, 1) — completely wrong; same file against a different georeferenced raster B' (origin 0,0, 1m) → fitted origin (500, -300), scale (1, -1) — also wrong. Internal round-trip (same raster) happens to work.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-005 [Georeferencing] [Correctness] [P1] — GCP list with mixed destination CRSes produces silently garbage fit
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/app/georeferencer/qgsgeoref_shell_window.cpp:1706-1710, 2045-2070, 210-211, 1256-1268; src/app/georeferencer/qgsgeoreftransform.cpp:295-307, 315-410
- **Function**: `QgsGeorefShellWindow::commitGcpPair`, `loadPoints`, `refreshFit`; `QgsGeorefTransform::fit`
- **Phase**: 3
- **Evidence**: `destinationPointCrs()` is captured per-GCP at add/load time, but `fit` and the warper never consult it — the destination coordinates are passed raw to the solver. Changing the panel CRS after GCPs are picked does not re-homogenize the list. Loading a `.points` file with a different per-point CRS column 10 (`qgsgcplist.cpp:83-88`) introduces mixed CRSes silently. Synthetic mixed list (2 GCPs in EPSG:32632, 2 in EPSG:4326) fits with `scaleY ≈ -44000` and 50 km RMS, no error surfaced, `fit.ready = true`. Marker display uses `transformedDestinationPoint` to reproject for *display only* (qgsgeorefdatapoint.cpp:87-103), so on-screen markers can look correct while the fit and warp are silently garbage.
- **Reproduction**: 4 GCPs, src pixels (100,100..200,200), 2 dest in UTM 32N, 2 dest in lon/lat — fit "succeeds" with garbage.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-006 [Georeferencing] [Error-handling] [P3] — `QgsGcpPoint::transformedDestinationPoint` silently returns untransformed point on `QgsCsException`
- **Status**: CONFIRMED
- **Confidence**: 0.90
- **File:line**: src/analysis/georeferencing/qgsgcppoint.cpp:43-55
- **Function**: `QgsGcpPoint::transformedDestinationPoint`
- **Phase**: 3
- **Evidence**: The function catches `QgsCsException` and returns the raw untransformed point with only a `QgsDebugError` log. Callers (only `QgsGeorefDataPoint::destinationDisplayPoint`) place canvas markers at the wrong location. Cosmetic / display-only, but silent. No upstream caller of fit/warp is affected.
- **Reproduction**: N/A — needs a non-PROJ-grid datum transform; verified by code review and reachability check.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9) — low priority; merge into F-005 if appropriate

## F-007 [Georeferencing] [Numerical-stability] [P3] — Helmert normal equations have no coordinate normalization (inherited from upstream)
- **Status**: CONFIRMED (low severity, upstream-equivalent)
- **Confidence**: 0.80
- **File:line**: src/analysis/georeferencing/qgsleastsquares.cpp:79-174
- **Function**: `QgsLeastSquares::helmert`
- **Phase**: 3
- **Evidence**: The 4x4 normal-equations matrix is built from raw sums with no centering or scaling. For UTM-scale inputs (~4.5e6), condition number reaches ~6e20, leaving only ~3 digits of accuracy. Upstream QGIS 4.0.2 / master has byte-identical code; the projective solver does use Hartley normalization, but Helmert does not. Not a correctness bug (recovered params agree with textbook M^T M to ~2.3e-3 absolute at UTM scale), but a conditioning concern.
- **Reproduction**: 100 GCPs with UTM-scale coords (~4.5e6), 2 km spread, σ=1m noise → recovered vs textbook agree to ~2.3e-3 absolute.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9) — debatable whether to file; upstream-equivalent

## F-008 [Georeferencing] [Correctness] [FALSE_POSITIVE] — Helmert 4x4 normal-equations matrix is non-symmetric → wrong
- **Status**: FALSE_POSITIVE
- **Confidence**: 0.0
- **File:line**: src/analysis/georeferencing/qgsleastsquares.cpp:124
- **Function**: `QgsLeastSquares::helmert`
- **Phase**: 3
- **Evidence**: The matrix IS non-symmetric, but it is the true symmetric M^T M with rows reordered (permutation (2,3,0,1)) and the RHS reordered identically. Row permutation leaves the solution unchanged. Independent verification: synthetic 3 / 5 / 8 / 100-GCP fits recover the true parameters to 1e-14; condition numbers match the textbook M^T M; GSL LU produces the same answer; upstream QGIS has byte-identical code; all four test cases in tests/test_least_squares.cpp and tests/test_gcp_transformer.cpp pass.
- **Reproduction**: not a bug.
- **Fix experiment**: N/A
- **Issue**: not filed.

## F-009 [Georeferencing] [Correctness] [P2] — RPC `RPC_HEIGHT` and `RPC_DEM` are applied additively, not "DEM wins"; comments and header document a non-existent GDAL convention
- **Status**: CONFIRMED
- **Confidence**: 0.90
- **File:line**: src/analysis/georeferencing/qgsrpcgcptransformer.cpp:105-118; qgsrpcgcptransformer.h:62-65
- **Function**: `QgsRpcGcpTransformer::updateParametersFromGcps`
- **Phase**: 3
- **Evidence**: GDAL 3.10.0 source (`alg/gdal_rpc.cpp:624`): `height = dfVDatumShift + (dfHeightOffset + dfDEMH * dfHeightScale)`. With both RPC_HEIGHT and RPC_DEM set, GDAL combines them additively, not "DEM takes precedence". The local code pushes `RPC_HEIGHT` before `RPC_DEM`; ordering is irrelevant because each key appears once. The comment block (cpp:105-108) and header (h:62-65) say "GDAL will let RPC_DEM take precedence" / "the DEM raster still wins when both are present" — false. With a DEM and a non-zero Z-offset, the warp silently applies a `zOffset`-meter bias on top of the DEM.
- **Reproduction**: synthetic RPC + DEM at 100m + zOffset=10; predicted height = 110m, not 100m as the comments imply.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-010 [Georeferencing] [Error-handling] [P2] — Uncaught exception in warp path can SIGABRT the GUI process
- **Status**: CONFIRMED (latent, currently unreachable through GUI for singular GCPs)
- **Confidence**: 0.85
- **File:line**: src/app/georeferencer/rs_georeferencing_session.cpp:308-323, 325-383; src/app/georeferencer/qgsgeoreftransform.cpp:148-155; src/app/main.cpp:194, 503
- **Function**: `transformFromSnapshot`, `cloneTransform`, `startWarpTask`, `applyTransform`, `QCoreApplication::exec`
- **Phase**: 3
- **Evidence**: Stack: `applyTransform` → `startWarpTask` → `transformFromSnapshot` → `updateParametersFromGcps` → `QgsLeastSquares::linear` throws `SingularException` (a `std::runtime_error`). No try/catch anywhere on this path; `QCoreApplication::exec` (main.cpp:194/:503) has no catch either → `std::terminate` → SIGABRT. Currently unreachable through the shell UI for singular-GCP inputs (fit() pre-gates, applyTransform refuses when `fit.ready == false`), but the API is public and un-gated, and any future throw site (or `std::bad_alloc`) crashes the process.
- **Reproduction**: see code; synthetic singular GCP set through direct API call would crash.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-011 [Georeferencing] [Error-handling] [P2] — `QgsGeorefTransform::fit` swallows `SingularException`; user sees no diagnostic
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/app/georeferencer/qgsgeoreftransform.cpp:352-380
- **Function**: `QgsGeorefTransform::fit`
- **Phase**: 3
- **Evidence**: Three `catch (...)` blocks, all generic. `SingularException` is caught. After: `fit.ready=false`, `fit.errorMessage="Parameter estimation failed"` (generic; specific cause lost), `fit.residuals` all-NaN sentinels, `fit.rms=-1.0`. `errorMessage` is never displayed to the user; UI shows zeroed residuals, "RMS: —", and disabled Apply with no explanation.
- **Reproduction**: 3 collinear GCPs → click any transform → observe silent failed fit with no message.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-012 [Georeferencing] [Memory] [P2] — Closing the georeferencer window mid-warp leaks `RsWarpTask` and the warp job runs to completion unobserved
- **Status**: CONFIRMED
- **Confidence**: 0.90
- **File:line**: src/app/georeferencer/qgsgeoref_shell_window.cpp:2001-2043; src/app/georeferencer/rs_georeferencing_session.cpp:41, 325-440
- **Function**: `QgsGeorefShellWindow::closeEvent`, `RsGeoreferencingSession::~RsGeoreferencingSession`, `RsWarpTask` lifecycle
- **Phase**: 3
- **Evidence**: `closeEvent` allows the window to close while a warp is in progress (only prompts the user). The session destructor is `= default` and does not cancel or `deleteLater` the pending `RsWarpTask`; that cleanup is only triggered by `onTaskUpdated` (rs_georeferencing_session.cpp:436-437), which can never fire after the session is destroyed → the `RsWarpTask` (with its cloned transform and GDAL transformer args) is leaked, and the JobEngine job continues to completion unattended (output written, no observer).
- **Reproduction**: launch a long warp; close the georeferencer window during the warp; check process memory for the leaked task; observe the output file is still written.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-013 [Classification] [Correctness] [P1] — Pixel-level train/test split inflates reported OA/Kappa under spatial autocorrelation
- **Status**: CONFIRMED
- **Confidence**: 0.98
- **File:line**: src/analysis/classification/rs_classification_split.cpp:10-13, 15-98; qgsclassificationmainwindow.cpp:2296-2297, 2524-2525; rs_classification_pipeline.cpp:329-331; rs_cross_validation.cpp:42-67
- **Function**: `RsClassificationSplit::stratifiedSplit` and all 3 callers
- **Phase**: 4
- **Evidence**: All three user paths (GUI apply, GUI preview, pipeline auto-extract, CV) split at pixel level. No ROI-level split API exists; the header comment's recommended mitigation is not implemented. Synthetic test: 8 ROIs, 4 classes, 800 px/ROI, 4 bands, real classifiers. With zero class signal (random labels), pixel-split reports OA 0.91–0.97 across SVM/NB/RF; block-split reports OA 0.15–0.25 (chance level). Typical realistic inflation: 30–65 points; pathological: ~80 points.
- **Reproduction**: any project with multi-pixel ROIs (the standard case); reported OA/Kappa are optimistic.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9) — top-priority classification finding

## F-014 [Classification] [Correctness] [P1] (CLI) / [P2] (GUI) — Predict-only run with missing/corrupt `.meta.json` sidecar silently predicts on unscaled features
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/classification/rs_classification_pipeline.cpp:194-254, 770-779; src/operators/rs/rs_supervised_classification_operator.cpp:221-227
- **Function**: `RsClassificationPipeline::run` (predict-only branch); headless operator
- **Phase**: 4
- **Evidence**: When `loadModelSidecar` returns false (missing file, non-object JSON, version mismatch, corrupt scaler), the pipeline leaves `config.scaler` unfitted and proceeds. The tile loop guards with `if (config.scaler.isFitted())` (`:770-779`) so unscaled predict happens. The model file does not record whether training used scaling. Synthetic test: SVM RBF on 3-band DN features, scaler N(0,1) → OA 1.000 scaled → 0.500 unscaled (chance). GUI warns (qgsclassificationmainwindow.cpp:3310-3347, transient status bar). Headless operator path has zero warning and the operator schema codifies the silent fallback.
- **Reproduction**: any CLI / `sicnu_geo_rs_cli` run with `modelIn` but no `meta.json`; produces chance-level output silently.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-015 [Classification] [Determinism] [P3] — Hard-coded seed 42; no user override
- **Status**: CONFIRMED
- **Confidence**: 0.90
- **File:line**: src/analysis/classification/rs_classification_split.cpp:40; rs_cross_validation.cpp:48; rs_training_data_extraction.cpp:88
- **Function**: `stratifiedSplit`, `kFold`, `rsShuffleAndKeep`
- **Phase**: 4
- **Evidence**: Three call sites use `std::mt19937 rng(42u)` with no parameter exposed in any public API or operator schema. Deterministic (good for tests/reruns) but no way to check split stability, vary folds, or detect an unlucky test fold. Severity: P3.
- **Reproduction**: two consecutive runs on same data produce identical splits; no API to vary.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-016 [Classification] [Performance] [P3] — Ignored/NoData pixels are scaled and predicted, only discarded at writeback
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/classification/rs_classification_pipeline.cpp:766-784, 824-848
- **Function**: tile predict loop
- **Phase**: 4
- **Evidence**: Per-tile: `X` filled (`:741`), `pixelNodata` marked (`:766-768`), `scaler.transform(X)` over all pixels (`:772`), `backend->predict(X)` over all pixels (`:784`), writeback overwrites ignored with `unclassified` (`:824-848`). Wasted work factor = 1/(1−ignore_fraction): 10% → 1.11×, 50% → 2.00×, 90% → 10.00×. Trivial mask fix.
- **Reproduction**: synthetic raster with 50% NoData; predict time ~2× the necessary work.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-017 [Classification] [Correctness] [NOT_REPRODUCIBLE] — `testSplit=0.0` silently holds out 5%
- **Status**: NOT_REPRODUCIBLE (in current callers)
- **Confidence**: 0.05 (claim is wrong)
- **File:line**: src/analysis/classification/rs_classification_split.cpp:28-29
- **Function**: `stratifiedSplit`
- **Phase**: 4
- **Evidence**: The clamp at `:28-29` exists but is unreachable: pipeline guard at `rs_classification_pipeline.cpp:329` (`if (config.testSplit > 0.0)`) prevents the call when testSplit=0.0, GUI spinbox range is [0.1, 0.95] (`rs_classifier_setup_bar.cpp:111`), CV doesn't call the split. The defensive code is harmless but dead. Severity P3 if anything (documentation nit).
- **Reproduction**: not reproducible.
- **Fix experiment**: N/A
- **Issue**: not filed.

## F-018 [Classification] [Performance] [P2] — KMeans `predict` is the only non-batched backend hot path; ~8× slower than a gemm-based batched implementation
- **Status**: CONFIRMED
- **Confidence**: 0.98
- **File:line**: src/analysis/classification/rs_classifier_kmeans.cpp:91-106
- **Function**: `RsClassifierKMeans::predict`
- **Phase**: 4 + 6
- **Evidence**: Per-pixel `cv::Mat sample = data.row(i)` + `cv::norm(sample, m_centers.row(k))` loop. All other backends (SVM, NB, MLP, RF) batch through `cv::ml::StatModel::predict`. Measured on the actual compiled backend (clang++ 22, OpenCV 5.0, single core): 256×256 tile, 4-band, k=5 — real code: 102.4 ms (16 thr) / 153.4 ms (1 thr); gemm-batched: 13.0 ms / 17.6 ms. **8–9× speedup** achievable with a 1-gemm formulation that produces byte-identical 1-based labels. Per-pixel cv::norm dispatch (not arithmetic) is the dominant cost.
- **Reproduction**: any KMeans classification run on real imagery; ~38–57 s on a 5000×5000 raster.
- **Fix experiment**: batched formulation verified label-identical; not yet integrated.
- **Issue**: pending (Phase 9)

## F-019 [Classification] [Correctness] [P3] — `RsClassifierRandomForest` overrides `predictProbabilities` but not `supportsProbabilities`; pipeline blocks RF probability output
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/classification/rs_classifier_random_forest.h:14-28
- **Function**: `RsClassifierRandomForest::supportsProbabilities`
- **Phase**: 4
- **Evidence**: Base class `RsClassifierBackend::supportsProbabilities()` returns `false` (default at `rs_classifier_backend.h:33`). `rs_classifier_random_forest.h:20` overrides `predictProbabilities` (impl in cpp:45-92) but not `supportsProbabilities`. The pipeline hard-fails RF probability output at `rs_classification_pipeline.cpp:658`. The operator restricts probabilityOutput to normal_bayes (`rs_supervised_classification_operator.cpp:207`). Nuance: the RF implementation is *not* dead code in the pixel pipeline only — it IS called by the OBIA object-classification path (`rs_object_classify.cpp:75`) and by `tests/test_classifier_random_forest.cpp`. Cheap fix: add `bool supportsProbabilities() const override { return true; }` to the RF header.
- **Reproduction**: pipeline predict with `methodName="rf"` and `probabilityOutput` set → hard error.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-020 [Classification] [Data-integrity] [P1] — `RsRoiIO::load` drops pixel indices; project reload silently re-derives training samples from the *current* source raster with no identity check
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/analysis/classification/rs_roi_io.cpp:190-191; src/app/classification/qgsclassificationmainwindow.cpp:3576-3601
- **Function**: `RsRoiIO::load`, `QgsClassificationMainWindow::loadProjectFromFile`, `openSourceRaster`
- **Phase**: 4
- **Evidence**: `RsRoi::pixelIndices()` is intentionally empty after load; `loadProjectFromFile` re-rasterizes each ROI against the current source raster via `RsPixelRasterizer::rasterize`. `openSourceRaster` reads only width/height/band count/geotransform — no content/identity validation. If the source raster file is replaced at the saved path with a different image, the training sample set silently changes (no warning, no checksum, no mtime check). If the source raster is missing, restore is silently skipped — ROIs load with empty pixel indices and training extraction yields zero samples. The `.rscproj` stores absolute paths with no versioned model linkage.
- **Reproduction**: open project, replace source raster with different content, reopen project — no warning, samples derived from new content. Same for: delete source raster, reopen — silent empty training set.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-021 [Classification] [Performance] [P1] — `RsPostProcess::sieve` performs two full-image scans per component per class instead of using `connectedComponentsWithStats` bounding boxes
- **Status**: CONFIRMED
- **Confidence**: 0.98
- **File:line**: src/analysis/classification/rs_post_process.cpp:79-130, 190-261
- **Function**: `borderNeighborMajority`, `sieve`
- **Phase**: 4 + 6
- **Evidence**: `connectedComponentsWithStats` returns CC_STAT_LEFT/TOP/WIDTH/HEIGHT but the function never reads them. `borderNeighborMajority` rescans the full image per component; `sieve` also performs a second full-image scan per component for replacement. Measured: 1024² with 100 components, 1 rare class — naive 699 ms, bbox-bounded 40 ms (**17×**). 1000 components across 10 classes — naive 4556 ms, bbox-bounded 109 ms (**42×**). Extrapolated: 4096² with 1000 speckles ≈ 73 s with no progress/cancel. Byte-identical output.
- **Reproduction**: sieve on 1024² with 100 speckle components of a rare class — 699 ms shipped.
- **Fix experiment**: bbox-bounded variant verified byte-identical; not yet integrated.
- **Issue**: pending (Phase 9)

## F-022 [Classification] [Performance] [P2] — `recomputeSpectralCurves` issues one 1×1 `GDALRasterIO` per pixel per band on every ROI change, no debounce
- **Status**: CONFIRMED
- **Confidence**: 0.95
- **File:line**: src/app/classification/qgsclassificationmainwindow.cpp:268-269, 2991-3125
- **Function**: `recomputeSpectralCurves`
- **Phase**: 4
- **Evidence**: `GDALRasterIO( band, GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 )` per pixel per band. Triggered by `RsRoiCollection::changed` with no throttle (JM matrix, by contrast, is debounced 500 ms). Re-opens the dataset on every invocation. Main training extraction uses scanline grouping (`rs_training_data_extraction.cpp:112-138`) — the good pattern already exists. Concrete: a 200×200 ROI × 4 bands = 160,000 RasterIO calls per single ROI edit; 50 ROIs × 100 px × 4 bands = 20,000 calls.
- **Reproduction**: edit any ROI, observe lag.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9)

## F-023 [Classification] [Data-integrity] [P1] (headless) / [P2] (GUI) — Operator path silently produces garbage when `.meta.json` sidecar is missing/corrupt; pipeline logs nothing
- **Status**: CONFIRMED
- **Confidence**: 0.98
- **File:line**: src/operators/rs/rs_supervised_classification_operator.cpp:221-227; src/analysis/classification/rs_classification_pipeline.cpp:115-176, 193-253
- **Function**: operator `processAlgorithm`; `RsClassificationPipeline::run` (predict-only)
- **Phase**: 4
- **Evidence**: Operator's only model check is `fileExists(modelIn)`. `loadModelSidecar` returns false silently on missing/non-object/version-mismatch/corrupt-scaler JSON. `run()` swallows the failure (`:201-212`): no log, no error result, no flag. Band/feature compatibility check (`:216-237`) is skipped when the sidecar failed to load. Sidecar method is never consulted (the operator always sets `cfg.methodName`). Synthetic SVM RBF on 3-band DN features: OA 1.000 scaled → 0.500 unscaled (chance). GUI does warn (qgsclassificationmainwindow.cpp:3297-3347). The pipeline's own save path enforces the invariant the load path ignores — it deletes orphan model files (`:378-380, :398`) "so callers never load a model without its matching .meta.json", yet the headless predict-only path loads exactly that.
- **Reproduction**: any CLI / `sicnu_geo_rs_cli` run with `modelIn` and a missing/malformed `.meta.json`.
- **Fix experiment**: not yet attempted
- **Issue**: pending (Phase 9) — duplicate root-cause with F-014; merge or keep both depending on dedup outcome

---

## Summary at end of Phase 7

| # | Area | Type | Severity | Status |
|---|---|---|---|---|
| F-001 | Georeferencing | Correctness | P1 | CONFIRMED |
| F-002 | Georeferencing | Correctness | P1 | CONFIRMED |
| F-003 | Georeferencing | Correctness | P2 | CONFIRMED |
| F-004 | Georeferencing | Correctness | P1 | CONFIRMED |
| F-005 | Georeferencing | Correctness | P1 | CONFIRMED |
| F-006 | Georeferencing | Error-handling | P3 | CONFIRMED |
| F-007 | Georeferencing | Numerical-stability | P3 | CONFIRMED |
| F-008 | Georeferencing | Correctness | — | FALSE_POSITIVE |
| F-009 | Georeferencing | Correctness | P2 | CONFIRMED |
| F-010 | Georeferencing | Error-handling | P2 | CONFIRMED |
| F-011 | Georeferencing | Error-handling | P2 | CONFIRMED |
| F-012 | Georeferencing | Memory | P2 | CONFIRMED |
| F-013 | Classification | Correctness | P1 | CONFIRMED |
| F-014 | Classification | Correctness | P1/P2 | CONFIRMED |
| F-015 | Classification | Determinism | P3 | CONFIRMED |
| F-016 | Classification | Performance | P3 | CONFIRMED |
| F-017 | Classification | Correctness | — | NOT_REPRODUCIBLE |
| F-018 | Classification | Performance | P2 | CONFIRMED |
| F-019 | Classification | Correctness | P3 | CONFIRMED |
| F-020 | Classification | Data-integrity | P1 | CONFIRMED |
| F-021 | Classification | Performance | P1 | CONFIRMED |
| F-022 | Classification | Performance | P2 | CONFIRMED |
| F-023 | Classification | Data-integrity | P1/P2 | CONFIRMED |

F-014 and F-023 are the same root cause (silent unscaled predict) seen from two different angles; they will be merged at issue-submission time.
