# Foundation 5.0 Audit — Dataset / Sample / Experiment & Reproducibility

- Track: D (Dataset / Sample / Experiment & Reproducibility Foundation 5.0)
- Baseline: master `93a7fb0b` (Model Runtime & AI Inference Platform 4.0)
- Branch: `feat/dataset-experiment-foundation-5`
- Worktree: `exp-rs-dataset-experiment-5`
- Status: living audit; every "Reusable seam" claim is verifiable in the files cited.

This audit answers, from code at the baseline commit, what the platform
already provides, which seams Foundation 5.0 reuses, what is genuinely missing,
and what Track D therefore builds. It deliberately does NOT restate the goal
document; it records observed state.

## 1. Capability audit

| Capability | Current State | Reusable Seam | Gap | Risk | Foundation 5 Action |
|---|---|---|---|---|---|
| DataAsset identity | `sicnu::data::AssetId` (UUID) + monotonic `AssetRevision`; identity decoupled from path via `SourceDescriptor` + SourceKey dedup (`src/data/asset_types.h`, `data_asset.h`) | Reuse AssetId as the leaf of every dataset provenance chain | None for assets | Re-deriving a second asset identity would fork the identity graph | Reference assets by `AssetId` string; never mint dataset-local asset ids |
| Physical vs logical identity | ArtifactStore separates logical key → storage path with version + digest (`src/data/artifact_store.h`) | Same split for dataset payloads | Dataset versions have no equivalent (a "dataset" is a folder path or a governance member list) | Path-as-identity creeps back in via samples | `DatasetVersion` = logical identity + content fingerprint; storage is a locator |
| Immutable content | Asset revisions bump on change; artifact versions append-only; dataset versions do NOT exist | Revision/version mechanics are proven patterns to mirror | A DatasetVersion referenced by an experiment can be silently edited today | Silent dataset drift invalidates comparisons | Draft→Commit lifecycle; committed versions are immutable; edits fork a child version |
| Digest | `artifactContentDigest` (streaming SHA-256), `canonicalizeJsonRfc8785` + `ExecutionFingerprint` (contract v2, full 256-bit digest) (`execution_fingerprint.h/.cpp`) | One canonical JSON + SHA-256 vocabulary reused everywhere | No dataset/manifest/split/experiment fingerprint exists | A second canonicalizer would break byte-stability guarantees | DatasetFingerprint/RunFingerprint hash canonical JSON via the same function |
| Provenance (per asset) | `DerivationRecord`: algorithm+version, params, inputs (assetId+revision), workflow/run/step ids, software version, fingerprint, cacheHit (`derivation_record.h`) | The record shape and `attachDerivationRecord` | Record exists per derived asset; no queryable cross-store lineage over datasets/samples/splits/runs | Lineage stays per-asset; "which dataset version fed this map" unanswerable | Lineage graph module joins dataset/experiment/artifact records (loose string refs, cycle-safe) |
| Classification samples | `RsTrainingGeometry {int classId, QgsGeometry, pixelIndices}` → `cv::Mat X/y` in-memory (`rs_training_data_extraction.h`); ephemeral, no identity, no provenance, no persistence | Extraction kernels stay (Track A ownership); the extraction RESULT can be lifted into samples | No persisted sample entity at all; classId is a UI-row-relative int | Reclassing or reordering the class table silently relabels samples | Sample/Annotation typed model with stable ids; `RsClassDef` int ids get mapped through LabelSchema (explicit mapping, no implicit renumbering) |
| Segmentation labels | Polygon/raster masks handled ad hoc in operators; no label-object model | Pixel- ignore/no-data options exist (`RsPixelIgnoreOptions`) | No label schema versioning, no annotation revisions | Label drift invisible | LabelSchema + Annotation revisions (Track D), rasterization stays with operators |
| Class definition | `RsClassDef {int id, name, color}` (`rs_class_def.h`) — int id only, no stable string id, no hierarchy, no version | The UI keeps its int ids; a mapping bridges them | Exactly the "UI row index as class id" anti-pattern the goal forbids | Taxonomy changes break stored labels | `LabelClass {stable uuid, code, parent, ...}` + `LabelMapping` for int-id interop |
| Split | `RsClassificationSplit::stratifiedSplit` on cv::Mat, seed default 42, `<7 samples → train` rule, optional group ids (`rs_classification_split.h`); `rs_cross_validation.h` similar | Deterministic-seed habit; group id concept | No persisted SplitManifest, no spatial/temporal grouping semantics, no leakage audit, seeds not recorded with results | Spatial leakage (overlapping patches across splits) undetectable; results not replayable | Deterministic Split Engine + persisted versioned SplitManifest + LeakageAudit (known-answer tested) |
| Experiment | Governance `ExperimentRecord {id, header, objective, variants[], runIds[]}` — bookkeeping only (`governance_types.h`) | Stable ExperimentId vocabulary; store patterns | No run identity binding dataset version + split + model digest + seed + environment; no config canonicalization; no comparison | Comparisons conflate incomparable runs | Experiment/ExperimentRun model + canonical config + comparability-first comparison |
| Metrics | `ResultRecord.metrics` is free QJsonObject; `rs_accuracy_assessment.h` computes confusion-matrix stats for classification sessions | Accuracy math exists for the classify session | No typed metric contract, no evaluation protocol binding, metrics arbitrary JSON | Metric comparisons meaningless across protocols | Typed metric structs + EvaluationProtocol (who/what/mask/ignore/threshold) |
| Model identity | Model catalog manifest 4.0: `id@version` identity tag + content digest as session anchor (`model_execution_service.h`, `model_runtime.h`) | Model digest is the anchor experiment runs must record | Link from run → model digest not persisted on the experiment side | Model drift invisible in comparisons | RunRecord stores model id + digest + runtime/device policy |
| Workflow / runs | `WorkflowRun` runId (filename-safe), per-step fingerprint, checkpoint, completion identity (size+mtime+digest), state machine (`workflow_run.h`); governance `RunRecord` mirror | RunId vocabulary; run states; per-step fingerprint | Run ≠ scientific ExperimentRun: no dataset version/split/seed/protocol binding, no environment capture | Reproducibility impossible to assert | ExperimentRun wraps (not replaces) workflow/task execution identity |
| Environment capture | None (bundle records software name only via `ReproBundleOptions.includeEnvironment`) | Bundle export skeleton exists (`governance/repro_bundle.h`) | No structured allowlisted environment snapshot; no secret filter | Env dump risks leaking credentials; missing env makes replay claims empty | Allowlisted `RunEnvironment` capture + secret-filtering tests |
| Reproduction bundle | `ReproBundleExporter` workspace-level: manifest/inputs/workflows/results/provenance/data, 3 modes, 20 GiB cap (`governance/repro_bundle.h`) | Directory layout + mode concept | No experiment-level bundle (dataset version refs, split, seed, model digest, validation verdict Exact/Compatible/BestEffort/Impossible) | Bundle can't answer "can this run be replayed" | Experiment-scoped bundle + ReproductionValidator; workspace exporter untouched |
| Dataset catalog | Governance store paged queries (kMaxPageSize 500), facets, forward-tolerant schema, WAL (`governance_store.h`) | Query/store patterns (batch upsert, checked commits, COUNT(*)) | Catalog of *datasets* (scientific, versioned, quality-gated) does not exist; governance Dataset is a member list | 100k-sample catalogs OOM if materialized | `DatasetStore` with index-backed paged catalog; iterators, no all-rows materialization |
| Persistence / migration | Governance + artifact stores: `schema_version`, newer→read-only, checked writes, `checkpointForBackup` (ADR 0130) | The exact store safety playbook | New stores must adopt it from day one | Half-committed dataset versions after crash | prepare/validate/stage/commit/publish transaction + stale-staging recovery |
| CLI | CLI 3.0 subcommands via `isCliCommand`/`dispatchCliCommand`, `--json` envelope (`src/cli/cli_commands.h`) | Add `dataset`/`experiment`/`reproduce` command groups | None | A second CLI framework | Commands ride the existing dispatcher |
| MCP / Pi | Tool prefix dispatch (e.g. `lineage:`) in `mcp_server.cpp`; tool catalog with manifests | `dataset:` / `experiment:` prefixes as thin read-only tools | No dataset/experiment/repro tools | Tools that bypass DataManager/TaskCenter | Thin query-only tools; no kernels in wrappers |

