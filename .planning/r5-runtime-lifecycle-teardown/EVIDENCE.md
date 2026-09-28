# EVIDENCE — R5 Track 01 runtime-lifecycle-teardown

All commands run in the worktree `../exp-rs-r5-runtime-lifecycle-20260928-035158`, parallelism `--parallel 2` (builds) / `-j1` (tests), `QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib` (pwb-sdk transitive deps, per #1354 C1 pin). Sanitizer runs additionally set `LSAN_OPTIONS=detect_leaks=0` (repo convention — the binaries deliberately leak their heap app objects) and `ASAN_OPTIONS=detect_odr_violation=0` (Qt moc `staticMetaObject` ODR noise).

## Build lanes

| Lane | Configure | Result |
|---|---|---|
| build-dev | `cmake --preset dev-default -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` (Debug, ENABLE_TESTS=ON) | full `cmake --build build-dev --parallel 2` rc=0 |
| build-sanitizer | `cmake --preset sanitizer-debug -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` (Debug, ASan+UBSan) | targeted: test_d17_workflow_pipeline_e2e, test_mcp_server, test_capability_surface_parity + closures, rc=0 |
| /home/kevin/tsan_wt (scratch) | same source, `CMAKE_CXX_FLAGS="-fsanitize=thread -g -fno-omit-frame-pointer"`, ENABLE_TESTS=OFF | target `sicnu_workflow` |

## #1358 — PipelineRunCoordinator teardown

Fixed at the root (commit 43650f0a9): pump-reentrant same-thread delete freed state under a live completion frame (old code: qWarning "unsupported" + free → UAF); phase-1 cancel / phase-2 shuttingDown gap let the frame tail emit after destruction began (probe-verified ~1 ms). Fix: frame-shared `RunState` (shared_ptr + mutex-guarded `adoptState()` per affinity body), both flags phase-1, drain phase-2, foreign busy-wait removed. No sleeps, no detached threads, no leaks.

Deterministic regressions (tests/test_d17_workflow_pipeline_e2e.cpp):
- T1 `Foreign-thread destruction during a whole-file hash never frees live state` — promise-armed foreign delete, 1 GiB artifact (>=3x margin).
- T2 `Deleting the coordinator from its own hash pump never frees live state` (NEW) — the previously-unprotected shape.
- T3 `A completion whose worker outlives the coordinator is never delivered` (NEW).
- All three scrub scratch via RAII guard (review B-1).

Runs:
- dev: destroy group x3 green; full binary 307 assertions / 9 cases green.
- ASan: destroy group x3 clean (exit 0, zero ASan reports); full binary clean (307/9, exit 0).

TSan (scratch driver `/tmp/tsan_coordinator_driver.cpp`, TSan-instrumented sicnu_workflow, serial): master coordinator reproduces the bug under TSan — 11 reports incl. 3 heap-use-after-free at the exact unprotected paths (`onNodeFinished` hash-loop cancel-flag read, `~BusyGuard`, `invokeOnCoordinatorThread`). Fixed coordinator: 0 UAF, 0 protocol races; remaining reports are pre-existing Qt QThreadPool teardown races (identical on master) and uninstrumented-Qt copy noise. A TSan runtime CHECK (thread registry vs Qt thread churn) aborts some runs late — recorded as a TSan/Qt interaction, not a race. Full table: DECISIONS.md D6.

Module regression (dev, direct binaries): test_workflow_pipeline 27/3, test_workflow_checkpoint_cache 1169/45, test_ir2_port_param_mapping 93/13, test_chunk_resume_11 47/8 — all green.

## #1357 — McpServer run_workflow / capability exit-phase double free

Reproduction status on this toolchain (CachyOS / GCC 16.2.1 / Qt 6.11.2 / GDAL 3.13.3 — the environment family that produced the red-zone observation in #1354):
- `test_mcp_server` full binary: 4982 assertions / 27 cases (incl. every run_workflow case), exit 0 — dev run x4, ASan run x1, zero ASan reports.
- `test_capability_surface_parity` full binary: 252 assertions / 3 cases, exit 0 — dev run x2, ASan run x1, zero ASan reports.

Verdict: not reproducible in isolation; the #1354 context records the red zone under full-suite `-j2` with a 204-item /tmp-ENOSPC cascade, which also matches the symptom class. Root-cause-grade hardening for the exit chain the run_workflow path exercises (commit 15fa5e146): `TaskCenter::instance()` now anchors `JobEngine` and `ExecutionTelemetry` BEFORE itself, so `~TaskCenter`'s calls into `JobEngine::instance().shutdown()` and `shutdownSharedWorkerPool()` → `ExecutionTelemetry::instance()` can no longer execute destroyed singletons (they were lazily constructed AFTER TaskCenter in the run_workflow flow, i.e. destroyed BEFORE its destructor ran).

No `_Exit`/leak workaround was added or kept for either seed; the leak doctrine (heap app objects intentionally outlive exit) is the #1342 one and unchanged.

## Master build blockers fixed en route (all first-class commits)

1. `tests/CMakeLists.txt:14728` stray `=======` marker — configure impossible.
2. `tests/support/qt_lifecycle.h` — `QCoreApplication*` returned as `QApplication*` (GCC 16 error; #1342 Phase-5 P2 regression).
3. `src/app/panels/data_manager_panel.cpp:222` — bare `tr(` in a free function (GCC 16 error; #1339 regression) — broke every app-panel-linked target.
4. `tests/test_vector_overlay_crs_transform.cpp` — never compiled anywhere (nonexistent `run(...)` API, rvalue→lvalue-ref bindings). Now compiles and runs; 4 overlay cases fail inside the algorithms — pre-existing semantic reds, owned by the vector-overlay/io track, documented and NOT skipped.
5. `sicnu_grader` non-PIC — sanitizer lane could not link `libsicnu_agent`.
