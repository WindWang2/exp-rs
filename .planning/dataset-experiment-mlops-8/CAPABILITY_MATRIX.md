# CAPABILITY MATRIX — 8.0 track scope vs. master `322dfd3876`

Status vocabulary: Implemented / Partial / Stub / Refused-by-contract /
Missing / Duplicated / Unverified. Every row verified against code, not docs.

## A. Automatic execution-to-experiment lifecycle wiring

| Item | Status | Evidence |
|---|---|---|
| Recorder adapter (start/advance/terminal, pins, env, verified fingerprints) | Implemented (7.0) | `src/experiment/run_recorder.{h,cpp}` |
| Truthful Failed/Cancelled with evidence | Implemented | `markFailed`/`markCancelled` store error evidence in metrics doc; store validates transitions |
| **Production wiring from workflow lifecycle** | **Missing** | Zero non-test callers of `ExperimentRunRecorder` (grep src/); 7.0 PR #824 limitation #2 |
| `markInterrupted` (Interrupted status is in the enum + transition table but has no recorder API) | Missing | `run_recorder.h` has no Interrupted method; `isValidRunTransition` allows Running→Interrupted→Running (resume) |
| Interrupted/stale reconciliation (decision path, not just report) | Partial | `reconcileStaleRuns` (read-only report) exists; nothing consumes it; no truthful closing policy |

## B. Provenance completeness

