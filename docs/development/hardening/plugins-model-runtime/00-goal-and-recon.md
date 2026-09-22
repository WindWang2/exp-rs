# Hardening 15/20 — plugins / model runtime: goal + recon baseline

Track slug: `plugins-model-runtime`. Branch: `hardening/plugins-model-runtime` (worktree
`../exp-rs-hardening-plugins-model-runtime`), cut from `origin/master` = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`.

## Startup facts (verified 2026-09-23, overrides prompt seed where newer)

- master `a9dc33fa7` (same as recon seed; #1236 Scene Suitability Assessor just merged).
- Open issues: 0.
- Open PRs: #1237 (teaching cockpit), #1238 (experiment studio), #1239 (teaching admin),
  #1240 (science context broker), #1241 (agent ops center), #1242 (geospatial fabric),
  #1243 (spectral), #1244 (temporal). **None touches `src/sdk/exprs`, `src/plugins`,
  `src/operators/runtime`** — this track's files are conflict-free. Shared-file hotspots to
  keep clean: root `CMakeLists.txt`, `tests/CMakeLists.txt` (touched by #1237-#1241; we only
  append new test source registrations if needed — we do NOT need new test executables, so
  `tests/CMakeLists.txt` stays untouched), `src/app/main_window*` (untouched by us).
- Branches: `agent/flash-*`, `rs14-unified-verifier` — old-baseline clue mines only; nothing
  cherry-picked. `hardening/temporal|spectral|geo-*` owned by sibling tracks.

## Domain map (call/ownership graph summary)

```
PluginRegistry (src/sdk/exprs/plugin_registry.cpp, gRegistryMutex recursive, mReloading per-id)
  installOrUpgrade → stage/validate → drain → PluginPackage::install [cross-process flock, #1226]
                   → landed id+version check → refresh → load → commit | snapshot restore
  unload/unloadAll → arm drain → wait idle → drop lock → revoke → loader unload  (mDiagnostics!)
PluginPackage (src/sdk/exprs/plugin_package.cpp): staging + rename swap, checksum verify,
  CrossProcessInstallLock (.locks/<id>.install.lock) — install() only (#1226), uninstall() NONE
PluginHostProcessRuntime (src/plugins/host/plugin_host_process_runtime.cpp): mSessions map,
  respawn policy 3/60s; PluginHostProcessSession::spawn → fork+setpgid+RLIMIT_AS / job object;
  awaitHandshake waits hello; kill ladder cancel→grace→kill(-pgid)
ModelExecutionService (src/operators/runtime/model_execution_service.cpp): runModelInference seam
  → task-intent gate → ensemble route | runtime readiness → registry.acquireWithFallback
  (VRAM ledger + LRU 2 + pressure valve, #1160 deleter release)
  → DetectionTileEngine | TileInferenceEngine(raster/multi-input/scene) → atomic publish + prov sidecar
ModelEnsemble (src/operators/runtime/model_ensemble.cpp): resolve members → acquire sessions →
  runMembersBounded (budget semaphore, fail-fast) → per-lane combine → stage/rename publish +
  publishSidecar (fault point ensemble.publish_sidecar); DetectionPublishGuard backs up sidecars
```

## Confirmed defects (all personally verified at source on a9dc33fa7)

### Plugin side

| id | site | defect | fix | oracle |
|----|------|--------|-----|--------|
| P-A | `plugin_package.cpp:807-863` | `uninstall()` takes no `CrossProcessInstallLock`; concurrent cross-process install+uninstall both "succeed" and destroy each other's result (install() comment itself admits the swap race) | take the same lock before `removeTree` | external flock holder → uninstall must fail typed (RED: succeeds today) |
| P-B | `plugin_package.cpp:321` | POSIX `flock(LOCK_EX)` blocks forever (Windows path fails fast) — a stuck holder hangs installs indefinitely | `LOCK_NB` + bounded retry (~5 s), typed `ResourceMissing` on timeout | bounded-acquire test asserts failure ≤ deadline; RED(POSIX)=hang, documented |
| P-C | `plugin_registry.cpp:1992-2008` | `unloadAll()` busy loop derefs `record()` pointer + writes state/diagnostics **outside** the registry lock — the header's own #943 comment defines this exact pattern as UAF | collect under lock; fix up in a second locked pass; keep `cancelPluginDrain` lock-dropped (AB-BA #1156) | contention stress test + TSan sabotage evidence |
| P-D | `plugin_registry.cpp:875-881, 971, 1977` | unload legs append to shared `mDiagnostics` with lock dropped (`PluginDiagnosticLog::add` = unsynchronized push_back; `reload()`'s own comment forbids this) | local `PluginDiagnosticLog` + `merge` under lock (idiom already used by `load()`:748-757) | same stress test / TSan |
| P-E | `plugin_registry.cpp:2092` | `saveUserIndex()` uses fixed `path + ".tmp"` — two processes interleave writes into one temp → torn/empty index silently resets user's disable set | pid-unique temp + remove on failure | pre-create `.tmp` as directory → setEnabled must still persist (RED: silently skipped today) |
| P-F | `plugin_host_session.cpp:126-142` | POSIX: no worker-binary pre-check (Windows has one) — missing worker burns the full 15 s handshake then reports misleading `IpcProtocolError` | mirror Windows pre-check with `access(X_OK)` → typed `HostProcessUnavailable` | spawn with missing path asserts typed code + latency ≪ timeout (RED: wrong code today) |

### Model runtime side

| id | site | defect | fix | oracle |
|----|------|--------|-----|--------|
| M-A | `detection_tile_engine.cpp:514-528` | detection lane drops `preprocess.offset` entirely; with `linear, scale=1.0, offset≠0` NO normalization runs at all (gate `scale != 1.0` only). Raster/scene lanes apply `v*scale+offset` (#646/Platform 10.0) | mirror raster gate + formula | capturing provider asserts blob mean (RED: offset ignored) |
| M-B | `tile_inference_engine.cpp:2570-2576` + ensemble scene combine | `std::clamp(NaN)==NaN` / logit sum poisons → NaN probabilities PUBLISHED; weighted_vote elects class 0 for a NaN member | finite gate in `classifyScene` (shared core) → typed `ComputationError` | NaN-logit member → ensemble must throw (RED: publishes NaN + success) |
| M-C | `model_ensemble.cpp:814/984/1306` | all-zero-weight refusal fires only AFTER every member fully executed (VRAM reserved, full raster passes) though weights are static manifest data | hoist weight-sum check to right after `resolveEnsembleMembers` (single shared point: all 3 lanes call it at :746), remove the 3 post-run copies | counting provider + 0/0 weights → forwards==0 + typed refusal (RED: forwards==N) |
| M-D | `tile_inference_engine.cpp:2247-2262, 3906-3916`; `model_ensemble.cpp:1749-1767` | sidecar-publish failure restores the product but the OLD sidecar was already `remove`d — verified product downgraded to MissingSidecar (detection guard does this right) | rename old sidecar to `backupPath + ".prov.json"`, restore on failure, remove on success (crash invariant unchanged: missing, never stale) | dir-at-stage injection (single-model) / ArmedFault (ensemble raster): after failure product AND sidecar intact (RED: sidecar gone) |
| M-E | `model_execution_service.cpp:205-212` | dead duplicate of the task-intent gate (superseded by #1226's pre-route gate at :167-174) | remove dead copy | covered by existing suites |
| M-F | `model_ensemble.cpp:1101 vs 1114-1132` | scene classification artifact: `backend/device/model_ref` added AFTER publish — on-disk doc lacks them (returned payload ≠ durable artifact) | build the enriched doc before `publishClassificationArtifact` | on-disk doc asserts backend/model_ref (RED: absent) |

## Deliberately NOT done in this slice (classified)

- **Owned by other tracks**: general scheduler/resource reservation outside model-specific
  lifecycle (Track 05); teaching/app shell files (PR #1237/#1239 own them).
- **Not reproducible / needs real platform environment**: NVML probe under registry mutex
  (`model_runtime.cpp:1293-1297`) — real GPU needed to measure; recorded, no speculative fix.
- **Needs design agreement (recorded as evidence only)**: upgrade TOCTOU landed-bytes gate
  compares only id+version (`plugin_registry.cpp:1597-1613`); cross-process `last-good-`
  snapshot sweep keyed on process-local live ids (`plugin_snapshot.cpp:790-794`); single-model
  detection products publish no provenance sidecar (verifier deterministically MissingSidecar);
  plugin model-runtime adapter does not serialize `infer` across shared sessions
  (`plugin_model_runtime_bridge.cpp:119`) — contract text vs enforcement needs an owner call;
  poisoned-worker drain under sustained traffic; non-operator proxies bypass the drain barrier.
- **Explicit future direction**: none added.

## Slice discipline

One PR, conventional commits per batch (plugins / model / tests). Every behavior fix bound to
the oracle above. Key oracles run twice consecutively before PR. No new executables in
`tests/CMakeLists.txt` (all oracles extend existing test files). Build `-j2` only.
