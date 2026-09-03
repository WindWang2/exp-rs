# Implementation Plan — #726 execution-cache correctness

Branch: `zcode/execution-cache-correctness` (worktree `../exp-rs-wt-execution-cache`).
Constraint recap: cache stays OFF by default; #727 (resume) untouched; changes
to `rs_pipeline_runner.cpp` limited to fingerprint/registration plumbing.

## 1. `src/data/execution_fingerprint.{h,cpp}` — contract + cache entry

- `TaggedDerivationInput` += `QString producerFingerprint` (chained identity
  token; hashed/compared like the other fields, `;pfp=` in the canonical form).
- `kExecutionFingerprintContractVersion = 2` constant (bump on any semantic
  change; documented next to the platform version constant `1.0`). Callers mix
  it into the implementation-version hash (fix 8).
- Cache entry upgrade:
  ```cpp
  struct CachedExecution {
    QString declaredOutputPath;      // producing run's destination
    QStringList producedArtifacts;   // every produced file (incl. declared)
    QJsonDocument resultPayload;     // full producing payload
    qint64 sizeBytes; qint64 lastModifiedMsec;  // declared-output validation
  };
  storeExecution(fp, CachedExecution) / lookupExecution(fp)
  ```
  `lookupExecution` self-heals (missing file ⇒ erase + miss) and validates
  existence/size/mtime of the declared output + existence of every artifact.
  Storing a new fp's claim on an already-claimed path evicts the other fp's
  entry (path ownership — anti-poisoning).
- `cachedArtifacts()` replaces `cachedOutputPaths()` as the GC-facing set
  (declared + artifacts of all entries). Legacy path API kept as thin wrappers
  where tests used it, migrated.

## 2. `src/processing/algorithms/temporal/temporal_workspace.cpp` — input identity

`fingerprintInputsForOperatorParams` rewrite:

- Collect path-like param candidates **without** the `isFile()` gate
  (placeholder `$…` refs excluded — they arrive as chained identities).
- Classify each candidate with `QgsDataSourceResolver` (core lib, already
  linked into `sicnu_processing`):
  - `LocalFile`: must exist on disk AND resolve to a registered asset
    (canonical match) — else `fail()` (uncacheable).
  - `RemoteUri` / `GdalVirtualPath` / `OgrConnectionString`: resolve through
    `DataManager::findByPath` against the registered canonical source —
    registered ⇒ `(assetId, revision)`; unregistered ⇒ `fail()`. Never
    omitted.
- The destination is **no longer value-erased** from the scan: a path under a
  non-output key (in-place `{input:x, output:x}`) resolves with its revision;
  output-vocabulary keys are skipped by key (the collector skips keys matching
  the platform output vocabulary).
- Collections / inline scenes: unchanged semantics (already fail-conservative).

## 3. `src/processing/framework/task_center.{h,cpp}` — submission-time chained fingerprints

- New per-task state: `m_taskFingerprints` (exists) + `m_taskFingerprintParams`
  (statically-resolved param snapshot for dispatch verification) +
  `m_taskProducers` (param key → upstream producer identity source).
- `computeAndRecordSubmissionFingerprintLocked(...)` (called at
  **enqueue/submit** time only; no `m_taskProducers` member — the chained
  wiring is a local map folded into the fingerprint, and the per-task
  `m_taskChainedEdges` record is only the producer paths/hex needed for
  input-stat validation and dispatch re-verification):
  1. gate on cache enabled + catalog wired + **submitting thread == catalog
     thread** (affinity moves here from admission),
  2. determinism gate + implementation identity =
     `SHA256(schema ‖ contractVersion ‖ platformVersion)` (fix 8),
  3. statically resolve `$step.port` placeholders from declared upstream
     output paths, recording chained wiring; params hashed with **key-based**
     output-vocabulary exclusion (fix 1),
  4. inputs = external/scene/collection resolution (§2) + chained producer
     fingerprints (`;pfp=`) — upstream fp invalid ⇒ downstream invalid,
  5. store `(fp, paramSnapshot)`.
- Pipeline steps fingerprinted in `submitPipeline`'s topo loop (upstream fp
  available). `enqueueTask` covers single/submitJob tasks (chained via parent
  tasks' fps when a placeholder references them).
- Admission (`processNextQueuedTasks`): after `applyPlaceholdersForTask`,
  **verify** the substituted parameterMap equals the stored snapshot; mismatch
  ⇒ drop the fp (conservative miss). No DataManager access on any thread here.
- `markTaskCompleted`: stamp `payload["executionFingerprint"]` (hex) on every
  fingerprinted completion (real run and hit alike); store the **execution**
  (`CachedExecution`) built from the task's outputLayerPath + produced
  artifacts extracted from the result payload (existing files only).
- `serveFromExecutionCache` rewrite (fixes 6+7):
  `lookupExecution` → materialize each artifact with same-directory temp +
  `QFile::rename` atomic replacement, sidecar refresh using the GC sidecar
  vocabulary, any failure ⇒ `false` ⇒ real execution; restore the full stored
  payload with run-specific paths rewritten (declared path + every produced
  path mapped by directory/stem substitution) + `cache`/`cachedFrom` markers;
  `markTaskCompleted(results, payload)` so consumers see a real-run-shaped
  payload.
- TaskCenter ctor installs `ArtifactGC`'s protection provider.

## 4. `src/data/{derivation_record.h,cpp}`, `src/data/data_asset.h` — fp on derivations

- `DerivationRecord` += `QString executionFingerprint` (additive JSON field,
  backward-compatible parse). `makeTaskDerivation` unchanged; callers stamp.

## 5. `src/data/data_manager.cpp` — revision convergence (fix 3)

> Scope note (post-review): the same-fingerprint silent-reuse engages on the
> CLI pipeline publisher (which stamps the fingerprint). The GUI/tool-call
> commit paths do not yet stamp fingerprints and keep the #687 bump
> semantics — conservative, tracked as follow-up.

- `RegisterRequest` += `QString executionFingerprint`.
- Dedup-hit decision becomes:
  ```cpp
  structureDiffers          → update + bump (unchanged)
  same fp re-publication    → silent reuse (no bump, no assetChanged)
  !notifyUpdateOnReuse      → silent reuse (unchanged)
  otherwise                 → update + bump (unchanged)
  ```
  where *same fp re-publication* = request carries a fingerprint equal to the
  record's attached derivation's fingerprint and structure is unchanged.

## 6. `src/cli/rs_pipeline_runner.cpp` — minimal registration plumbing

- `registerStepOutputs` reads the task payload's `executionFingerprint` and
  passes it to `registerOutputAsset` → `RegisterRequest` +
  `DerivationRecord`. No resume-logic changes.

## 7. `src/workflow/artifact_gc.{h,cpp}` — protection seam (fix 4)

- `ArtifactGC::installProtectedArtifactProvider(std::function<QStringList()>)`
  (process-wide, mutex-guarded) + `protectedArtifacts()`.
- `inspectReapable` skips any candidate whose canonical path is in the
  protected set (in addition to the legacy `plan.cacheHit` check).
- TaskCenter installs the provider wired to
  `ExecutionResultCache::instance().cachedArtifacts()`.

## 8. Tests

See CACHE_TEST_MATRIX.md. New cases in `test_execution_fingerprint`,
`test_workflow_cache_e2e` (production-faithful per-run registration loop,
cold A→B→C single-run convergence, grouped composite, destination
materialization, remote fail-conservative, in-place identity),
`test_data_manager` (fp-equality no-bump / fp-change bump / structure bump),
`test_workflow_artifact_gc` (cache-owned survival, invalidate releases).
