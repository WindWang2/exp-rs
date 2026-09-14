# DECISIONS — D15 algorithm & engineering trade-offs

Each entry: decision / alternatives / why. These bind the implementations in
Packages A–I; changing one requires updating the corresponding test truth.

## A. Spatial split

- **A1 Block-to-role assignment: seeded integer hash, not RNG object.**
  `splitmix32(blockKey ^ seed) % 1000` against cumulative ratio bands.
  Alternatives: `std::mt19937` shuffle of block list (order-dependent on block
  enumeration, harder to reason per-block); hash is stateless, reproducible
  under threading, and per-block independent.
- **A2 Buffer exclusion direction: demote Val/Test samples only.**
  Train samples are never demoted; guard ring is carved out of evaluation
  side. Alternative (symmetric demotion) biases training distribution for no
  additional guarantee — the invariant only needs a no-man's-land between
  train and eval.
- **A3 `minTrainTestDistance` semantics: minimum over Train×(Validation∪Test)
  pairs.** Computed by brute force O(n²) — sample counts in the workbench are
  ≤ 1e5 and the audit runs once; a kd-tree is YAGNI here (documented).
- **A4 Moran's I on the label attribute by callers**; the partitioner reports
  the statistic but never thresholds it — humans/agents read it.

## B. GLCM

- **B1 Quantization: equal-width bins over `[minVal, maxVal]` config range.**
  Alternative: per-window histogram equalization — window-dependent bin
  semantics make features incomparable across windows; fixed range wins.
  NaN/out-of-range clamp into end bins (never dropped — keeps ΣP=1 window
  invariants with R>0).
- **B2 Omnidirectional = mean of the four directional GLCMs.** Alternative:
  pool all pair counts into one matrix (equivalent up to pair-count
  normalization per direction; mean-of-matrices keeps each direction equally
  weighted and matches ENVI/OTB behaviour).
- **B3 Normalization: symmetrized `P=(C+Cᵀ)/2R` with `R = ΣΣ C(i,j)` of the
  *un-symmetrized* count matrix.** Keeps ΣP exactly 1 for any offset family.
- **B4 Feature map edge handling: clamp-replicate borders**, window fully
  inside `[0,w)×[0,h)`; NaN pixels produce NaN feature values (sentinel
  transparency) rather than being silently filled.
- **B5 `windowSize` must be odd** (>=1, <= implemented guard 51); even values
  are rejected by clamping to the nearest odd — documented, tested.

## C. Classifiers

- **C1 RF: own CART implementation (gini, bagging, ⌈√d⌉ feature subspace),
  not OpenCV `rtrees`.** The seam (`span`-based, float row-major) must stay
  dependency-light and header-stable; OpenCV backend already exists in
  `src/analysis/classification` for the GUI path. Max depth 15 default
  matches `ClassifierHyperparameters`.
- **C2 SVM: SMO-style dual ascent with RBF kernel, one-vs-one multiclass.**
  Alternatives: libsvm vendoring (new third-party dep, rejected), OpenCV SVM
  (same argument as C1). Simplified SMO is adequate for the separable
  workbench regimes and keeps the engine self-contained; C and γ are exposed.
- **C3 KMeans seeding: k-means++** with fixed seed; empty cluster → reseed at
  the farthest-point; convergence when centroid shift < ε or maxIterations.
- **C4 ISODATA split guard `|S_k| ≥ 2(N_min+1)`, split step 0.5·σ_max** per
  the canonical algorithm; merge pairs oldest-first under θ_C; class count
  clamped to [1, 64] to bound pathology.
- **C5 NormalBayes: full covariance with ridge `+1e-6·tr(Σ)/d`** on the
  diagonal (documented deviation-free fallback for singular matrices); MAP on
  log-scale discriminant with log-prior; ε floor 1e-300 on exp/log guards.
- **C6 Probability semantics**: RF = vote share; NormalBayes = softmax of
  log-discriminants (true posterior under the Gaussian model); KMeans/ISODATA
  = 1-hot; SVM = 1-hot (calibration is out of scope).

## D. Post-processing

- **D1 Majority: strict majority (>K/2), ties keep centre** (spec-literal);
  NoData pixels excluded from the vote histogram but counted in K? — No:
  NoData excluded from both histogram and K (window of all-NoData keeps
  NoData centre).
- **D2 Sieve reassignment: longest shared border, tie → lowest class id.**
  Deterministic and matches ERDAS sieve behaviour closely enough for
  teaching; alternative (largest neighbour area) is less local.
- **D3 CCL: union-find over two passes**, 4- or 8-connectivity from config;
  components carry (classId, area, border-counts-per-neighbour-root).
- **D4 `clumpAndEliminate` operates per class-pair adjacency** (same CCL,
  sieve applied globally with min size) — kept as the spec's named composite
  (mark + eliminate) to avoid API bloat.

## E. Change detection

