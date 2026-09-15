# BASELINE — D19 Dataset Foundry & Scientific Benchmark Platform

Recorded: 2026-09-15 (Asia/Shanghai)

## Git

| Item | Value |
|------|-------|
| Worktree | `/workspace/exp-rs-dataset-foundry-benchmark-d19` |
| Branch | `grok/dataset-foundry-benchmark-d19` |
| Seed tip | `d3387fcb` (chore seed planning track) |
| **Baseline master** | **`ebcafb4d`** — `fix(ci): correct OSR WKT import in test_io_operators (#990)` |
| Parallel track | D18 `grok/unified-mission-workbench-d18` — open PR #991 |

Master is **READ-ONLY**. This track never merges PRs.

## Toolchain on this box

| Tool | Status |
|------|--------|
| `g++` | **absent** |
| `cmake` | **absent** |
| Qt / Catch2 compile | **not available this run** |

Local evidence policy: write tests and wire CMake; mark compile/run as **not-executed** when toolchain missing. No online CI dependency.

## Parallelism constraint (D18)

D19 **must not** redesign:

- `src/app/workbench/**`
- MissionContext / main window / visual workflow canvas
- cross-studio UI shell integration

D19 owns dataset/samples/labels/versions/splits/patches/feature tables/benchmarks/evaluation/QA/experiment linkage/reproducibility/lineage.

## Existing authorities already on master (do NOT reimplement)

| Authority | Location | ADR / track |
|-----------|----------|-------------|
| DatasetStore + version DAG | `src/dataset/dataset_store*` | ADR 0134, MLOps 9 M1 |
| DatasetManifest + fingerprint | `src/dataset/dataset_manifest*`, `dataset_fingerprint*` | ADR 0134 |
| Sample model (pixel/patch/polygon/object/pair/temporal/multimodal) | `src/dataset/sample*` | ADR 0135 |
| LabelSchema + LabelMapping | `src/dataset/label_schema*` | ADR 0135 |
| Annotation revision chains + pseudo provenance | `src/dataset/annotation*` | ADR 0135 |
| Split engine (incl. SpatioTemporalBlock) | `src/dataset/split*` | ADR 0136, MLOps 9 M0 |
| Leakage audit + fold audit | `src/dataset/leakage_audit*`, `fold_audit*` | ADR 0136 |
| Patch generator (specs, not pixels) | `src/dataset/patch_generator*` | ADR 0135/0136 |
| Sample promotion (pipeline → samples) | `src/dataset/sample_promotion*` | Platform 7 |
| Composition / label QA | `src/dataset/dataset_quality*` | Platform 7 |
| Facets + quality cache | `dataset_store_facets*` | Platform 7 |
| ExperimentStore / Run / metrics | `src/experiment/*` | ADR 0137/0138 |
| EvaluationProtocol + ConfusionMatrix + metrics | `src/experiment/evaluation*` | ADR 0137 |
| Promotion evidence seam | `src/experiment/promotion*` | MLOps 9 M8 |
| Matrix / evidence / replay | `experiment_matrix*`, `evidence*`, `replay_*` | MLOps 9 |

## Open PRs at baseline (do not collide)

- #991 D18 Unified Mission Workbench — UI/MissionContext only; D19 stays headless services.

## Recent master themes (reuse, don't duplicate)

- Scientific MLOps 9/10, Model Runtime, Temporal/Spectral platforms, Data Fabric, Scientific Contract, LabSpec, dataset version DAG, spatial split / leakage defense, model promotion.
