# PLAN — D15 Classification & Change Detection Studio (Packages A–I)

Methodology: Matt Pocock TDD vertical slices (tracer bullet → core math →
robustness), Karpathy minimalism, black-box seam testing only.
Every package: **public seam → failing test → minimal green → atomic commit**.

Global conventions:
- New code lives beside (never inside) prior-track algorithm files; zero edits
  to existing algorithm headers.
- Float assertions use `Catch::Matchers::WithinAbs` with explicit tolerances;
  expected values come from hand-derived closed-form solutions or committed
  fixtures — never recomputed through the code under test.
- NoData sentinels propagate; every denominator/log carries an epsilon guard.

| Pkg | Public seam (header) | Namespace | Library | Test binary |
|---|---|---|---|---|
| A | `src/core/spatial_split.h` | `rs::core` | `qgis_core` | `test_spatial_block_leakage` |
| B | `src/processing/algorithms/glcm_texture.h` | `rs::processing` | `sicnu_processing` | `test_glcm_texture` |
| C | `src/processing/algorithms/classifier_engine.h` | `rs::processing` | `sicnu_processing` | `test_classifier_engine` |
| D | `src/processing/algorithms/classification_postprocess.h` | `rs::processing` | `sicnu_processing` | `test_classification_postprocess` |
| E | `src/processing/algorithms/change_detector.h` | `rs::processing` | `sicnu_processing` | `test_change_detector` |
| F | `src/processing/algorithms/confusion_matrix.h` | `rs::processing` | `sicnu_processing` | `test_confusion_matrix` |
| G | `src/app/workbench/classification_studio_widget.h` | `rs::app` | compiled into test + `sicnu_geo_rs` | `test_magic_wand_roi`, `test_feature_scatter`, `test_classification_studio_widget` |
| H | `src/agent/tools/classification_tool.h` | `rs::agent` | `sicnu_agent` | `test_classification_agent_tools` |
| I | `tests/support/d15_e2e_pipeline.{h,cpp}` | `rs::testing` | test-local | `test_d15_classification_change_e2e` |

## Package A — Spatial leakage defense (`rs::core`)

Seam: `SpatialBlockPartitioner::partition(span<const SpatialSamplePoint>, const SpatialBlockConfig&) -> SpatialSplitReport`;
`SpatialAutocorrelationAuditor::computeMoransI(points, values, cutoff)`.

I/O contract: pure function; no global state; `SampleRole` enum
{Unassigned, Train, Validation, Test, ExcludedBuffer}. Errors (empty input,
degenerate config) → empty report with zero counts, no throw.

Slices: (1) tracer identity partitioner; (2) block map + seeded hash roles +
buffer exclusion sweep; (3) Moran's I + degenerate-variance guards.

## Package B — GLCM texture (`rs::processing`)

Seam: `GlcmTextureCalculator::computeForWindow(span<const float> win, w, h, cfg) -> GlcmHaralickMetrics`;
`computeTextureFeatureMap(raster, w, h, cfg, featureName) -> vector<float>`.

Contract: quantization `g = clamp(floor((I-min)/(max-min) * G), 0, G-1)`;
NaN clamps to 0. Co-pairs counted per offset family (0/45/90/135°, step
`d`), symmetrized `P(i,j) = (C(i,j)+C(j,i)) / (2R)`; ΣP ≡ 1 (R>0).
Omnidirectional = mean of the 4 direction GLCMs (equal pair weighting).
8 Haralick metrics per spec formulas, entropy ε=1e-12, correlation
denominator ε guard; degenerate window (all-same value) → variance 0,
correlation 0. `computeTextureFeatureMap` supports
contrast/dissimilarity/homogeneity/energy/entropy/asm/mean/variance/correlation,
border clamp replication, out-sized `w*h`, NoData-NaN pixels → NaN output.

Slices: (1) tracer 0° GLCM on 4×4 stripes; (2) all metrics + 4 directions +
omni; (3) sliding-window map + edge behavior.

## Package C — Classifier engine (`rs::processing`)