## 2. Current data flow (observed)

```text
Import/STAC/OutputCommitter → DataManager.register (SourceKey dedup)
  → AssetSnapshot (structure probe: raster/vector/remote map)
  → leases for view/task/edit; unload/reap policies
Derived run: TaskCenter → JobEngine → RSOperator → kernel
  → output file → DataManager re-register (dedup/revision/fingerprint rules)
  → attachDerivationRecord (inputs+revisions, params, fingerprint)
Governance mirror: GovernedAsset upsert → SQLite (WAL, paged queries)
```

## 3. Current result flow (observed)

```text
Operator/task result payload (JSON) → TaskCenter unified result surface (ADR 0132)
  → output files registered as assets (or plain files on pipeline path)
  → ResultRecord (governance): semantic type, status lifecycle, artifacts+digests,
    metrics (free JSON), producer {operatorId, workflowId, runId, stepId}
Workflow path: WorkflowRun checkpoint per step (fingerprint, completion identity,
  resume verifies bytes before serving cache)
```

## 4. Current provenance flow (observed)

```text
Per derived asset: DerivationRecord (algorithm, params, inputs[assetId,revision],
  workflow/run/step, software version, executionFingerprint, cacheHit)
Per run: WorkflowRunRecord + governance RunRecord (definition + summary + outputs)
Queries: MCP lineage: prefix (ancestors/descendants over derivation edges)
Hole: edges stop at asset/run granularity; datasets, samples, splits,
  annotations, experiments and metrics are not nodes of any queryable graph.
```

