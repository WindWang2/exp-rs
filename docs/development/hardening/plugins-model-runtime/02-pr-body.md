# fix(plugins,model): hardening 15/20 — cross-process uninstall lock, registry lock discipline, detection offset parity, scene NaN gate, ensemble fail-fast, sidecar rollback integrity

## Goal

Hardening campaign track 15/20 (plugins / model runtime / ensemble lifecycle, isolation and
recovery): close the highest-confidence correctness and integrity defects that are provable
on current master, each bound to a regression oracle that fails on the old implementation.
Optimization-only; no new product surface, no second authority anywhere.

## Recon baseline

- Cut from `origin/master` = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01` (post-#1236).
- Open PRs #1237–#1244 inspected: none touches `src/sdk/exprs`, `src/plugins` or
  `src/operators/runtime`; this PR touches zero shared-shell files (no root/app CMake, no
  main_window, no `tests/CMakeLists.txt`).
- Open issues: 0. Old branches (`agent/flash-*`, `rs14-unified-verifier`) treated as clue
  mines only; nothing ported.
- Already-fixed history respected: #1226 (P3 gate/lock/ensemble batch) and #1175 (labels
  sidecar) are built upon, not re-implemented.
- Full status matrix + call/ownership graph: `docs/development/hardening/plugins-model-runtime/00-goal-and-recon.md`.

## Fixes (each: root cause → minimal fix → oracle)

### Plugins

1. **`PluginPackage::uninstall()` takes the cross-process install lock** (`src/sdk/exprs/plugin_package.cpp`).
   `install()` has held `.locks/<id>.install.lock` since #1186/#1226 (its comment: "without
   this lock two processes can still race the final swap"), but `uninstall()` ran bare: a
   concurrent CLI uninstall could delete a just-committed install while both processes
   reported success. Uninstall now acquires the same lock after its read-only gates.
2. **POSIX lock acquisition bounded** (`LOCK_EX|LOCK_NB` + 5 s retry, typed
   `ResourceMissing` timeout). Windows `CreateFileW` already failed fast; POSIX
   `flock(LOCK_EX)` hung forever on a stuck holder (SIGSTOP'd CLI, hung FS). Both platforms
   now fail with the same typed diagnostic.
3. **`unloadAll()` no longer dereferences `record()` pointers outside the registry lock**
   (`src/sdk/exprs/plugin_registry.cpp`). The busy-plugin fixup loop read/wrote record state
   and per-record diagnostics through a pointer handed out by `record()` (lock released on
   return) — the header's own #943 comment defines exactly this pattern as a use-after-free
   across a concurrent `refresh()` (which reallocates `mRecords`). Bookkeeping now runs in a
   pass that holds `gRegistryMutex`; `cancelPluginDrain` stays lock-dropped (#1156 AB-BA).
4. **Unload teardown diagnostics merged under the lock.** `unload()` (both legs) and
   `unloadAll()` appended to the shared `mDiagnostics` (`PluginDiagnosticLog::add` is an
   unsynchronized `push_back`) while the lock was deliberately dropped; `reload()`'s own
   comment forbids exactly that. Now collected into a local `PluginDiagnosticLog` and
   `merge`d under the lock — the idiom `load()` already uses.
5. **`saveUserIndex()` uses a process-unique temp file.** The fixed `<index>.tmp` let two
   processes interleave `ofstream` writes into one file and rename a torn document over
   `plugins.index.json`, silently resetting the user's disable set. Temp is now
   `<index>.tmp.<pid>` (same per-pid idiom as package staging/snapshots).
6. **POSIX worker-binary pre-check in `PluginHostProcessSession::spawn()`.** Windows
   pre-checks the worker path and fails typed `HostProcessUnavailable`; POSIX did not, so a
   missing worker burned the full 15 s handshake timeout and reported a misleading
   `IpcProtocolError` (× the 3-attempt restart budget). POSIX now mirrors Windows via
   `access(X_OK)`.

### Model runtime

7. **Detection lane preprocess now matches raster/scene lanes** (`src/operators/runtime/detection_tile_engine.cpp`).
   The gate tested only `scale != 1.0`, so `normalize:"linear", scale:1.0, offset≠0` fed the
   model RAW pixels, and any declared offset was dropped (`v *= scale`, no `+ offset`).
   Raster/scene lanes apply `v*scale+offset` (#646/Platform 10.0); the detection lane now
   does the same.
8. **Non-finite scene scores fail closed** (`src/operators/runtime/tile_inference_engine.cpp`,
   `classifyScene`). `std::clamp(NaN)==NaN` and the logit softmax pass NaN through; the
   scene lane published NaN probabilities verbatim, and under `weighted_vote` a NaN member
   silently elected class 0 (NaN never wins a `>` comparison). `classifyScene` is the shared
   core for the single-model writer AND every ensemble member, so one typed
   `ComputationError` gate protects both lanes — matching the raster lane's NaN poisoning
   and WBF's non-finite box gate.
9. **All-zero-weight ensembles refuse BEFORE any member session is acquired or run**
   (`src/operators/runtime/model_ensemble.cpp`). The refusal is a static manifest property
   but fired only in each lane's combine pass — after VRAM reservation and full member
   execution. Hoisted to a single authority right after `resolveEnsembleMembers` (all three
   lanes); the three post-run copies are removed. Message and error code unchanged.
10. **Sidecar-publish rollback restores the previous product WITH its sidecar** (single-input
    `tile_inference_engine.cpp`, multi-input `tile_inference_engine.cpp`, ensemble raster
    `model_ensemble.cpp`). The old code `remove`d the old `.prov.json` before the new one
    landed and never restored it: a sidecar failure downgraded a verified product to
    `MissingSidecar`. The old sidecar is now PARKED next to the product backup before the
    swap and restored on any failure (removed only on success). Crash invariant preserved
    and actually tightened: the old code removed the sidecar only AFTER the product swap
    (contradicting its own comment), leaving a stale-sidecar crash window; parking before
    the swap means a crash can only ever leave a MISSING sidecar, never a stale one. The
    detection `DetectionPublishGuard` already behaved this way.
11. **Scene classification artifacts carry their identity fields on disk** (ensemble +
    single-model). `backend`/`device`/`model_ref`/`ensemble_members` were added to the
    payload only after the publish, so the durable document silently lacked them. Now built
    before `publishClassificationArtifact`.
12. **Dead code removed**: the duplicated task-intent gate in `model_execution_service.cpp`
    (superseded by #1226's pre-route gate, unreachable for every path).

## Dedup record

- #1237–#1244 (all open PRs): no file overlap with this branch (verified via `gh pr diff
  --name-only` on 2026-09-23).
- #1226, #1175: already-merged fixes built upon; the one behavioral delta is deliberate
  (item 9 moves #1226's detection all-zero-weight refusal before execution; message
  unchanged).
- Not done here (classified): see `00-goal-and-recon.md` — NVML-probe-under-lock (needs real
  GPU to justify), upgrade landed-bytes TOCTOU / cross-process last-good sweep / single-model
  detection sidecar / plugin-adapter infer serialization (recorded with evidence, need design
  agreement or platform access), teaching/app-shell files (owned by #1237/#1239).

## Test evidence

All oracles extend EXISTING test files (no new executables, no `tests/CMakeLists.txt`
change). RED evidence per fix: revert only the binding source file, incremental rebuild,
run the oracle on the old implementation — table with the exact failing assertions in
`docs/development/hardening/plugins-model-runtime/01-test-ledger.md` and the PR comment
summary. The bounded-acquire oracle is demonstrated RED via `timeout` (the hang IS the bug).

Full targeted suites (all with existing tests): `test_exprs_plugin_system`,
`test_exprs_plugin_loader`, `test_plugin_host_process`, `test_model_ensemble`,
`test_model_tasks`, `test_ensemble_scene`, `test_ensemble_detection`,
`test_ensemble_parallel` — key oracles run twice consecutively.

## Known limitations

- `IPluginModelRuntimeV1` adapters still rely on the documented "implementations serialize
  forward passes" contract; enforcing it in the bridge is recorded as a design decision,
  not silently changed.
- Single-model detection products still publish no provenance sidecar (verifier reports
  `MissingSidecar` deterministically — fail-closed); wiring it is a scoped follow-up with
  the same publish-contract pattern as the ensemble detection lane.
- Cross-process `last-good-` snapshot sweep still keys on process-local live ids (dev-mode
  rollback only); needs a cross-process liveness attribution decision.

## Rollback

Pure revert of the branch. No schema, serialization, or on-disk format changes: the only
on-disk delta is the new pid-suffixed index temp file (never read back) and the previously
garbage `.tmp` name is no longer touched.
