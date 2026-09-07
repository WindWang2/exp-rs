# REVIEW_LOG — Harness 4.0

Six adversarial review passes, run against the completed implementation
(Windows/MSVC local lane). Status: COMPLETE. Result: **P0 = 0, P1 = 0**;
P2 items accepted with evidence below.

## R1 Architecture — PASS

- Harness lives in `sicnu_agent`, exposed as `harness:` SpatialTools through
  the single catalog; no agent runtime, planner, or memory copied from Pi.
- Single engine confirmed: the only execution bridge is
  `compilePlanToWorkflowJson` → `WorkflowRunCoordinator::startTrackedPipelineJson`
  (`grep`-verified: no second submitPipeline caller in the harness).
- FIXED during review: harness tool registration used static guards that
  would not re-register after `SpatialToolRegistry::reset()`; now naturally
  idempotent + reset-safe (registerTool duplicate rejection).

## R2 Correctness — PASS

- FIXED: recipe token substitution looked up `slotPaths["$slot.path"]`
  verbatim — slot keys never contain the `.path` suffix, so every slot
  binding substituted to "". Found by the NDVI eval.
- FIXED: recipe gate semantics — declared `inputs` were never copied into
  the instantiated plan, so the engine ran downstream steps concurrently
  (probe proof: threshold's fileExists fired before difference's
  createOutputTiff); inputs are now carried and gate-dropped wiring is
  filtered (also fixes dropped-upstream references when gates close).
- FIXED: `tool_taxonomy.cpp` kOverrides declared 90 entries with 84 written
  → six nullptr tail entries segfaulted `taxonomyForTool` (size made exact
  + null guard); same pattern checked in `tool_manifest.cpp` (39/39 ok).
- Preflight CRS facts read the `{authid, wkt}` object form the inspect
  tools emit (string form accepted too).
- Plan compiler output verified against `workflowDefinitionFromJson` in the
  NDVI eval (parse + steps + operator ids).

## R3 Concurrency / lifecycle — PASS

- FIXED (finding, not a defect): `sicnu_task_center` is a static library
  linked into both `sicnu_agent.dll` and the test exe → two singleton sets.
  Production is single-instance (mcp_server compiles into the app). The
  eval suite was corrected to drive the tool surface only (which is
  self-consistent inside the DLL) instead of mixing singleton instances.
- run_status polls only coordinator state; auto-resume is bounded to one
  attempt per call and reuses the engine's resumeRun (completed steps with
  valid outputs are never re-executed).

## R4 Performance — PASS

- Measured (benchmarks/harness-token-budgets.json, opt-in dump
  SICNU_BENCH_OUT): context 2.4 KiB (cap 256 KiB), error catalog 1.7 KiB
  (cap 8 KiB), 50-tool manifest page 12.5 KiB (cap 64 KiB).
- Entity resolution is a linear scan over DataManager assets, bounded by
  the catalog (100k-asset scale is a Platform-3.0 tested contract).

## R5 Cross-platform — PASS (with repairs)

