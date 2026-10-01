# R6 BASELINE — Persistence / Sidecar / Atomicity / Crash-Consistency

- origin/master @ `1e28de867` (2026-09-30): per-process test isolation (#1414).
- Merged into baseline (post-fix reality, NOT re-fixed here): #1403 (publishStagedGroup
  crash window + metadata-patch stale digest), #1404 (fence key platform-true anchor),
  #1405 (store invariants: approval/verdict, split-insert txn, schema gate before DDL),
  #1406 (reproduction bundles crash-durable, Exact requires pinned provenance).
- Open PRs: 0. Open issues: #1395, #1394 (ADR 0166 sidecar write API — focus), #1393,
  #1392, #1389, #1387 (digest authority fork — explicitly OUT of scope here).
- Worktree: `../exp-rs-r6-persistence`, branch `hardening/r6-persistence-sidecar-atomicity`.

## WP-A census conclusions (subagent #1, full report in EVIDENCE.md)

Durability facts: QSaveFile = rename-atomic but NO fsync. `atomic_fs` (geospatial) =
fsync + Windows ReplaceFileW/MoveFileExW-WRITE_THROUGH + EXDEV fallback — dataset
publish authority. `platform/portable.h` = syscall authority (syncFileUtf8,
claimExclusiveUtf8, syncDirectoryBestEffortUtf8).

Duplication map: 8 temp-name implementations, 6 fsync variants, 5 digest variants,
2 last-good shapes (mission vs plugin), 3 group-publish shapes, 2 fixed-shared-`.tmp`
hazards (plugin_discovery storeIndex, cli_batch_runner writeResultIndex).

Top crash-consistency risks (ranked):
1. `RsPostProcess::saveClassMetaData` (analysis/classification/rs_post_process.cpp:770)
   — plain open+truncate; kill mid-write destroys `<raster>.class.json` silently;
   reader never checks version; no recovery copy. Called from classification main
   window (4 sites) + rs_recode/rs_majority operators.
2. `PluginRegistry::saveUserIndex` (sdk/exprs/plugin_registry.cpp:2181) —
   ofstream+rename, no fsync, silent error surface; torn index resets user disable set.
3. `plugin_discovery storeIndex` :141 — fixed shared `<path>.tmp`, no short-write guard.
4. `cli_batch_runner writeResultIndex` :104 — fixed `<path>.tmp`, torn result ledger.
5. `workflow_checkpoint saveCheckpoint` Windows lane :116-120 — no FlushFileBuffers.
6. `harness evidence atomicWrite` :43-67 — QSaveFile, `file.write()` unchecked.
7. `processing output_committer` :130-200 — custom publish-then-swap without fsync;
   kill window between primary→.old and .new→primary leaves NO stable primary.
8. study/studio/registration/lab-batch QSaveFile quartet — copy-pasted, divergent
   error mapping.
9. `range_cache_disk putBlock` — own tmp+fsync idiom (cache-only, benign loss).
10. `session_journal writeFileAtomic` :56-142 — complete but parallel authority.

The GOOD pattern (generalization target): `mission_runtime_store` — single authority +
decode-probed last-good + poisoned-state refusal + typed problem codes + version
refusal. Its own write boundary is still QSaveFile (no fsync).

Readers resilience matrix today: mission family complete; classification has NO
recovery copy, NO version gate, silent skip on corrupt; workflow/batch/lab degrade to
"fresh start"; stores have sqlite WAL + #1390 read-only newer-schema gate.

## Build lane

Windows MSVC 14.38 / Ninja / `-j2` hard cap / CTEST_PARALLEL_LEVEL=1. Configure via
`r6_dev.cmd` (vcvars64 + Qt 6.8 prefix + vcpkg manifest mode sharing the main
checkout's `build-dev/vcpkg_installed` — read-only reuse of compiled deps).
