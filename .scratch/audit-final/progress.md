# Progress — exp-rs Audit (Georeferencing + Classification)

## Phase 1 — Baseline & inventory (DONE)
- 2026-08-15 — BASE_SHA 19843d1b69 recorded; worktree at /home/kevin/projects/rs-studio/main/.scratch/audit-worktree
- Toolchain: GCC 16.1.1, CMake 4.4.2, GDAL 3.13.2, PROJ 9.8.1, Qt 5.15.19, Qt6 available
- gh auth: WindWang2, full scopes
- Recent repo issues: 30/30 open, all English, [Pn][category] convention
- Repo provenance: QGIS 4.0.2 fork; georef files ported from upstream 2026-06-02 at commit 3ce168ec02
- Classification file count: 51 (analysis/) + 47 (app/) = 98 files
- Georeferencing file count: 8 (analysis/) + ~24 (app/) = ~32 files
- 4 of 5 georeferencer .ui files are orphaned; only qgsmapcoordsdialogbase.ui is live
- Scope doc: .audit/scope.md

## Phase 2 — Architecture / call-chain mapping (DONE)
- 3 parallel Explore agents delivered module map, call graph, algorithm summaries, suspicious code locations, tests coverage
- Call chains written to .audit/callchains.md
- Suspicious code locations catalogued (georef: 20 items, classification: 22 items)
- 19+1+3 = 22 test coverage gaps documented

## Phase 3 — Georeferencing deep audit (in progress)
- Phase 3 plans:
  1. Verify the highest-severity georef items: Helmert normal-matrix derivation (qgsleastsquares.cpp:119-126), Linear near-collinear (qgsleastsquares.cpp:59-62), Projective SVD silent rank deficiency (qgsleastsquares.cpp:376-382, qgsgcptransformer.cpp:506-520), CRS homogenization across GCPs (qgsgeoref_shell_window.cpp:1706-1710)
  2. Verify .points v2 y-negation semantics with referenced raster (qgsgcplist.cpp:37-38, 77; qgsgeoreftransform.cpp:180-188)
  3. Verify uncaught-exception paths in transformFromSnapshot/cloneTransform (rs_georeferencing_session.cpp:308-323)
  4. RPC option ordering (RPC_HEIGHT before RPC_DEM) against GDAL docs
  5. UI: GCP origin (0,0) rejection (qgsgeoref_shell_window.cpp:1680-1689)
  6. Performance: per-mutation full refit + raster re-open for RPC (qgsgeoreftransform.cpp:315, qgsrpcgcptransformer.cpp:81)
  7. UI: reentrancy of double click Run, modal dialog during warp, progress/cancel feedback
  8. UI: hard-coded residual thresholds in GCP list model (qgsgcplistmodel.cpp:176-179)

## Phase 4 — Classification deep audit (queued)
- Phase 4 plans (in priority order):
  1. Spatial train/test leakage at pixel level (rs_classification_split.cpp:10-13) — document vs fix
  2. Predict-only unscaled silently (rs_classification_pipeline.cpp:194-254) — repro
  3. KMeans per-pixel predict loop (rs_classifier_kmeans.cpp:91-106) — benchmark
  4. RF supportsProbabilities() API mismatch (rs_classifier_random_forest.h) — repro
  5. Hard-coded seed 42 (rs_classification_split.cpp:40) — repro
  6. testSplit=0.0 silently holds out 5% (rs_classification_split.cpp:28-29, rs_classification_pipeline.cpp:472-510)
  7. Per-tile NoData/ignore marked but still predicted (rs_classification_pipeline.cpp:766-784)
  8. Output dtype driven by classColors not remap targets (rs_classification_pipeline.cpp:575-578)
  9. majorityFilter O(H*W*k^2) (rs_post_process.cpp:263-291) — benchmark
  10. borderNeighborMajority O(components * H*W) per class (rs_post_process.cpp:96-114, 245)
  11. Spectral curves per-pixel 1x1 RasterIO (qgsclassificationmainwindow.cpp:3084)
  12. JM: covariance inv() may throw (rs_jm_separability.cpp:94)
  13. Pixel indices not persisted; ROI recomputed silently (rs_roi_io.cpp:190-191, qgsclassificationmainwindow.cpp:3598-3601)
  14. RsClassificationProject version migration (rs_classification_project.cpp:9)
  15. Cross-validation: degenerate folds silently skipped (rs_cross_validation.cpp:100-104, 161-166)
  16. MLP <2-class sentinel duplicate (rs_classifier_mlp.cpp:30-38)
  17. predictProbabilities column-alignment for MLP (rs_classification_pipeline.cpp:816-820)
  18. Kappa guard edge cases (rs_accuracy_assessment.cpp:66-67)

## Phase 5 — Pending
## Phase 6 — Pending
## Phase 7 — Pending
## Phase 8 — Pending
## Phase 9 — Pending
## Phase 10 — Pending
