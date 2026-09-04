# REVIEW LOG

Method: two independent adversarial review passes (concurrency+recovery,
cache-correctness+resources) over the full branch diff, findings fixed and
re-verified by the suites below.

## Pass 1 — Concurrency (findings -> fixes)

- [P1] gpu_plane evictStale freed VRAM of in-use sessions -> now recycles
  only released sessions (test_gpu_plane).
- [P2] fused bindings survived shutdownForTests -> cleared (test_task_center
  family via worker/fixture reset).
- [P2] worker host: grace deadline started at launch (any job >3s was killed
  instantly on cancel); cancel unobservable while worker silent; clean exit
  misreported as crash -> grace starts at cancel request, readFrame soft
  timeouts poll cancellation, clean exit reported as cancelled
  (test_worker_host cancel case).
- [P3] chunk_pipeline thread-creation exception safety — accepted limitation
  (threads created once before the loop; documented).
- Verified non-issues: queue wakeup semantics, terminal-final guards,
  flushPendingSignals lock inversion, W3 election window.

## Pass 2 — Cache/data correctness

- [P0] fused_chain producer cursor was function-scope static: a SECOND fused
  execution reused the exhausted cursor and wrote zero tiles while reporting
  success. Fixed with per-execution grid+cursor; test_fused_chain runs
  multiple fused executions in one process.
- [P1] fused chain plane-arity: NDVI-after-threshold chains would read
  nonexistent planes -> rejected at planning.
- [P1] pool-served entries could skip transfer when the original path still
  held bytes — serving bytes NOT vouched by the digest. In-place skip now
  only for in-memory (validated) entries.
- [P1] pool partial record sets were servable; recordExecution now proves
  completeness (objectCount marker + re-read) and lookups refuse partials.
- [P1] eviction was dead code (nothing trash-state): forgetExecution now
  trash-marks; puttmp staging excluded from usage accounting.
- [P2] updateContentDigest rewrote every version of an artifact; latest only.
- [P3] telemetry JSON escaping; LIKE backslash escaping; latestByPath rowid
  tie-break.

## Pass 3 — Resource/performance

- [P2] pool I/O under the global cache mutex and evict O(n²) walk:
  totalObjectBytes hoisted; full staging-outside-lock documented as a
  known limitation (single-writer trade-off, MEMORY_BUDGET rule kept).
- [P2] remote pool: per-URL buckets unbounded across URLs and 20ms poll on
  release — documented (bounded per URL; poll dominates remote I/O).
- [P3] catalog page() N+1 hydration for aliases/tags — documented; page
  size bounded at 500.

## Pass 4 — Recovery

- [P2] election key canonicalization collapsed distinct runIds (QString::remove
  strips all occurrences) -> exact prefix/suffix stripping.
- [P1] workspace_catalog removeAsset committed then failed (changes checked
  after the wrong statement) -> checked after the assets DELETE.
- Verified: W1 persist-before-dispatch, W2 size>0 resume validation, W3
  newest-wins election, archive-failure fallback leaves the checkpoint in
  place (recovery ignores Completed runs, so no re-execution).

## Environment note

Two test_workflow_cache_e2e CLI-subprocess cases (resume/list-runs across
processes) hang >90s in this sandbox WITH AND WITHOUT the workflow changes
(reproduced on the reverted tree) — treated as pre-existing environment
behavior, not a branch regression; all in-process paths of the same flows
are covered by test_workflow_recovery / test_workflow_persistence suites.
