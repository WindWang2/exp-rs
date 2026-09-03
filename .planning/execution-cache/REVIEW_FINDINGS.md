# Adversarial Review Findings — #726

Three independent review passes over the working diff (scientific correctness /
concurrency & lifetime / architecture). Every P0 and P1 was fixed and
re-verified; P2s were fixed when cheap or explicitly accepted with rationale.

## Review 1 — Scientific correctness (any false hit = P0)

| # | Sev | Finding | Resolution |
|---|-----|---------|------------|
| 1-1 | **P0** | `QgsDataSourceResolver::classify` defaults unknown datasource strings (`HDF5:"…"`, `netcdf:…`, `GTIFF_DIR:…`, `file://…`) to LocalFile; a non-existent string then escaped input identity entirely, so a subdataset input could be re-keyed without its content ever entering the fingerprint → false hit after the backing file changed. | **Fixed**: `looksLikeHiddenDatasource()` in `temporal_workspace.cpp` — an identifier-prefix + colon shape (the GDAL subdataset/driver-connection signature) is collected as a datasource candidate and must resolve to a registered asset or the step is uncacheable. Scientific values ("NDVI", "3", "mean", ISO datetimes) do not match the shape. Pinned by header contract; conservative cost: a deterministic op with a colon-bearing scientific value (e.g. `EPSG:4326`) stays uncacheable. |
| 1-2 | **P1** | A cache entry validated only its OUTPUT stats; a chained intermediate corrupted between producer completion and consumer execution poisoned the consumer's entry, and the producer's self-heal masked it (run 2 serves v2-derived bytes under a fingerprint that proves v1). | **Fixed**: `CachedExecution.inputSizes/inputMsecs` — `storeExecutionResultLocked` stats every chained producer path the consumer read; `declaredOutputStillValid` refuses the entry when any recorded input no longer matches. One extra miss per real rewrite, byte-exact convergence otherwise. |
| 1-3 | P2 | `isOutputVocabularyKey` (contains "output"/"result") would collide scientific params like `outputMode` if a tolerance op carrying them ever opts into determinism. | **Accepted + documented** on `isOutputVocabularyKey` and in the contract: today's deterministic surface carries only true destinations under those keys; ops with scientific output-named params must rename them or the schema-port refinement must land before opting in. |
| 1-4 | P2 | Dispatch verification did not re-check the chained producer's stamped payload fingerprint. | **Fixed**: `verifyDispatchFingerprintLocked` compares each `ChainedEdge.producerFingerprintHex` against the producer's stamped `executionFingerprint`; mismatch ⇒ drop. |
| 1-5 | P2 | Chained-input exclusion was by VALUE, so a literal key carrying a producer path escaped identity. | **Fixed**: exclusion is now by PARAMETER KEY (`chainedProducerKeys`); a literal key with the same path is revision-stamped. |
| 1-6 | P2 | Downstream fingerprint hashes the statically-resolved intermediate path, so relocating the producer's destination costs the downstream its hit (miss-only). | **Accepted**, documented in C8. |
| 1-7 | P2 | Sidecars transferred on serve but not stat-validated. | **Accepted** (cosmetic metadata), documented in C8. |
| 1-8 | P2 | Test gaps: size/mtime anti-poisoning branch untested; no `{scratch:O}`≠`{output:O}` pin; no chained-input validation test. | **Fixed**: `test_execution_fingerprint.cpp` now rewrites a file's bytes and asserts a miss, validates input-stat invalidation, and pins the output vocabulary + scratch-value hashing. |

## Review 2 — Concurrency & lifetime

Verified sound: global lock order (TaskCenter → cache mutex; GC-static → cache
mutex; JobEngine never holds its mutex into TaskCenter); single
launch/store/serve per task; monotonic task ids; canonical GC protection
matching; terminal-transition dedupe covering cancel-during-serve.

