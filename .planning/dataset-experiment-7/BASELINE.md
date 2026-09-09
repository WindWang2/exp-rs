# BASELINE — audit @ master 2041f6fa (2026-09-09)

## Repository / process state

- `master == origin/master == 2041f6fa`; no open PRs; no open issues.
- Recently merged: #818 (Scientific Computing & Data Foundation 6.0),
  #819 (Workbench UX 6.0), #820 (Cartography 6.0), #821 (Help/Diagnostics 6.0),
  #822 (fix issues #773–#817, incl. dataset/experiment store fixes M2).
- Predecessor of this track: #770 "Dataset, Experiment & Reproducibility
  Foundation 5.0" (already merged). Remote branch `feat/dataset-experiment-foundation-5`
  is residue; its work is in master.
- Residue worktrees exist for merged 6.0 PRs (not active development); `win-build`
  is unrelated. None touch this track's scope going forward.
- Untracked at master checkout: `AUDIT_DOSSIER_ISSUES_747_760.md`,
  `PROJECT_REVIEW_DOSSIER_5.0.md` (historical audit reports, Platform 4/5 era).
- ADRs governing this module: 0134 (dataset store, referenced in code),
  0135 (sample/annotation/label ontology), 0136 (deterministic splits/leakage),
  0137 (experiment run identity/metrics/comparison), 0138 (lineage/environment/
  reproduction bundle).

## What already exists (DO NOT REBUILD)

`src/dataset/` (namespace `sicnu::dataset`):
- `sample.h`: `SampleRecord` envelope + variant payload — Point, Pixel, Window,
  Patch, Polygon, Object, Pair, Temporal (with missing-observation semantics),
  MultiModal (per-member missing policy). Half-open pixel windows, north-up-only
  geotransform → WKT footprints, `gridWindows`, `validateSample`.
- `annotation.h`: immutable revision-chain `AnnotationRecord` (parent revisions,
  source type incl. human/field/pseudo/model-assisted, model id+digest+threshold
  for pseudo labels).
- `label_schema.h`: validated schema documents, immutable by (id, version).
- `dataset_store.h`: SQLite WAL store — versions (draft/staged/committed/
  deprecated), paged `samplesPage` (max 500/page), annotations with chain
  integrity checks, label schemas, lineage edges, `staleStagedDrafts`.
- `dataset_fingerprint.h`, `dataset_manifest.h`, `dataset_ids.h`,
  `dataset_version.h`, `wkt.h`, `deterministic_random.h` (fixed PRNG).
- `split.h`: `SplitEngine::generate` — 12 methods: Random, Stratified, Grouped,
  SpatialBlock, SpatialBuffer, Temporal, LeaveOneRegionOut, LeaveOneSceneOut,
  LeaveOneYearOut, KFold, SpatialKFold, GroupKFold. `SplitManifest` persisted,
  content-fingerprinted, `materializeFold(foldIndex)`.
- `leakage_audit.h`: `LeakageAuditor` over `AuditSample` view; bucketed pairwise
  checks (grid hashing) for ~linear 100k behavior; explicit `auditedChecks`
  (no unevidenced "clean"); cross-split collision logic incl. fold placements.
- `dataset_quality.h`: composition (by class/sensor/region/season/modality/
  resolution/year), imbalance findings, label QA (empty/unknown/unparsable/
  out-of-raster/tiny/conflicting/duplicate), `recommendQualityLevel` gate.
- `patch_generator.h`: deterministic patch/window generation (border policy,
  nodata mode, generator config hash).
- `dataset_ids.h`, `dataset_version.h`: identity generation.

`src/experiment/` (namespace `sicnu::experiment`):
- `experiment_types.h`: `ExperimentRun` with 3 distinct hashes (config hash /
  execution fingerprint / result fingerprint); `RunStatus` truthful transitions;
  `RunEnvironment` (allowlist + denylist, `redacted()`, `redactSecretKeys`);
  `RunComparison` (dimensions: dataset/split/model/algorithm/config/seed/
  environment; verdicts Comparable/ComparableWithDifferences/NotComparable;
  `metricDiff`).
- `evaluation.h`: `ConfusionMatrix` (identity by class CODE), full derived metric
  set, regression metrics (MAPE zero policy), boundary F, AP/mAP,
  `EvaluationProtocol` (dataset/split/subset/ignore/mask/thresholds/aggregation),
  `MetricRecord` with `metricsHash`.
- `experiment_store.h`: WAL store — experiments, runs (transition-validated
  upsert), metric records, experiment-side lineage edges; paged lists.
- `lineage.h`: unified graph over both stores, typed tombstones, bounded
  traversal.
- `reproduction_bundle.h`: exporter (Reference/Portable modes), checksums,
  secret re-filtering at export, `validateBundle` with `ReproductionHooks` and
  `ReproductionLevel` Exact/Compatible/BestEffort/Impossible.

CLI (`src/cli/cli_dataset_commands.cpp`, 437 lines):
- `dataset create|inspect|validate|diff|stats`
- `experiment create|inspect|compare`
- `reproduce export|validate`

MCP (`src/agent/mcp_server.cpp`): only `describe_dataset` (raster probe).
No `dataset:`/`experiment:`/`reproducibility:` namespaced tools.

Tests (tests/CMakeLists.txt): `test_dataset_core`, `test_sample_label_annotation`,
`test_split_leakage`, `test_experiment_evaluation`, `test_dataset_quality_scale`
(100k scale, RUN_SERIAL), `test_dataset_e2e_examples`, `test_adversarial_m2`.

## Gap matrix vs the 7.0 brief

| Brief | State | Real gap |
|---|---|---|
| A MCP surface | only `describe_dataset` | No namespaced tools: dataset:list/inspect/version/diff/stats/validate, label schema, split inspect, leakage audit, experiment:list/inspect/compare, reproducibility:inspect/export/validate. CLI also misses list/version/label-schema/split/leakage/experiment:list/repro:inspect. |
| B Pipeline→SampleRecord | model + patch_generator exist in-module | No adapters promoting classification/segmentation/annotation workflow outputs (transient) into SampleRecords/AnnotationRecords; no LabelMapping ↔ LabelSchema interop; class identity must never be cv::Mat row index. |
| C Workflow→ExperimentRun | run types/store exist | No adapter from WorkflowRunCoordinator/JobEngine seams to ExperimentStore. Runs must be recorded truthfully incl. failed/cancelled/interrupted. |
| D Fold-level split & leakage | methods + materializeFold exist | No per-fold leakage audit report; no fold comparability summary (size/class balance per fold); no deterministic replay check helper. |
| E Quality at scale | paged samples + caller-side composition | No indexed facet counts in the store (composition requires full page scan); no incremental/lazy quality; no duplicate/near-duplicate/conflict/invalid-geometry summaries at scale. |
| F Reproducibility 7.0 | bundle + validate + hooks exist | No replay-readiness report (per-dependency status + missing-dependency diagnostics), no historical run/environment comparison. |
| G Comparison | identity-pin comparison + metricDiff | No metric-protocol compatibility check, no class-schema compatibility, no paired run comparison (honest statistics, no fake significance). |

## Build environment (from residue build cache)

- Generator: Ninja; preset `dev-default` (Debug, `build-dev`).
- `CMAKE_PREFIX_PATH=C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install`
- `CMAKE_TOOLCHAIN_FILE=C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake` (manifest
  mode; `vcpkg_installed` per build dir).
- MSVC 2022 via vcvars64; `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`.