- **E1 CVA threshold: mean + α·σ over finite ΔG values** (Gaussian-tail
  heuristic). Alternative: Otsu on ΔG histogram — better for bimodal change
  share but unstable when change area is tiny (the workbench's common case);
  documented in DECISIONS, exposed as α multiplier only.
- **E2 Direction angle: `atan2(Δx2, Δx1)` remapped to [0, 2π)** with exact
  branch table for axis cases (spec); (0,0) → 0.0.
- **E3 Log-ratio epsilon 1e-4 default** (spec), applied to both numerator and
  denominator — keeps identical inputs at exactly 0.
- **E4 PCA difference: Jacobi eigensolver on the pooled difference
  covariance; output = |projection of per-pixel difference onto the smallest
  eigenvector|.** Full pipeline is deterministic and dependency-free;
  sign-fix eigenvector convention (largest |component| positive).

## F. Confusion matrix

- **F1 `compute` = accumulateTile over one tile + finalize** (single code
  path — the Zero-Drift property is structural, not tested-by-luck).
- **F2 Classes not present in `targetClasses` are ignored entirely** (not
  counted in N), enabling masked/limited evaluations; duplicated class ids in
  `targetClasses` are deduplicated on first occurrence.
- **F3 Guards**: N=0 → all metrics 0.0; pe=1 (degenerate marginals) → κ=0.0;
  PA/UA zero-denominator → 0.0.

## G. Workbench UI

- **G1 Magic wand distance: per-band scale by raster band min-range**
  (config pass-in `spectralTolerance` is absolute on the normalized metric);
  matches the spec's Scale_k normalization; 4/8-connectivity selectable.
- **G2 Polygon output: bounding outline (marching-square-free)** — the
  connected mask's axis-aligned outer boundary trace (Moore tracing on cell
  edges); convex hull would over-cover concave regions and lie about area.
- **G3 Scatter: 200×200 max binning grid pre-rendered to `QImage` on set**;
  paint event blits the cached image only (60 FPS invariant by construction);
  log(1+n)/log(1+max) color mapping, theme-agnostic turbo-ish LUT.
- **G4 Studio assembly keeps zero business logic** — it wires signals to
  Package C/D entry points owned by the host (adapters later); widget remains
  headless-testable (offscreen).

## H. Agent tool

- **H1 JM computed from (μ, Σ) pairs, not raw samples** — the tool consumes
  classifier-produced statistics; sample-based variant already exists in
  `src/analysis/classification/rs_jm_separability.*` (untouched).
- **H2 Saturation guard**: `B` ≥ 700 clamps to 2.0 exactly (exp underflow
  would still give ≈2.0; explicit clamp keeps assertions exact).
- **H3 `execute()` schema**: input `{confusion_matrix: int[][], class_labels:
  string[], class_stats?: {mean[], cov[][] , gain[]}}`; output
  `{status, score, kappa, worst_pair{a,b,jm}, jm_matrix, recommendations[]}`.
  Status: ERROR on schema violation; WARNING when κ<0.6 or min JM<1.4; else OK.

## I. E2E + labs

- **I1 Scene synthesis: 4 signatures with per-class Gaussian noise σ=0.02**
  on [0,1] reflectance scale; classes water/veg/built/bare spatially laid out
  as 4 quadrant rectangles + texture modulation so GLCM features help; change
  block 32×32 veg→built in T2 (Dice target ≥0.9 is comfortably reachable
  with CVA mean+1.5σ threshold on a 64×64 true-change footprint of 1024 px
  out of 65 536 → wait: 32×32 = 1024 px; threshold calibration slice tunes α
  only via fixed 1.5σ — validated in Slice 2 before freezing).
- **I2 Training samples: 400 per class drawn from the truth map under a
  Package-A split** (buffer 10 px, blocks 32 px) — leakage-free by
  construction and audited in-pipeline.
- **I3 Lab artifacts**: landcover artifact regenerated at the committed
  32×32/4326/0.001° grid by reproject-free direct synthesis (the lab fixture
  grid is authoritative; classification re-run on synthetic 4-band imagery
  derived from the *fixture truth*), graded with `landcover_classify`;
  change artifact synthesized to the `change_detect` contract (128×128, one
  3072-px rectangle changed, nodata 0.5% ring) and graded likewise.
  These grade the *pipeline outputs through the real grader* — no grader
  mocking (veto rule 4).
- **I4 Test-side E2E orchestration** (`rs::testing`, tests/support) keeps
  production libraries free of test-only glue; the seam stays `runFullWorkflow`
  exactly as specced.

## Process decisions

- **P1 Commits**: one atomic commit per green slice, message prefix
  `feat(d15/<pkg>):` / `test(d15/<pkg>):`; no drive-by refactors inside a
  red-green cycle.
- **P2 Build**: `ninja -C build-dev -j2` only; `-j1` under >70% RSS;
  `QT_QPA_PLATFORM=offscreen ctest -j1 -R "d15-set"` per package gate.
- **P3 Review**: Phase 6 dual-axis (Standards / Spec) by ≤3 read-only
  subagents; findings triaged P0–P3, P0/P1 fixed before EVIDENCE.md.
