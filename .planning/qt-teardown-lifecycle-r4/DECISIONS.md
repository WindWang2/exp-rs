# DECISIONS — Track 2: Qt/QGIS teardown lifecycle (R4 Deep)

Status: v1 (WP-A audit). Evidence status: **stack-capture pending** — binaries still compiling at record time; every A-n below is upgraded from *hypothesis* to *confirmed* only by a captured crash stack in EVIDENCE.md (brief WP-A: no stack → not reproducible → not claimable).

## 1. Object construction/teardown timeline (the model)

Main-thread lifetime phases in a teardown-afflicted test binary:

```
[load]   DSO init: qgis_core/qgis_gui loaded; library-level statics registered
[TU init] test-file namespace/statics: lambda-static QApplication / ensureApp statics
          → registered with __cxa_atexit at FIRST execution (early, E1)
[main]   Catch2 session runs TEST_CASEs; per-case stack objects (QgsMapCanvas,
         widgets) constructed and destroyed INSIDE the case (clean, E2)
         during cases: first CRS use → thread_local QgsProjContext::sProjContext
         (qgsprojutils.h:302, USE_THREAD_LOCAL && !Q_OS_WIN branch);
         first registry/cache use → Q_GLOBAL_STATIC guards registered (late, E3)
[exit]   glibc __run_exit_handlers:
         1) __cxa_atexit LIFO → newest first: E3 caches/registries destroyed,
            THEN E1 app static (leaked-until-exit) destroyed LAST
         2) __call_tls_dtors → ~QgsProjContext → proj_context_destroy
```

Authoritative teardown contract in production code (truth sources):
- `src/core/qgsapplication.cpp:1619 exitQgis()` — threadpool join → authManager delete → DeferredDelete flush → project delete → DeferredDelete flush → provider registry delete → `invalidateCaches()` → style cleanup → GDAL cleanup (only when no open datasets).
- `src/core/qgsapplication.cpp:647-657` — `~QgsApplication` + `invalidateCaches()`: "invalidate coordinate cache while the PROJ context held by the thread-local QgsProjContextStore object is still alive. Otherwise … we might use freed memory."
- `src/app/main.cpp:626-636` — GUI exit ordering: TaskCenter/JobEngine shutdown (join) → plugin unloadAll → `window.reset()` → `delete app` → return.
- `src/app/main_window.cpp:333 ~QgisDesktopWindow` — session view dereg → child windows → plugin sinks → canvas `stopRenderingAndSettle()` + `unsetMapTool` + layer clear → layer-tree model detach. (In-file history: two real exit-SIGSEGV classes already fixed here — map-tool double-delete; layer-tree view querying dead model.)

## 2. Suspect points (A-n)

| id | point | evidence so far | status |
|---|---|---|---|
| A-1 | 23 test files keep a `static`/lambda-static/leaked `QApplication` alive into `exit()`; destroyed LAST in LIFO, i.e. **after** all QGIS caches/registries that were created during the run | pattern scan (BASELINE §6); Qt parentless-app dtor vs Q_GLOBAL_STATIC guards | hypothesis → stack pending |
| A-2 | `tests/test_layout_tools.cpp:69-76` leaks a heap `QgsApplication` with `initQgis()` and **no** `exitQgis()` → provider registry, QgsProject, auth manager, NAM, default style all live at exit; `~QgsApplication`/`invalidateCaches()` never runs | file read | hypothesis → stack pending |
| A-3 | The 21+5 `FastExitListener` files bypass **all** of phases exit-1/2 via `std::_Exit(testRunEnded)`; crash masked, not fixed | file reads (test_dual_viewport_sync:19-30, test_workbench_full_shell_lifecycle:57-81) | confirmed masking (by construction) |
| A-4 | `tests/test_view_link.cpp:79-87` static heap `QgsApplication` + initQgis/exitQgis but never deleted (the #1319 leak precedent) | file read | hypothesis → stack pending |
| A-5 | Production `main.cpp:176` comment claims heap-allocation "avoids destructor crash during DSO cleanup", yet the GUI path **does** `delete app` (main.cpp:634) — comment is stale; the actual protection is the ordered teardown, not the allocation | main.cpp read | documented |
| A-6 | 14 binaries (PR #1335 taxonomy group 2) crash at exit with **no defense at all**: provider_http, georef_crs_pick_failclosed, layer_sync_contract, qgis_display_manager, edit_roi_semantics, edit_persistence, editing_e2e, workspace_services(-host) | #1335 body | to reproduce at first build |
| A-7 | `exitQgis()` skips GDAL deinit when datasets remain open (qgsapplication.cpp: GDALDumpOpenDatasets guard) — any test leaking a GDAL dataset poisons process teardown | source read | hypothesis → stack pending |

## 3. Retirement blueprint (WP-B design, pending stack confirmation)

For each atexit-family file, replace "leak app + `_Exit`" with the production-shaped sequence, placed in a **shared helper** so the fix is one root-cause, not 26 private hacks:

```
int main(...) {  // or listener-free Catch main
    <AppKind> app(argc, argv);            // scoped, E1-equivalent but owned
    QgsApplication::initQgis();           // only where the file already did
    int rc = Catch::Session().run();
    // teardown mirror of main.cpp:626-636 (scaled to what the file initialized):
    QgsApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    <exitQgis() if initQgis() was called>
    return rc;                            // app destroyed here, in E1 order
}
```

Non-negotiables: same REQUIRE set (no assertion edits); two consecutive green runs with **no** `_Exit`; red step = same binary with teardown removed (or defense removed pre-fix) showing the real stack; no new leaks to substitute for `_Exit`.

## 4. WP-C fixture subjects (initial layering, ≥15)

Layer 1 canvas/window family (retire root-cause adjacency): dual viewport, twincanvas, secondary map view, georef dual window, workbench full shell, layout designer lifecycle.
Layer 2 data-holding widgets (src/app/widgets, 22 pairs): guided_workflow_widget, spectral_workbench_panel, comparison_widget + GDAL-holder widgets enumerated next pass.
Layer 3 host objects: JobEngine/TaskCenter shutdown mirroring (main.cpp:628-630), QgisDesktopWindow partial teardown invariants (main_window.cpp:333 ordering), NAM-holder cleanup.
Fixture skeleton per brief: construct (explicit parent chain) → use (touch background/network/data path) → destroy → assert no-crash + thread joined (`isRunning()==false` AFTER dtor returns) + `QPointer` null.
