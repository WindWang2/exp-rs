# Classification & Object Intelligence 11.0 (F12) — probabilistic classification, calibration, spatial validation, uncertainty, object-level post-processing

Local evidence only; no online CI dependency.

## Summary

This PR upgrades the classification domain (D15 follow-up, no overlap with D19
Dataset Foundry) along five axes, each with machine-verifiable contracts:

1. **Class-order authority** (`RsClassOrder`): every probability /
   decision-score matrix column k refers to the k-th strictly ascending
   training class id; serialised as a bare JSON array (the existing RF/MLP
   companion-file format) and embedded in model sidecars. NormalBayes gains a
   fail-closed `.labels.json` sidecar (legacy sidecar-less models still load,
   `classOrder()` stays empty rather than being faked).
2. **Probability calibration** (`RsProbabilityCalibrator` +
   `RsCalibrationMetrics`): per-class Platt scaling (deterministic Newton,
   ridge-guarded Hessian) and isotonic regression (PAV), fitted outside the
   classifier on a held-out calibration set, one-vs-rest with row
   normalisation, fail-closed on degenerate input; multiclass Brier,
   confidence-ECE, reliability bins and log-loss with hand-computed known
   answers in tests.
3. **SVM one-vs-rest decision scores (opt-in)**: `RsClassifierSvm(true)`
   trains K binary C_SVC models at fit time and exposes sign-calibrated
   margins via `RsClassifierBackend::decisionScores()`; the default
   constructor keeps historical behaviour and cost exactly.
4. **Uncertainty** (`RsUncertainty` + pipeline integration): entropy / margin
   / confidence / ensemble-disagreement definitions locked by tests, with a
   per-measure reject-direction policy. `RsClassificationPipeline::Config`
   gains `uncertaintyOutput` (3-band Float32 GTiff: normalised entropy,
   margin, rejected mask), `rejectThreshold`, `uncertaintyMeasure`,
   `calibrationModel` / `applySidecarCalibration` — all default-off, zero
   behaviour change for existing callers.
5. **Spatial CV 2.0** (`RsSpatialCrossValidation`): group / block /
   buffered-block folds with a per-fold leakage audit (min retained
   train-test distance + distinct group overlap). A synthetic spatial leak is
   caught by construction: random folds inflate accuracy AND fail the audit
   while spatial folds stay clean (GOAL Oracle 1). Zero dataset-platform
   dependency.
6. **Feature schema** (`RsFeatureSchema` / `RsFeatureAssembler`): named typed
   feature columns with a mandatory deterministic FNV-1a fingerprint
   (drift-gated on load; external python reference literal in tests) and a
   NaN/NoData sentinel contract with per-column valid counts.
7. **Object-level cleanup** (`ClassificationObjectPostProcessor`): segment
   adjacency graph (4/8-connectivity, border weights), per-segment majority
   vote (tie → lowest class id), deterministic min-area merge (union-find,
   longest-border absorber, class/id tie-breaks), iterative strict-majority
   Jacobi smoothing; NoData (-1 / segment ≤ 0) never absorbs; segment-cap
   typed refusal.
8. **Studio 11** (`classification_studio_widget.*` only): four pure-data
   painter panels (probability bars, reliability diagram raw-vs-calibrated,
   top confusion pairs, feature importance) + clamped mean-confidence
   summary; malformed input filtered, painting never throws.
9. **Scaler hardening**: `RsFeatureScaler::fit` fails closed on non-finite
   training values.
10. **Model sidecar v2**: adds `classOrder`, `calibration`, `featureSchema`,
    `training{seed, trainSamples}`; v1 files remain readable; predict-only
    replay is byte-identical (GOAL Oracle 3).
11. **Operator surface**: `rs:supervised_classification` gains optional
    `uncertaintyOutput`, `uncertaintyMeasure`, `rejectThreshold`.

