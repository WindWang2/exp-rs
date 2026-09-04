# Root Causes — #726 verification at 6033505f86

Each issue item re-verified against the worktree code before fixing.

## Item 1 — Value-based destination exclusion ⇒ non-injective fingerprints ⇒ undeclared-file overwrite

- `task_center.cpp:2221-2232` (`taskExecutionFingerprintLocked`): every param
  whose *string value* equals `outputLayerPath` is erased from the hashed
  params. `{input:x, output:x}` hashes `params={}` + `inputs=[]`.
- `temporal_workspace.cpp:297-302` + `derivation_record.cpp:192-198`: the same
  value is excluded from the input scan.
- A later `{input:y, output:y}` in the same process produces the identical
  digest; `serveFromExecutionCache` (`task_center.cpp:2265-2273`) then
  `QFile::remove(y); QFile::copy(cached=x, y)` — y.tif replaced with
  x-derived bytes, task marked Completed.
- Root cause: the lossy erasure happens *before* hashing and keys on value
  equality instead of parameter *semantics*. The 256-bit digest cannot repair
  pre-hash information loss.

## Item 2 — Remote/VSI inputs escape identity instead of failing conservative

- `derivation_record.cpp:190`: `if ( !QFileInfo( trimmed ).isFile() ) continue;`
  silently drops every `/vsicurl/https://…` and raw `https://…` param — exactly
  the remote-COG sources the gdal source provider registers
  (`gdal_raster_source_provider.cpp:90-107`).
- Empty `paths` ⇒ the conservative failure loop
  (`temporal_workspace.cpp:306-318`) never fires ⇒ the step fingerprints with
  zero revision-stamped inputs, violating the contract at
  `task_center.cpp:2234-2236`.
- Re-registering new bytes over the stable remote path keeps the path
  identical ⇒ identical fingerprint ⇒ revision-1 pixels served for revision-2
  input. Silent wrong scientific output.
- Root cause: input collection gated on local-filesystem existence; no
  datasource classification (`QgsDataSourceResolver` was not consulted), so
  remote inputs are dropped rather than either identified or refused.

## Item 3 — Per-run revision bumps defeat chained convergence

- `rs_pipeline_runner.cpp:704`: `notifyUpdateOnReuse = true` for every step
  output; `data_manager.cpp:303-334` then unconditionally advances
  `revision().next()` on dedup hit.
- Run N+1's step B fingerprints `a.tif@k+1` ≠ stored `a.tif@k` ⇒ guaranteed
  miss ⇒ re-execute ⇒ bump again. Only first-wave steps can converge.
- The E2E masked this: `test_workflow_cache_e2e.cpp:244-267` registers outputs
  once and omits the per-run production registration its run-3 assertion
  claims to model.
- Root cause: revision advancement keyed on "registration happened", not on
  "the artifact's producing execution changed".

## Item 4 — `StepPlan::cacheHit` has no producer; cache entries are reaped by GC

- Only GC protection: `if ( plan.cacheHit ) continue;`
  (`artifact_gc.cpp:145`) — nothing ever sets `cacheHit` on a live plan (only
  checkpoint serialization at `workflow_run.cpp:189,247-248`).
- `ExecutionResultCache::cachedOutputPaths()` (`execution_fingerprint.cpp:436`)
  — documented as the GC protection seam — has **zero callers** in `src/`.
- Tracked/coordinator runs: `finalizeRunLocked` → `sweepRun`
  (`workflow_run_coordinator.cpp:328-340`) reaps consumed intermediates; the
  cache entry dangles; the next identical run self-heals to a miss. The cache
  cannot outlive a completed tracked run.
- Root cause: protection contract existed on paper only; no producer, no
  consumer.

## Item 5 — Downstream steps never fingerprint on worker threads (cold pipelines)

- Downstream admission runs inside `markTaskCompleted` →
  `processNextQueuedTasks` (`task_center.cpp:1478`) on the JobEngine **worker**
  thread (plain `std::thread` delivering the listener directly,
  `job_engine.cpp:608-626`).
- The catalog affinity guard (`task_center.cpp:2177-2178`) refuses
  fingerprinting off the catalog thread ⇒ fresh A→B stores a fingerprint for A
  only; B first becomes cacheable on the *second* submission. Every cold
  pipeline re-runs its whole chain one extra time.
- Root cause: fingerprinting was scheduled at admission time (thread varies)
  instead of submission time (thread stable, affinity holds).

## Item 6 — Grouped temporal composite caches a path nobody wrote

- Dispatch-time `outputLayerPath` is the declared bare path
  (`task_center.cpp:814/2025`); grouped mode (`period != "all"`) writes only
  `<base>_<label>.tif` files (`rs_temporal_composite_operator.cpp:130-138,
  351`) and the payload's `output` is the *first period's* path, with the full
  set in `outputs[]`.
- `storeOutputPath` (`task_center.cpp:1462-1473`) records the bare path;
  `lookupOutputPath`'s `QFile::exists` fails and erases the entry
  (`execution_fingerprint.cpp:401-407`) ⇒ grouped composites can never be
  served.
- Had such an entry been served, the payload carried `output` but no
  `outputs[]`, so `collectProducedOutputs`
  (`src/app/dialogs/temporal_analysis_dialog.cpp:588-604`) would auto-load 1
  of N period rasters.
- Root cause: the cache entry was a single `QString` output path instead of
  the execution's *result* (payload + produced artifact set).

## Lower-severity (verified, fixed in this pass)

- **Naked `remove + copy` serve** (`task_center.cpp:2270-2271`): non-atomic
  replacement, stale sidecars survive, torn reads by concurrent consumers,
  no rollback. Root cause: serve bypassed every transactional publish
  convention in the codebase.
- **Schema-only implementation proxy** (`task_center.cpp:2193-2216`): a
  behavior fix that does not change `schema()` serves stale artifacts until
  restart. No explicit cache-contract/platform version mixed into the
  implementation identity.
- **Destination-path poisoning** (found during this re-verification, not in
  the issue): `lookupOutputPath` validated only existence, so a path reused as
  the destination of a *different* fingerprint left the old fingerprint's
  entry serving the new bytes (p=1 → out.tif stored; p=2 rewrites out.tif;
  p=1 again → hit on p=2's bytes). Fixed by entry validation + per-path
  ownership eviction.