Seam: `ClassifierEngine::create(algo, params) -> unique_ptr<IClassifierModel>`;
`fit(span X, span y, n, d)`; `predictOne`; `predictBatch`; `predictProbabilities`.

Contract: feature matrix row-major `n×d` float; labels non-negative ints;
`predictProbabilities` returns per-present-class posterior vector (size =
training class count, ascending label order) summing to 1 for RF/NormalBayes;
KMeans/ISODATA probabilities are 1-hot by assignment; SVM posterior = 1-hot
(arg-margin). Implementations: KMeans (k-means++ seed, empty-cluster reseed,
ε convergence), Random Forest (CART + gini, bagging + sqrt(d) feature
subspace, majority vote, per-tree bootstrap), SVM (RBF kernel, SMO-lite
decomposition on dual QP, C on the diagonal of the box constraints, one-vs-one
multiclass), ISODATA (split along max-σ dimension when σ>θ_S and
|S_k| ≥ 2(N_min+1); merge when centroid distance < θ_C; max-iteration guard),
NormalBayes (full covariance + 1e-6·tr ridge; MAP log-discriminant).
Numeric guards: singular covariance ridge, log ε floor, divide-by-zero smoothing.

Slices: (1) tracer factory + KMeans on two obvious blobs; (2) RF + NormalBayes
+ probabilities on separable 3-Gaussian set; (3) ISODATA split/merge + SVM +
regularization edge tests.

## Package D — Post-classification morphology (`rs::processing`)

Seam: `ClassificationPostProcessor::applyMajorityFilter / applySieveFilter /
clumpAndEliminate`.

Contract: majority = strict majority (>K/2) replaces centre, ties keep centre;
edge via clamp replication; NoData (`-1` default config) never wins a window
and is preserved only if centre is NoData. Sieve: DSU 2-pass CCL (4/8
connectivity), components smaller than `minPixelSize` merge into the
adjacent component sharing the longest border (ties → lowest class id); the
largest component of each class is never removed even if below threshold? —
no: threshold is absolute (spec), applied to all. `clumpAndEliminate` =
sieve applied to every class independently with class-aware adjacency.

Slices: (1) tracer 3×3 majority on salt noise; (2) DSU CCL + area sieve;
(3) longest-border reassignment + clump/eliminate.

## Package E — Change detection (`rs::processing`)

Seam: `ChangeDetector::computeDifference / computeNormalizedDifference /
computeLogRatio / computeCva / computePcaDifference`.

Contract: single-band ops are size-safe (size mismatch → empty output);
ND = (a−b)/(a+b+ε); log-ratio = ln((t2+ε)/(t1+ε)), ε default 1e-4.
CVA: per-pixel ΔG = ‖x2−x1‖₂ over `bands`; direction = atan2(Δx2, Δx1) mapped
to [0, 2π) (0-angle exact branch: (0,+)→π/2, (0,−)→3π/2, (0,0)→0);
threshold T = μ + α·σ over valid ΔG; mask = ΔG ≥ T. PCA-difference: joint
2B-band covariance, symmetric Jacobi eigendecomposition, project per-date
onto the minor component of the *difference* distribution — output is the
per-pixel absolute minor-component score of (x2−x1). All outputs NaN-safe.

Slices: (1) tracer diff + log-ratio identity cases; (2) CVA magnitude/direction
closed-form (3-4-5 triangle → ΔG=5, θ=atan2(4/3)) + threshold mask; (3) PCA
difference with known rotation answer + stability.

## Package F — Confusion matrix (`rs::processing`)

Seam: `ConfusionMatrixEvaluator::compute(gt, pred, classes) -> EvaluationMetrics`;
`accumulateTile(inOut, gt, pred, classes)`; `finalize(matrix, classes)`.

Contract: matrix[truth][pred] int64; classes not listed are ignored (mapped
off-matrix); OA, Cohen's κ with pe = Σ row_i·col_i / N², per-class PA/UA/F1
(0-guard denominators); streaming `accumulateTile` × `finalize` must be
bit-identical to single-shot `compute` (Zero-Drift test).