Contract truth: `docs/processing/classification-intelligence.md` (ADR-free to
avoid the #1008 numbering race — DECISIONS D-002).

## Baseline & parallel-track dedupe

- Baseline: origin/master `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  (rebased: already up to date at PR time).
- Open PR at start: #1008 (radiometric/spectral) — file-level dedupe in
  `.planning/classification-intelligence-11/PARALLEL_OWNERSHIP.md`. Shared
  integration files (`tests/CMakeLists.txt`, `src/analysis/classification/
  CMakeLists.txt`, `src/processing/CMakeLists.txt`, `.gitignore`) are
  append-only; none of #1008's changed files are touched. `docs/adr/` is
  avoided entirely.
- Open issues #1001–#1007 (io/workflow/dataset/georef R2 findings): zero file
  intersection with this track — out of scope, not fixed here (no P0-level
  classification findings among them).

## Compatibility

- All new pipeline/Config/operator parameters default to off/empty/negative —
  existing callers (GUI, agent, operators) see bit-identical behaviour for
  unchanged inputs (guarded by the existing 19-case
  `test_classification_pipeline` suite, green).
- Sidecar: writers emit version 2; readers accept 1 and 2. NOTE (accepted,
  D-008): a v2 sidecar is rejected by pre-update readers — one-way, ships
  with this PR.
- NormalBayes save is now fail-closed (writes `<model>.labels.json`, returns
  false on write failure or when no class order is known); legacy
  sidecar-less models still load.
- P1 review fix: a degenerate posterior row (e.g. NB likelihood underflow to
  all zeros) is now NoData `-1` in the probability raster and excluded from
  `meanConfidence` — previously (ADR 0094) it was silently reported as a
  genuine 0.0 confidence. Documented in contract §5.

## Local tests & evidence

All commands `QT_QPA_PLATFORM=offscreen`, binaries run directly (Catch2),
build via `cmake --build build-dev -j2` / targeted `-j1` (Unix Makefiles,
`--preset dev-default`). Full matrix with exits:
`.planning/classification-intelligence-11/TEST_MATRIX.md`.

18 suites × 2 consecutive passes (GOAL Oracle 4/6), all exit 0, zero code
changes between passes:
`test_class_order`, `test_uncertainty`, `test_feature_schema`,
`test_probability_calibration`, `test_spatial_cross_validation`,
`test_classification_object_postprocess`, `test_classification_studio_widget`,
`test_classifier_normalbayes`, `test_classifier_svm`, `test_classifier_mlp`,
`test_classifier_random_forest`, `test_feature_scaler`,
`test_cross_validation`, `test_stratified_split`,
`test_accuracy_assessment`, `test_classification_pipeline`,
`test_classification_intelligence_e2e` (6205 assertions),
`test_classification_intelligence_scale` (149910 assertions, RUN_SERIAL,
bounded invariants only — no wall-clock gates).

Final gates: `git diff --check origin/master...HEAD` clean; conflict-marker
scan clean; secret scan clean; no generated artifacts committed.

### not-executed (with proof)

`sicnu_add_test` full-stack targets (test_classifier_engine,
test_classification_postprocess, test_d15_classification_change_e2e,
test_classification_agent_tools, test_magic_wand_roi, test_feature_scatter)
cannot build on this host: two PRE-EXISTING master compile breaks under
GCC 16.2.1, both in files with an EMPTY diff against master in this PR —
`src/app/workbench/mission_context_store.cpp:24` (uses `QDir` without
`#include <QDir>`) and `src/agent/data_platform_tools.cpp:1184` (unqualified
`BenchmarkService`, declared in `namespace sicnu::experiment`). Affected
capabilities are covered by direct-link alternatives registered in this PR
(`test_classification_object_postprocess` links `sicnu_processing` only; the
studio suite compiles the widget TU directly). Fixing those two files is a
one-line-each cross-track change left to the platform owners.

## Resource evidence

Host shared with 5 concurrent track worktrees; this track held `-j2` builds /
`-j1` tests throughout (never `-j$(nproc)`), RSS peak < 20 GiB / 64 GiB,
load1 ∈ [10,17] dominated by the sibling tracks. The 100k-sample scale suite
is RUN_SERIAL with a 600 s TIMEOUT fence and asserts invariants only.

## Review findings & dispositions

Independent adversarial review (read-only subagent, full diff): **P0=0**;
3×P1 + 6×P2 + P3 findings — every one fixed or dispositioned in
`.planning/classification-intelligence-11/REVIEW_LOG.md` (headlines: NB empty
class-order save self-poisoning; orphan model file on backend-save failure;
degenerate-row NoData disclosure; mandatory fingerprint drift gate; fit label
validation; distinct-group overlap semantics; portable Fisher-Yates;
uncertainty rename error reporting). P0=0/P1=0 confirmed after fixes.

## Known limitations / follow-ups

- OpenCV `NormalBayesClassifier::predictProb` underflows to all-zero rows for
  pixels slightly off the training distribution (pre-existing ADR 0094
  fragility). This PR exposes it honestly (-1 sentinel + exclusion) and the
  contract recommends RF/MLP for uncertainty statistics; the SVM OvR +
  calibration path is the designed cure for probability-grade SVMs.
- `sicnu_add_test` master build breaks (above) — two one-line fixes for the
  platform owners.
- Backend factory substring matching (`rs_classifier_backend_factory.cpp`)
  can mis-route arbitrary names containing "rf"/"bayes" — pre-existing;
  recorded as follow-up (changing it risks existing caller strings).
- Studio panels are wired but not yet fed by a production host (D-011 scope:
  widget-own files only); main-window wiring is the natural follow-up.
- Hierarchy `ProbabilityWeightedVote` still lacks a probability source and
  OBIA lacks a probability raster output (pre-existing audit gaps #15, #20) —
  follow-up tracks.

Local evidence only; no online CI dependency. PR not merged.