| Item | Status | Evidence |
|---|---|---|
| Unified lineage graph with tombstones, bounded traversal | Implemented (7.0) | `src/experiment/lineage.{h,cpp}` |
| ExperimentRun artifacts (path/role/digest/size) | Implemented | `experiment_types.h` Artifact struct |
| **Step-level execution provenance (what actually ran: resolved params, per-step outputs + digests) propagated into the experiment record** | **Missing** | StepPlan carries resolvedParams/outputDigest (#727/#750 work) but nothing copies it into ExperimentRun; auto-recorded runs would otherwise lose it |
| Unresolvable references stay visible | Implemented | Lineage tombstones; audit reports gaps (digestUnknownCount etc.) |

## C. Dataset version lifecycle at scale

| Item | Status | Evidence |
|---|---|---|
| draft → stage → commit → deprecate | Implemented | `dataset_store.h` createDraftVersion/stageVersion/commitVersion/deprecateVersion |
| Crash-safe staging (staleStagedDrafts) | Implemented | store + tests |
| Fork | Implemented (as parent→child draft versions; no separate fork verb) | manifest parent id handling |
| Conflict handling | Implemented | `dataset.conflict` on re-create/immutable re-save; split manifest fingerprint idempotency |
| 100k-scale bounded queries | Implemented | `test_dataset_quality_scale` (733 assertions, 100k samples), SQL-side facets |

Verdict: **already satisfied** — strengthen only where WP-A integration
exposes gaps (e.g., run→dataset pin verification uses existing seams).

## D. Sample and annotation governance

| Item | Status | Evidence |
|---|---|---|
| Sample kinds (point/polygon/patch/pair/temporal/object…) | Implemented | `sample.h` SampleKind + payloads |
| Promotion with explicit unmapped-class refusal | Implemented | `sample_promotion.{h,cpp}` unmappedRawValues — promotion refused, never guessed |
| Annotation revision chains, human/field/model-assisted provenance | Implemented | `annotation.{h,cpp}`, promotion sources |
| Label ontology / raw-label mapping | Implemented | `label_schema.{h,cpp}` |
| Geometry validity | Implemented (typed WKT validation, ring validity) | `wkt.{h,cpp}` — scope is honest, no pretend OGC full validity |

Verdict: **already satisfied** for this track's scope.

## E. Split and leakage 8.0

| Item | Status | Evidence |
|---|---|---|
| 12 deterministic split methods (block/buffer/group/temporal/LORO/LOSO/LOYO/KFold/SpatialKFold/GroupKFold) | Implemented | `dataset_types.h` SplitMethod, `split.cpp` |
| 13 leakage kinds incl. patch overlap, augmentation parent, pseudo-label lineage, pre/post pair, event/temporal group | Implemented | `dataset_types.h` LeakageKind, `leakage_audit.{h,cpp}` |
| Explicit audited-checks list (no unevidenced "no leakage") | Implemented | LeakageReport::auditedChecks |
| K-fold per-fold audit + replay verification | Implemented (7.0) | `fold_audit.{h,cpp}` |
| Near-duplicate beyond exact digest | **Refused-by-contract** | Platform never invents perceptual hashing from caller hashes (7.0 honesty contract; keep) |

Verdict: **already satisfied**; near-dup remains honestly digest-based.

## F. Evaluation protocol and comparison

| Item | Status | Evidence |
|---|---|---|
| Protocol compatibility (8 dimensions) | Implemented | `comparison_ext.cpp` |
| Label schema compatibility | Implemented | compareLabelSchemas |
| Paired metrics with support gates, no fabricated significance | Implemented | pairedRunComparison (insufficient_support) |
| Non-comparable reasons | Implemented | RunComparison::reasons |
| Baseline tags / experiment groups | Partial | `Experiment.tags` exists; comparisons don't read tags; MCP experiment tools are read-only — acceptable for 8.0: document + surface tags in existing projections only if cheap |

## G. Reproducibility and replay readiness

| Item | Status | Evidence |
|---|---|---|
| Exact/Compatible/BestEffort/Impossible, never overstated | Implemented | `replay_readiness.{h,cpp}` |
| Remote/model/operator/artifact availability via hooks | Implemented | `ReproductionHooks` (unwired = Unknown/BestEffort, never fake Exact) |
| **Hooks actually wired to platform authorities (model catalog, operator registry, workflow engine)** | **Partial/Missing in surfaces** | Hooks are caller-supplied; MCP reproducibility:inspect wires dataset store but (verify at implementation time) may leave model/operator hooks unwired → readiness understated. Bridge-recorded runs should carry enough evidence for the hooks to answer. |

## H. Reproduction bundle 2.0

| Item | Status | Evidence |
|---|---|---|
| Manifest/checksums/secret filtering/portable cap | Implemented | `reproduction_bundle.{h,cpp}` (#789 defense re-filter) |
| Workflow snapshot in bundle | Implemented (workflow.json when applicable) | bundle writer |
| Migrations/validation | Implemented (schema version + validate verb) | kReproductionBundleSchemaVersion, reproducibility:validate |

Verdict: **already satisfied**; bundle gains automatically-recorded runs for
free through WP-A.

## I. User/agent surfaces

| Item | Status | Evidence |
|---|---|---|
| MCP dataset:×9 / experiment:×3 / reproducibility:×3 | Implemented (7.0) | `data_platform_tools.cpp` |
| CLI dataset/experiment/reproduce verbs | Implemented (7.0) | `cli_dataset_commands.cpp` |
| GUI dataset/experiment panel (browse/compare) | Implemented (7.0) | `dataset_experiment_panel.cpp` |
| **MCP/CLI/GUI: automatic recording of governed workflow runs** | **Missing** | Follows WP-A (opt-in per submission) |
| Experiment timeline view | Partial | panel lists runs; a timeline projection is a read-side nicety (P2, only if budget allows) |

## Priority order for 8.0 implementation

1. **WP-A core** (`ExperimentRunBridge` in sicnu_experiment + `markInterrupted`).
2. **WP-A adapter** (`sicnu_experiment_bridge`: WorkflowRun→event conversion + Qt monitor).
3. **WP-A surface**: MCP `run_workflow` opt-in recording args.
4. **WP-B**: step-level provenance stamping inside the bridge conversion
   (resolved params summary, per-step outputs/digests, error evidence).
5. **G**: ensure auto-recorded runs carry model/algorithm evidence; wire
   model/operator hooks in `reproducibility:inspect` where the authorities
   are reachable from sicnu_agent (verify first — may be P2).
6. E2E + unit tests, perf evidence on the bridge path, docs + ADR.
7. GUI panel surfacing of auto-recorded runs (P2, cheap read-side).
