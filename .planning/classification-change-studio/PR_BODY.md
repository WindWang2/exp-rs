# D15 Classification & Change Detection Studio — spatial leakage defense, GLCM texture, classifier kernels, CVA, accuracy assessment, workbench UI, agent diagnostics, lab E2E

**Local evidence only; no online CI dependency.** Every claim below maps to a
local command + exit code recorded in
`.planning/classification-change-studio/EVIDENCE.md`.

## Baseline

- `origin/master @ 007e70cff6f` (no rebase needed; branch contains exactly 11
  atomic commits).
- Methodology contract: Matt Pocock TDD vertical slices (stub-red →
  implementation-green → atomic commit per package) + Karpathy minimalism,
  declared up front in `.planning/classification-change-studio/{ADR-0160,PLAN,
  DECISIONS,BASELINE}.md`.

## What this track delivers (Packages A–I)

1. **Spatial leakage defense** (`src/core/spatial_split.*`, `rs::core`) —
   seeded spatial-block partitioning with an exclusive guard buffer
   (Train/Validation/Test/ExcludedBuffer state machine; strict isolation
   invariant `d(Train, Val∪Test) > buffer`) plus a Moran's I
   spatial-autocorrelation auditor with inverse-distance cutoff weights and
   degenerate-input guards. Closed-form test truth `3(3−√2)/14` independently
   recomputed by the review agent.
2. **GLCM texture** (`src/processing/algorithms/glcm_texture.*`, `rs::processing`)
   — 8 Haralick metrics over directional/omnidirectional co-occurrence
   matrices; equal-width quantization with UB-safe clamping; omni = mean of
   the four normalized matrices with metrics computed once on the mean
   (linear statistics agree, ASM/entropy/correlation do not).
3. **Classifier kernels** (`classifier_engine.*`) — KMeans (k-means++,
   farthest-point reseed), ISODATA (canonical split/merge guards, cluster cap),
   Random Forest (CART + gini + bagging + √d subspace), RBF SVM (simplified
   SMO on the dual, one-vs-one), NormalBayes (full-covariance MAP, ridge
   escalation, correct triangular back-substitution — regression-tested on a
   hand-crafted correlated covariance). Pure STL, no OpenCV; deterministic
   via a seeded LCG.
4. **Post-classification morphology** (`classification_postprocess.*`) —
   strict-majority filter (NoData-excluded vote, ties keep centre), two-pass
   component labelling with longest-shared-border sieve reassignment;
   chain/cycle-resolving elimination (no oscillation), NoData components never
   eliminated.
5. **Change detection** (`change_detector.*`) — difference / normalized
   difference / log-ratio (exact-0 identity, domain-guarded), multi-band CVA
   (magnitude, [0,2π) direction, population-σ adaptive threshold, NaN-safe
   statistics) and PCA minor-component difference (Jacobi eigendecomposition,
   sign-fixed). BSQ layout documented and used consistently.
6. **Accuracy assessment** (`confusion_matrix.*`) — K×K matrix, OA, Cohen's
   κ with po/pe guards, per-class PA/UA/F1; streaming
   `accumulateTile`+`finalize` shares one code path with single-shot
   `compute`, so zero drift is structural.
7. **Studio workbench UI** (`src/app/workbench/classification_studio_widget.*`,
   `rs::app`) — BFS magic wand over `QgsRasterLayer` (raw-unit spectral
   distance, maxPixels cap, 4/8 connectivity) returning a crack-following
   boundary polygon (every accepted pixel centre strictly inside, zero
   background leaks); 200×200 binned density scatter (100k samples bin+render
   in «40 ms, offscreen); studio shell with palette table, typed signals and
   QPointer-guarded layer binding. Compiled into the test executables and the
   `sicnu_geo_rs` target.
8. **Agent diagnostics** (`src/agent/tools/classification_tool.*`,
   `rs::agent`) — Jeffries-Matusita distance from (mean, covariance)
   sufficient statistics (Cholesky solve, ridge-free-first escalation,
   saturation clamp), JSON execute() schema reusing the Package F evaluator,
   worst-confused-pair excavation, PRUNE_FEATURE recommendations.
9. **E2E + teaching labs** (`tests/support/d15_e2e_pipeline.*`,
   `tests/test_d15_classification_change_e2e.cpp`) — full chain
   (spatial split → spectral+GLCM features → RF → morphology → CVA →
   streaming accuracy) over synthesized 256×256 quad-signature scenes with a
   32×32 vegetation→built change block; gates OA ≥ 0.88, κ ≥ 0.82,
   Dice ≥ 0.90 all green. Lab03 landcover and lab04 change artifacts are
   graded at score **100** through the real `OutputVerifier` against the
   committed rules (`data/labs/grading/*.rules.json`); lab03/04/11 specs
   validated loadable with intact grading references.

## Verification (local, offline)

- D15 precise suite: **62/62 passed** (11 binaries, `QT_QPA_PLATFORM=offscreen
  ctest -j1`, 22.9 s wall).
- Spec gate regex `test_spatial_block|test_glcm|test_classifier|
  test_classification|test_change|test_confusion|test_d15`: **56/56 passed**.
- Dual-axis review (2 read-only subagents): Standards 78→fixed, Spec 88→fixed;
  **P0 = 0, P1 = 0**; all 5 recomputed closed forms confirmed; rubric
  self-assessment **92/100** (REVIEW_LOG.md carries findings + resolutions).
- Red→green evidence per package in EVIDENCE.md (e.g. Package C red 6/7 →
  green 7/7 caught the missing `isTrained()` overrides; Package G red 8/10
  incl. 3 SEGFAULTs → green 10/10 caught an iterator-invalidation in the
  outline walk).

## Pre-existing failures at HEAD (not this PR)

`tests/test_io_operators.cpp` and `tests/test_detection_nms_10.cpp` do not
compile against system GDAL 3.13 at the baseline commit (argument-order/API
drift, 36 and 84 errors). Both files are untouched here
(`git diff 007e70cff6..HEAD` lists only additive D15 files, three CMake
source-list entries and the additive `TEST_PREFIX` plumbing in
`tests/CMakeLists.txt`); no D15 target depends on them.

## Notes

- `TEST_PREFIX` plumbing in the shared `sicnu_add_test` /
  `sicnu_discover_tests` helpers is purely additive (optional keyword,
  default behavior byte-identical for existing callers).
- New public seams live beside (never inside) prior-track files; the OpenCV
  classification stack in `src/analysis/classification` is untouched.
- No push of unverified state occurred during development (ci=none envelope);
  this PR is created after the full local gate went green.