- FIXED (pre-existing master defects, required for any Windows build):
  - `src/core/project/qgsproject.cpp`: POSIX directory fsync guarded
    `#ifndef Q_OS_WIN` (Windows cannot `::open` a directory; unguarded
    `O_RDONLY`/`open` broke the compile).
  - `src/sdk/exprs/plugin_validator.cpp`: `S_ISREG` fallback define.
  - `src/sdk/exprs/*`: new `msvc_posix_shim.h` (dirent / dlfcn / lstat /
    chmod / mkdir over Win32) wired into plugin discovery/package/loader/
    registry and plugins/framework/external_tool_operator.
  - `src/workflow/workflow_run_coordinator.h`: `class AlgorithmTaskInfo`
    forward declaration vs `struct` definition — MSVC name mangling
    distinguishes class-key → LNK2019 in the moc; aligned to `struct`.
  - `src/agent/interaction_tool_registry.h`,
    `src/data/governance/workspace_service.h`: `struct AssetSnapshot`
    forward declarations vs `class` definition — same MSVC mangling trap.
  - `src/stubs/Qsci/qsciscintilla_stub.cpp`: define the three
    `staticMetaObject` symbols Q_OBJECT's inline `tr()` odr-uses.
  - `src/workflow/workflow_run.cpp`: `gmtime_r` → `gmtime_s` on Windows.
  - `src/core/raster/qgssinglebandpseudocolorrenderer.h`: unique_ptr member
    with incomplete type (MSVC deletes inline dtor) → include the shader
    header.
  - `tests/CMakeLists.txt`: POSIX-fixture targets (mini_cog_server sockets,
    fork/waitpid fault injection, SIGSTOP cache e2e, popen CLI JSON,
    symlink/rm -rf plugin packaging) gated `if(NOT WIN32)` with comments.
  - `tests/test_placeholder_grammar.cpp`, `tests/test_exprs_*`,
    `tests/test_plugins_runtime_host.cpp`,
    `tests/test_raster_processing_dialog_base_main.cpp`: guarded
    `<unistd.h>` / portable setenv.
  - `tests/test_workflow_run_coordinator.cpp`: most-vexing-parse
    `Json::Value x(Json::Value(Json::objectValue))` declarations.
  - `tests/test_harness_error.cpp`: Catch2 `const char*` pointer-equality
    comparisons wrapped in `std::string`.
- Harness code itself: QDir/QFile/Json only; no POSIX headers.

## R6 Documentation claims — PASS (with enforced weak wording)

- "deterministic preflight/verification/recipes/evals" — code-backed (rule
  packs are pure functions over inspected facts; evals assert exact codes).
- "no false success after FAIL" — code-backed (`runResultDocument` forces
  `status:"failed"`); eval asserts FAIL aggregation.
- "bounded responses" — measured + capped (benchmarks file); registry-seam
  enforcement of the 512 KiB cap remains P2 (below).
- NOT claimed: "fully autonomous correctness", "cross-platform" as a goal —
  Windows work is documented as repairs enabling the local lane; POSIX-only
  test fixtures are skipped with evidence.

## P2 items (accepted with evidence)

- FIXED (pre-existing master defects, required for any Windows build):
  - `src/core/project/qgsproject.cpp`: directory fsync guarded
    `#ifndef Q_OS_WIN` (Windows cannot `::open` a directory; the previous
    code referenced `O_RDONLY`/POSIX `open` unguarded in a live branch).
  - `src/sdk/exprs/plugin_validator.cpp`: `S_ISREG` fallback define for MSVC.
- Harness code: uses QDir/QFile/Json only; GDAL probes guarded; no
  `#include <unistd.h>`.

## R1 findings (recorded during self-review)

- FIXED (harness): registration used static guards, which would not
  re-register after `SpatialToolRegistry::reset()`; harness register
  functions now rely on registerTool() duplicate rejection (naturally
  idempotent + reset-safe).
- P2 (pre-existing 3.0, out of scope): `registerWorkspaceTools` and friends
  use static-guard registration with the same reset() limitation — reset()
  drops 3.0 tools after first registration. Harness tools no longer have this
  problem; the 3.0 fix touches the hot registration path, deferred.

## P2 items (accepted with evidence)

- `harness:run_status` transient classification for auto-resume is heuristic
  (message sniff) — bounded to one attempt, engine `resumeRun` re-checks
  outputs before re-running, so a wrong classification cannot redo completed
  work. Full typed error propagation from TaskCenter task failures is
  follow-up (needs TaskCenter error-code surfacing).
- 512 KiB output cap (`kMaxToolOutputBytes`) not yet enforced at the
  SpatialToolRegistry seam; harness tools stay far below it and evals pin
  budgets. Enforcement touches the hot MCP path → deferred with evidence.
- MCP workflow-run outputs are not yet registered as governed assets
  (TODO(P1-E1) predates this epic); run verification therefore warns (not
  fails) on missing provenance sidecars.
