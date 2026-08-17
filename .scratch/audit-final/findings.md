# Findings & Discoveries

## Issue Overview & Target Architecture

### GitHub Open Issues Summary
- Total open issues: 15
- Open PRs: 0

### Key Seams & Dependencies
1. **Epic 1 (Issues #36, #37, #38, #40)**:
   - `ProcessingAssetResolver` implemented in `src/data/processing_asset_resolver.h/cpp`.
   - `DerivationRecord` implemented in `src/data/derivation_record.h/cpp`.
   - `OutputCommitter` implemented in `src/processing/framework/output_committer.h/cpp`.
   - Verified by `test_processing_asset_resolver`, `test_derivation_record`, `test_output_committer`, `test_spectral_index_asset_pipeline`.

2. **Epic 2 (Issues #48, #49, #50, #51, #52, #53)**:
   - `CollectionImportService` implemented in `src/processing/framework/collection_import_service.h/cpp`.
   - Verified by `test_collection_import_service` (183 assertions in 17 test cases).

3. **Epic 3 & 4 (Issues #85, #97)**:
   - Workflow & Pipeline Session: `WorkflowSession` & `PipelineStatusResolver` in `src/workflow/`.
   - Tool Call Dispatcher: `ToolCallDispatcher` & `PythonWorkerProcessPool` in `src/python/isolated/`.

4. **Epic 5 (Issues #75, #13, #14)**:
   - Python Plugin Isolation & Wayfinders.

---

# Round 2 Findings (2026-08-16, same baseline 19843d1b69)

Re-verified against current master 880e98f138: all six files/paths unchanged except where noted.

## R2-verified-correct (no issue)
- rs_accuracy_assessment.cpp: confusion axes, OA, kappa, producer/user/F1 - hand-computed toy matches (OA=5/9, kappa=0.3207); pe==1->1.0 branch is dead-but-harmless (pe=1 implies OA=1).
- rs_hungarian_assignment.cpp: transcribed to Python, 281 trials vs scipy (square/rect/ties/negative/all-equal/kmeans-like): 0 failures. kPadCost=1e9 safe for count-bounded costs; -1 pad output handled by pipeline.
- Probability column alignment (rs_classification_pipeline.cpp:810-847): confidence = max over columns (value only); class id from predict() - column order irrelevant. NOT a bug.
- KMeans untrained predict (all-zero Nx1): unreachable - fit() return checked (:367), KMeans load() returns false (ModelOpenFailed).
- onTaskUpdated cross-thread: default AutoConnection with GUI-thread receiver -> queued. Safe.
- GCP table: sorting never enabled -> no wrong-row deletion path.
- CV fold display: folds numbered by actual successes; below filing bar.

## F-101 [Classification] [Correctness] [P1] - Magic wand ROI trains on bbox rectangle; roiDrawnPixels has zero consumers
- Status: CONFIRMED_AND_ISSUED (#283)
- File:line: src/app/classification/rs_roi_tool_magicwand.cpp:108-136; qgsclassificationmainwindow.cpp:727,1202,1176,1081
- Evidence: full chain trace; header contract (h:9) states the violated requirement.

## F-102 [Classification] [Reproducibility] [P2] - No-cap extraction uses QHash order; GUI cross-run nondeterminism defeats seed-42
- Status: CONFIRMED_AND_ISSUED (#284)
- File:line: rs_training_data_extraction.cpp:98-103 (no-cap), :250-253 (default cap=0); qgsclassificationmainwindow.cpp:2196, 3164
- Evidence: capped branch sorts first, no-cap doesn't; GUI never sets cap; operator defaults 5000 (deterministic) - GUI-only nondeterminism.

## F-103 [Classification] [Data-integrity] [P2] - saveLabelRaster/polygonize delete existing output before Create; failure loses previous result; corrupt file wedges re-runs
- Status: CONFIRMED_AND_ISSUED (#285)
- File:line: rs_post_process.cpp:485-505, 612-616
- Evidence: delete->create ordering; delete gated on GDALOpen success.

## F-104 [Classification] [Numerical] [P3] - JM separability saturates to 2.0 when samples < bands (hyperspectral); det floor 1e-300 dominates term2
- Status: CONFIRMED_AND_ISSUED (#287)
- File:line: rs_jm_separability.cpp:85-101
- Evidence: formula verified correct; degenerate-regime arithmetic (det ~ eps^d underflows for d>~50).

## F-105 [Georeferencing] [Correctness] [P2] - RPC refinement adds panel-CRS-unit biases to degree-unit offsets; no outlier rejection; applied unconditionally
- Status: CONFIRMED_AND_ISSUED (#286)
- File:line: qgsrpcgcptransformer.cpp:128-156
- Evidence: GDALRPCTransform outputs degrees; destinations raw in panel CRS; test only covers degree destinations.

## F-106 [Classification] [UI] [P3] - Magic wand flood fill silently clipped to 513x513 window
- Status: CONFIRMED_AND_ISSUED (#288)
- File:line: rs_roi_tool_magicwand.cpp:59-72
- Evidence: hard-coded halfWindow=256, no feedback, no config.

## Round 2 issues filed
#283, #284, #285, #286, #287, #288
