# ARCHITECTURE — Platform 7.0 additions

## Authority map (unchanged)

- Authoritative sample/annotation/label persistence: `DatasetStore` (SQLite WAL).
- Authoritative run persistence: `ExperimentStore`.
- Authoritative execution: existing Workflow (`WorkflowRunCoordinator`) +
  `JobEngine` seams. We RECORD executions; we never execute.
- Authoritative MCP entry: `src/agent/mcp_server.cpp` tool table.
- Authoritative CLI entry: `src/cli/cli_dataset_commands.cpp`.
- ADRs 0134–0138 keep governing identity/splits/lineage/bundles.

## New units (all thin over the above)

1. `src/experiment/run_recorder.{h,cpp}` (M3, goal C)
   - `ExperimentRunRecorder`: adapter that opens a run from an execution
     request (workflow/operator identity, params, dataset version + fingerprint,
     split manifest + fingerprint, model identity, seed, environment capture),
     advances it through `upsertRun` transitions, and finalizes with truthful
     terminal states (`Failed`/`Cancelled` included — never swallowed into
     success). Artifacts digest via existing digest helpers; metrics recorded as
     `MetricRecord` when an evaluation protocol is supplied.
   - One `executionRef` back-pointer to the workflow/job id. No second
     scheduler; the recorder is called BY the existing seam (or by tests/CLI
     directly).
   - Crash/interrupt semantics: a run left `Running` with a dead execution ref
     is reported by `reconcileStaleRuns` (read-only truth report), never
     auto-marked success.

2. `src/dataset/sample_promotion.{h,cpp}` (M2, goal B)
   - `promoteClassificationRaster`: classified raster (+ class mapping) →
     polygon/pixel samples with class identity from a `LabelMapping`
     (class CODES, never cv::Mat row index — row index is promotion-time only).
   - `promoteSegmentationObjects`: segmentation outputs (object refs) →
     `ObjectSample`s with source asset + object ref + bounds.
   - `promoteAnnotations`: annotation workflow geometry + class + provenance →
     `AnnotationRecord` revision chains (human/field/pseudo/model-assisted).
   - `promotePairs`/`promoteTemporal`: pre/post and time-series assembly from
     member samples with event-group semantics.
   - All promotions write through `DatasetStore.addSamples/addAnnotation` into
     DRAFT versions only (committed versions stay immutable).

3. `src/dataset/fold_audit.{h,cpp}` (M4, goal D)
   - `auditFolds`: for each fold of a fold-based manifest, materialize
     train/test, run `LeakageAuditor` with fold placements, aggregate per-fold
     findings + comparability summary (fold sizes, per-class counts, group
     disjointness). Deterministic replay check: re-run `SplitEngine.generate`
     and compare fingerprints.

4. `src/dataset/dataset_facets.{h,cpp}` (M5, goal E)
   - SQL-side facet counts over the samples table + annotation tips (by class,
     sensor, region, season, modality, year, kind, quality bucket) with bounded
     result size; `FacetQuery` (filter + group + limit) executed in SQLite, not
     in C++ memory.
   - Incremental quality: cached quality summaries per version (recomputed on
     draft mutation, stamped with sample-count + max-rowid so staleness is
     detectable). Duplicate summary via content-digest buckets; near-duplicate
     via optional evidence supplied by the caller (platform never pretends a
     perceptual hash it did not compute).

5. `src/experiment/replay_readiness.{h,cpp}` (M6, goal F)
   - `assessReplayReadiness(run, hooks)`: per-dependency report (dataset
     version availability + fingerprint match, split manifest, model
     id@digest, algorithm/workflow availability, artifacts on disk with size
     match, environment compatibility) each with status/availability/reason;
     overall `ReproductionLevel` with the evidence trail; explicit
     `MissingDependency` diagnostics list. Historical comparison: `compareRuns`
     extends existing `RunComparison` (see 6).

6. Comparison extensions (M7, goal G) — added to `experiment_types.h` /
   new `comparison_ext.{h,cpp}`
   - `protocolCompatibility(a, b)`: `EvaluationProtocol` equality semantics
     (dataset/split/subset/ignore/mask/thresholds/aggregation) → compatible /
     not-comparable with reasons.
   - `classSchemaCompatibility(schemaA, schemaB)`: identity, version, code
     sets (added/removed/renamed) → comparability impact.
   - `pairedRunComparison(a, b)`: requires same identity pins + seed; reports
     per-fold/per-subset metric deltas with counts; where n is too small the
     report says "insufficient n" — NO fabricated significance.

7. MCP surface (M1, goal A) — new file `src/agent/mcp_tools_data.{h,cpp}`
   - Namespaced tools (page/bounded JSON outputs, `limit` defaults, cursors):
     `dataset:list`, `dataset:inspect`, `dataset:version`, `dataset:diff`,
     `dataset:stats`, `dataset:validate`, `dataset:label_schema`,
     `dataset:split_inspect`, `dataset:leakage_audit`,
     `experiment:list`, `experiment:inspect`, `experiment:compare`,
     `reproducibility:inspect`, `reproducibility:export`,
     `reproducibility:validate`.
   - Read-only tools default; export takes an explicit output dir; all outputs
     pass through bounded pagination (store max page size 500; MCP layer caps
     lower and returns `next_offset`).
   - CLI completion mirrors the same verbs (dataset:list/version/label-schema/
     split/leakage; experiment:list; reproduce:inspect) reusing one shared
     projection helper so CLI and MCP cannot drift.

## Ownership / seams

- `src/dataset`, `src/experiment` own the new logic. `src/agent` (MCP) and
  `src/cli` get thin registrations only.
- `src/workflow`/`src/jobs` are touched ONLY to emit recorder hooks at existing
  state-transition points (or, if cleaner, a recorder call site in the CLI/pipeline
  runner that already owns run lifecycle) — no logic moves into them.
- GUI untouched. QGIS untouched.

## Non-goals

- No new persistence format; store schema v1 stays (additive indexes only).
- No perceptual-hash near-duplicate implementation (evidence-supplied only).
- No auto-repair of stale runs: report truth, caller decides.
- No bootstrap/p-value machinery: paired comparison reports deltas + counts.
- No restructuring of mcp_server.cpp beyond registering the new tool table.