| # | Sev | Finding | Resolution |
|---|-----|---------|------------|
| 2-1 | **P1** | Serve TOCTOU: the cached source was validated only at lookup; a concurrent writer to the cached path between lookup and rename could publish wrong/torn bytes as a verified hit. | **Fixed**: `materializeCachedArtifacts` re-stats every staged source (against the entry's recorded expectations) after staging and before any rename; mismatch ⇒ abort ⇒ real execution. |
| 2-2 / 3-17 | P2 | `verifyDispatchFingerprintLocked` could erase `end()` if the maps ever desync. | **Fixed**: key-based `remove()`. |
| 2-3 / 3-5 | P2→P1 | `collectPayloadFilePaths` claimed ANY existing file in the payload (echoed inputs), which both over-protected inputs and let `mapProducedPath`'s substring stem rename collide artifacts. | **Fixed** (hardened): only output-vocabulary-keyed paths inside the declared output's directory are collected; stem mapping is prefix-based (`<stem>_`) and can never map a sibling onto the declared destination. |
| 2-4 | P2 | GC-vs-store TOCTOU (sweep deletes a just-claimed artifact) — conservative miss. | **Accepted**, self-heal covers it. |
| 2-5 | P2 | Completion-time payload walk + stat under m_mutex — bounded liveness cost. | **Accepted** (documented); moving it off-lock would race the terminal transition. |
| 2-6 | P2 | Same-fp concurrent serves shared one staging temp; Windows rename-over-existing fails. | **Fixed**: staging suffix isolated per serve (fp prefix + process-unique counter — the suffix no longer collides across serves); Windows fallback removes-then-renames (documented platform window). |
| 2-7 / 3-18 | P2 | enqueueTask-chained children whose parent completed first are uncacheable (fp consumed at producer completion) — fail-closed. | **Accepted** + documented in the contract (pipeline submissions fingerprint in topo order, which is the #726 scope). |
| 2-8 | P2 | shutdown() force-cancel skipped map cleanup; provider never uninstalled; post-reset catalog null. | **Fixed**: shutdown finalization removes all three per-task entries; `~TaskCenter` uninstalls the GC provider. Catalog-null reset is test-fixture business (shutdownForTests contract). |

## Review 3 — Architecture

Verified sound: no new layering edge (std::function provider only);
`QgsDataSourceResolver` is the single classifier (zero new scheme sniffing in
the classifier role); src/data stays Qt-Core-only; serialization of
`executionFingerprint` is backward/forward compatible; default-off invariant
untouched; `rs_pipeline_runner` diff limited to fingerprint plumbing (#727
resume logic untouched); no second publisher/resolver/engine/cache.

| # | Sev | Finding | Resolution |
|---|-----|---------|------------|
| 3-6 | P2 | `findOutputPathInParams` hand-rolled the vocabulary. | **Fixed**: shares `isOutputVocabularyKey`. |
| 3-10 | P2 | Legacy `storeOutputPath` wrapper produced entries that bypassed disk validation. | **Fixed**: wrapper stats the file so the same validation applies. |
| 3-2 | P2 | task_center.h now transitively includes jsoncpp. | **Accepted** (same library already linked; forward-declaring the resolver lambda's parameter would complicate the seam for marginal gain). |
| 3-7 | P2 | `.provenance.json` sidecars are outside the shared sidecar vocabulary — a stale provenance sidecar can survive a hit onto a previously-real destination. | **Accepted** (vocabulary is shared with GC; widening it would change GC sweep behavior beyond this fix) — documented. |
| 3-13 | P2 | Tasks reaching dispatch without enqueue/submitPipeline are now permanently uncacheable (conservative). | **Accepted**, documented. |
| 3-15 | P2 | Planning-doc drift (member name; convergence rule scope). | **Fixed** in the docs alongside this file. |
| 3-16 | P2 | `findByPath` is O(records) per call — pre-existing; the new path leans on it once per submission (previously once per scheduling pass — a net reduction). Canonical-index follow-up noted. |

## Post-fix verification

After the P0/P1 fixes: all targeted suites re-run green —
`test_execution_fingerprint` (16 cases), `test_workflow_cache_e2e` (10 cases
incl. CLI crash-resume subprocess), `test_task_center` (30),
`test_data_manager` (40), `test_temporal_workspace` (11),
`test_workflow_artifact_gc` (7), `test_output_committer` (13),
`test_workflow_incremental_cache` (6), `test_workflow_run_coordinator` (2),
`test_workflow_recovery` (6), `test_data_manager_reap` (13),
`test_data_manager_promote` (4). Full-tree Release build (-j2, clang, ccache)
completes with no errors.
