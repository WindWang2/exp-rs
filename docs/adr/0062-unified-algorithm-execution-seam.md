# ADR 0062: Unify Algorithm Execution — Bridge the AtomicAlgorithmRegistry to JobEngine

## Status
Accepted

## Context
The unified adapter layer (ADR 0012) built the *description/export/validation*
half: `AlgorithmDescriptor`, three `AtomicAlgorithmAdapter` implementations, the
`AtomicAlgorithmRegistry`, and the `ToolCallDispatcher`. The Agent exports
provider algorithms (`gdal:gdal_translate`, `otb:…`, `native:…`) through
`exportOpenAiToolDefinitions()` and `ToolCallDispatcher` resolves their ids — so
they are **visible** to the LLM.

But the *execution* half was never connected. `JobEngine::runOperatorJob`
resolved ids as **per-job executor → prefix executor → `RSOperatorRegistry::create`**
and reported "Unknown algorithm" when all three missed. There was no path from a
provider id to `ProviderAlgorithmAdapter::execute`, which was effectively dead
code (only the `py:` prefix path ever called an adapter's `execute`). The result:
provider algorithms were **visible to the Agent but not executable** when
submitted as jobs — a hard, reproducible failure.

A second gap: `ProviderAlgorithmAdapter::execute` used the simplified
`clone->run()` form (no project, no transform context, no explicit
`postProcess`), whereas the production `"processing:"` prefix executor in
`processing_job_adapter.cpp` ran the full `prepare → runPrepared → postProcess`
lifecycle with project/transform context. Routing provider ids through the
adapter as-is would have *downgraded* their execution.

## Decision
1. **Upgrade `ProviderAlgorithmAdapter::execute`** to the production lifecycle:
   `prepare()` → `runPrepared()` → `postProcess(true)`, with
   `QgsProject::instance()` + transform context and `postProcess(false)` cleanup
   on cancel/exception. It now mirrors `processing_job_adapter.cpp`'s body and
   raises `std::runtime_error` on failure (matching `RsOperatorAdapter::execute`'s
   exception-pass-through contract), rather than returning an `error` JSON.
2. **Add a registry fallback seam to `JobEngine`**: `setFallbackExecutor(JobExecutor)`.
   Resolution order in `runOperatorJob` becomes
   **per-job → prefix → `RSOperatorRegistry` → fallback**. The fallback is tried
   *after* the native RSOperator registry misses, so `rs:`/`opencv:` operators
   keep their first-class path (full cancel/log/progress `RSOperatorContext`)
   and never reach the adapter.
3. **Wire the fallback at app boot** (`main.cpp`, after `AlgorithmEngine::initialize()`
   populates the registry): look up the id in `AtomicAlgorithmRegistry::findAdapter`,
   run `adapter->execute(params, progressBridge)`, and bridge `int percent → ctx.reportProgress`.
   This lives in the app layer (not `sicnu_processing`) because `sicnu_jobs` is Qt-free
   and `sicnu_processing ← sicnu_operators ← sicnu_jobs` — a `sicnu_processing → sicnu_jobs`
   link would close a cycle. App-layer wiring matches the existing
   `registerProcessingJobExecutor()` / `"py:"` registration pattern.

## Consequences
- Provider algorithms (`gdal:`/`otb:`/`native:`) submitted as jobs now execute
  instead of failing "Unknown algorithm". The Agent's exported tool catalog is
  executable end-to-end.
- `rs:`/`opencv:` operators are unaffected — they resolve at
  `RSOperatorRegistry` before the fallback fires, keeping the full
  cancel/log/progress context.
- `py:` prefix executor is unchanged (it still marshals to the main thread);
  the fallback is a strictly-later resolution step.
- `ProviderAlgorithmAdapter::execute` now throws on failure; callers that
  previously received an `error` JSON see an exception instead. The only
  caller path (now JobEngine via the fallback) maps exceptions to
  `JobState::Failed`, so the job panel surfaces errors correctly.
- The `"processing:"` prefix executor remains for now; its body now duplicates
  the upgraded adapter. Folding `processing:foo` onto the adapter (and deleting
  `processing_job_adapter`'s duplicate JSON converters) is a follow-up slice.
- New `tests/test_job_engine.cpp` cases cover the seam (stub adapter → Succeeded,
  unknown id → Failed, progress bridge → progress == 1.0) without requiring a
  real QgsProcessingAlgorithm.