## 5. Current identity graph (observed)

```text
AssetId (UUID) ── AssetRevision (monotonic)
CollectionId (temporal collections, grouped assets)
DatasetId/ExperimentId (governance, bookkeeping only)
ArtifactId (logical key + version, digest, refs)
WorkflowRun runId (filename-safe) ── stepId ── execution fingerprint
Model identity: catalog id@version + content digest
NOT PRESENT: DatasetVersionId, SampleId, AnnotationId, SplitManifestId,
             ExperimentRun identity binding the above with seed+environment.
```

## 6. Answers to the Phase 0 question list

- **Asset identity?** `AssetId` UUID + `AssetRevision`; source dedup by canonical SourceKey.
- **Physical/logical decoupled?** Yes for artifacts (logical key → path) and assets (SourceDescriptor → resolvable source).
- **Immutable objects?** Committed artifact versions are immutable except lifecycle/refs; assets are mutable-with-revision; dataset versions do not exist yet.
- **Artifact storage/reference?** SQLite metadata store, payload at producer path, digest + stat validation, ref kinds pin artifacts, GC reaps only trash+unreferenced+stale.
- **Derived result backtracking?** DerivationRecord per asset; MCP `lineage:` queries.
- **Classification sample representation?** Ephemeral `RsTrainingGeometry` → `cv::Mat`; nothing persisted; int class ids.
- **Segmentation label representation?** Polygon/raster masks ad hoc in operators; ignore/no-data options only.
- **Workflow execution stable ID?** `runId` (validated charset) + per-step fingerprints.
- **Operator version / model digest?** Operators: platform version "1.0" placeholder; models: catalog `id@version` + content digest (strong).
- **Provenance in task/job results?** DerivationRecord + unified task result surface + workflow checkpoints.
- **Identity stable across project reload?** Assets restored by id (RestoreRequest carries id+revision); governance store mirrors by id; run records re-indexed from checkpoints.
- **Schema/serialization versioning policy?** Per-document `kSerializationVersion` constants, strict reject on mismatch (workflow run), forward-tolerant read-only for stores (governance/artifact).

## 7. Gap conclusion (what Track D builds)

1. **Dataset core** — typed `DatasetId/DatasetVersionId/DatasetManifest/DatasetVersion/DatasetEntry` + fingerprint + immutable version lifecycle + SQLite store (reuse store-safety playbook + canonical JSON + Result/Diagnostic).
2. **Sample & annotation foundation** — shared core + typed payloads (point/pixel/window/patch/polygon/object/pair/temporal/multimodal), spatial contract (half-open windows, explicit CRS/no resample), LabelSchema ontology (stable ids, hierarchy, mappings, versions), Annotation revisions with source/confidence/review.
3. **Split & leakage** — deterministic, seed-policy-driven split engines producing persisted SplitManifests; spatial/temporal/group leakage audit with typed findings and known-answer tests.
4. **Quality & catalog** — dataset statistics, QA findings (severity, lifecycle gates), index-backed paged catalog to 100k metadata rows with bounded memory.
5. **Experiment foundation** — Experiment/ExperimentRun binding dataset version + split + model digest + canonical config + seed + environment; comparability-first comparison; typed metrics + EvaluationProtocol.
6. **Lineage & reproducibility** — queryable graph over the new entities (cycle-safe, dangling-ref aware), allowlisted environment capture (secret-filtered), experiment-scoped reproduction bundle + validation verdicts.

All of it integrates through existing seams only: DataManager identities, TaskCenter/JobEngine execution, governance store patterns, CLI 3.0 dispatcher, MCP prefix tools. Track D builds **no** second scheduler, no second CLI, no second canonical JSON, no second asset identity.