Slices: (1) tracer 2×2 OA; (2) full K×K + hand-computed 3-class matrix
(OA=0.765, κ=0.643939…, F1₀=0.8); (3) streaming accumulation equivalence.

## Package G — Studio workbench UI (`rs::app`)

Seam: `RsRoiMagicWandTool::extractRegion(seed, layer, tol, maxPixels, connectivity)`;
`FeatureScatterWidget::setData/setGridBinning/renderDensityThumbnail`;
`ClassificationStudioWidget::bindInputLayer/setClassPalette` + signals.

Contract: magic wand = BFS flood fill on normalized multi-band spectral
distance ≤ τ (per-band scale = layer range), connectivity 4/8, `maxPixels`
hard cap, returns connected-region outline polygon (bounding hull of the
mask); never leaks through a boundary where distance > τ. Scatter widget
bins into ≤ 200×200 grid, log-normalized density image (no per-point
painting) — 100k points must bin+render < 40 ms (offscreen). Studio widget
assembles class table + swipe slider (`swipeOffsetChanged` 0..1) + algorithm
combo (`classificationRequested`). Qt thread affinity: UI objects
main-thread only; algorithm work synchronous in these seams (tests offscreen).

Slices: (1) tracer magic-wand BFS on synthetic circle raster (QgsRasterLayer
fixture); (2) scatter density binning + thumbnail; (3) studio assembly +
signal wiring.

## Package H — Agent diagnostics (`rs::agent`)

Seam: `ClassificationDiagnosisTool::execute(QJsonObject) -> QJsonObject`;
static `computeJeffriesMatusitaDistance(mean1, cov1, mean2, cov2, dim)`.

Contract: JM = 2(1 − e^(−B)), B = Bhattacharyya with pooled covariance;
identical distributions → 0, disjoint (‖Δμ‖²/8 ≥ ~37) → saturates 2.0.
`execute` accepts confusion-matrix / class-stats payloads, emits
`{status: OK|WARNING|ERROR, worst_pair, jm_matrix[], recommendations[]}`;
κ < 0.6 or any JM pair < 1.4 → `status:"WARNING"` with
`code:"WARN_SEVERE_SPECTRAL_CONFUSION"` naming the worst-confused classes;
feature-importance block supports `action:"PRUNE_FEATURE"` suggestions
(gain share < 2% and collinearity > 0.9).

Slices: (1) tracer JM closed-form limits; (2) JSON schema + worst-pair
excavation; (3) pruning recommendations.

## Package I — E2E suite + labs (`rs::testing`)

Seam: `ClassificationChangeE2ePipeline::runFullWorkflow(cfg, &OA, &kappa, &dice)`
in `tests/support/d15_e2e_pipeline.{h,cpp}` (test-side orchestration seam;
production seams remain Packages A–H).

Contract: synthesize 256×256 ×4-band scene with 4 land-cover signatures
(RsSyntheticRasterBuilder + hand signatures); T2 rewrites a 32×32 block from
vegetation→urban; extract training samples from the known scene map with the
Package-A split; stack spectral + GLCM contrast/entropy features; RF classify
(Package C); majority+sieve post-process (Package D); CVA detect change
(Package E); accuracy vs. ground truth (Package F). Gates: OA ≥ 0.88,
κ ≥ 0.82, Dice ≥ 0.90. Lab slice: export artifacts at the committed grading
contracts (32×32 EPSG:4326@0.001° class map; 128×128 change mask with exactly
3072 changed px + nodata band) and assert `OutputVerifier::gradeForTeaching`
returns score 100 for `landcover_classify` / `change_detect`; assert
lab03/lab04/lab11 JSONs parse and reference existing pipelines/rules.

Slices: (1) tracer classification+accuracy mini-loop; (2) full GLCM+post+CVA
pipeline; (3) lab grading 100/100.

## Test registration

All binaries registered in `tests/CMakeLists.txt` via `sicnu_add_test(NAME ...)`
(Packages A–F, H: default link set; G: custom `add_executable` + widget
sources + AUTOMOC, mirroring `test_visual_analytics`; I: default set + extra
`d15_e2e_pipeline.cpp` source).
