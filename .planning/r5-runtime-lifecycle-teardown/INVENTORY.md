# R5 Track 01 — Runtime lifecycle / Qt-QGIS-MCP exit-phase teardown — execution-time inventory

Recorded at execution start (2026-09-28, worktree creation time).

## Baseline facts

| Item | Value |
|---|---|
| Prompt snapshot master | `a726d17a6224632d929e782e996351732632f272` (2026-09-27) |
| Execution-time `origin/master` | `a726d17a6224632d929e782e996351732632f272` (unchanged vs snapshot) |
| `BASE_SHA` (worktree branch point) | `a726d17a6224632d929e782e996351732632f272` |
| Branch | `hardening/r5-runtime-lifecycle-teardown-20260928-035158` |
| Worktree | `../exp-rs-r5-runtime-lifecycle-20260928-035158` (separate from the 3 sibling R5 worktrees) |
| Toolchain (this host) | CachyOS / GCC 16.2.1 (`/usr/sbin/c++`) / Qt 6.11.2 / GDAL 3.13.3 (pwb-sdk prefix `/home/kevin/pwb-sdks/root/usr`) / cmake 3.30.5 (`/home/kevin/toolchain/cmake-dist/bin`) / ninja (`/home/kevin/pwb-sdks/root/usr/bin/ninja`) — same environment family that produced the seed failures in #1354 |
| Build | `cmake --preset dev-default -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` → `build-dev/` (Debug, ENABLE_TESTS=ON), `--parallel 2` |
| Test entry | `ctest` inside `build-dev/`, `QT_QPA_PLATFORM=offscreen`, `-j1` for the seeds |
| Sanitizer lane | `sanitizer-debug` preset exists (ASan+UBSan, `-fno-sanitize=vptr`); TSan not preset — separate dir if needed, run serially |

## Seed issues (both OPEN at execution time)

- **#1357** `test_capability_surface_parity` family / "McpServer run_workflow" case: all assertions pass, then exit-phase `double free or corruption`. Asks: ASan first site; audit McpServer run_workflow session/tool/runtime ownership; remove any `_Exit` defense afterwards.
- **#1358** `test_d17` "Foreign-thread destruction during a whole-file hash never frees live state": completion callback firing after host destruction; suspect `PipelineRunCoordinator` teardown order. Asks: TSan clean; stable green; deterministic late-completion regression in ctest.

Note: the literal "McpServer run_workflow" TEST_CASEs live in `tests/test_mcp_server.cpp` (lines 973–1420), not in `tests/test_capability_surface_parity.cpp` (which exercises McpServer via `CapabilityProbeServer` but has no run_workflow case). Both binaries are in scope for the exit-phase double free.

## Relevant merged work read

- **#1342** (`r4-qt-teardown`): retired 27 `_Exit`/leak workarounds; shared ordered teardown in `tests/support/qt_lifecycle.h` (`TeardownListener` → deferred-delete drain → `exitQgis()` → delete heap app); drift gate `test_teardown_retirement_gate_r4`; production exit order in `src/app/main.cpp` is truth. Known leftovers it disclosed: Windows `_WIN32 _exit()` sites; `qgscoordinatetransform.cpp:1318` missing guard (A-8).
- **#1354** (`r4-ci-redzone`): source of the two seed issues (its "unresolved items"); environment attribution matches this host.
- **#1346** (`r4-mcp-surface`): three-surface tool contract snapshot; MCP protocol edges.
- **#1351** (`r4-workflow-durability`): workflow crash matrix, lock semantics (touches the same workflow module).

## Open PRs at execution time

- #1365 (R5 track 04, persistence) — no file overlap with this track's seed scope.
- Remote sibling R5 branches present (fused-chain, model-runtime-workers, ci-test-budgets, core-platform-portability, persistence-consistency): none touch `src/workflow/pipeline_run_coordinator.*` or the two seed test files per their names/scope; re-check at PR time.

## Out of scope (this track)

- workflow resume/cancel/checkpoint **state semantics** (Track 06), GDAL fused-chain crash (#1356 → Track 02), plugin loader lifecycle (#1364 → Track 03) unless a shared Qt helper needs a minimal compatible patch, #1355 ctest GDAL_DRIVER_PATH, #1360/#1359 persistence (Track 04), test budgets (#1361/#1363).

## Findings already at execution time

1. **master cannot configure**: `tests/CMakeLists.txt:14728` carried a stray `=======` merge-marker line (parse error at configure). Fixed as the first commit (`fix(ci): remove stray merge-conflict marker breaking configure`); `git grep` confirms no other markers.
2. Neither `test_mcp_server`, `test_capability_surface_parity`, nor `test_d17` register the #1342 `TeardownListener`; all three leak their heap app objects by design and rely on exit() surviving global-static destruction — exactly the hazard zone of the two seeds.
3. `PipelineRunCoordinator` (Engine-2, the #1358 suspect) already implements the Track 13 drain (`shuttingDown` + `pool.clear/waitForDone` + `removePostedEvents` + foreign-thread `affinityBusy` wait). The remaining hole(s) must be found empirically; candidate: worker→completion `QueuedConnection` post windows around the drain, and the destructor's busy-wait semantics.
4. MCP `run_workflow` submits into `TaskCenter::instance()` + `WorkflowRunCoordinator::instance()` — two Meyers singletons (QObject) whose relative static-destruction order in test binaries is unspecified; `WorkflowRunCoordinator`'s ctor runs `installExecutionEnvironmentPins()`; `TaskCenter::~TaskCenter` runs `shutdown()` + `ArtifactGC::installProtectedArtifactProvider({})` with an explicit cross-TU order comment. This graph is the #1357 suspect zone.
