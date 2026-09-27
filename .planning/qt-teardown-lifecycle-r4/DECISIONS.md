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

## 5. A-8 (context note, src/core read-only) and WP-D audit table v1

**A-8**: `src/core/proj/qgscoordinatetransform.cpp:1318 invalidateCache()` uses `sCacheLock` without the null-guarded accessor pattern that `qgscoordinatereferencesystem.cpp:3326` and `qgsellipsoidutils.cpp:348` already received — a latent crash *only* in the "invalidate after guards destroyed" path. Our listener invalidates while guards are alive (safe), so this is a read-only observation for the red-zone owners, not a change.

**QTimer::singleShot audit (whitelisted src/app+src/gui+src/ui; 22 sites)**:
| site | form | verdict |
|---|---|---|
| 19 sites (va_cursor_probe:67, progress_dialog:56, qgsdatasourcemanagerdialog:128/181, main_window_menus:836, workbench_state:103, qgsnewvectortabledialog:54, processing_history_panel:166, qgsstacsourceselect:403, qgsdualview:622/624/627, qgsoptionsdialogbase:728/742, qgsvaluerelationwidgetwrapper:587, qgsmodelgraphicsview:97/107/113) | context = `this` | Qt auto-disconnect contract; safe by construction. qgsdualview additionally captures raw `canvas` beyond `this` → residual hazard only if canvas dies while view lives; covered by fixture case (dual viewport / map tool) |
| main.cpp:403, 600 | no context; lambda captures QPointer-guarded window + raw app | fires inside running exec(); app outlives loop; safe-by-lifetime, QPointer guards the window |
| qgscredentialdialog.cpp:102 | context nullptr; captures local `realm` copy | value-captured; no member deref → safe; deep-check in execution pass |
| qgsoptionsdialoghighlightwidget.cpp:156 | context `this`; captureless lambda (statics) | safe; static members outlive widgets |

**QNetworkReply audit (whitelist; 11 files)**: stac_client.cpp (191-199) and qgscodeeditorwidget.cpp (596-608) show the repo-standard pattern — `connect(reply, …, this, …)` + `reply->deleteLater()` on finished. Remaining 9 files touch replies via wrappers/indirect members; per-file confirmation during execution pass (WP-D budget), each recorded here.

**先亡宿主 representative test** (WP-D deliverable): dual-viewport fixture case "controller dies before canvases" already asserts the no-post-mortem-delivery contract for the sync path; the canvas/CRS churn cases cover the PROJ-cache side. Network-path 先亡宿主 rides on stac_client execution-pass verification.

## 6. Root cause CONFIRMED (red-step stack, 2026-09-27)

A-1/A-3 upgraded from hypothesis: captured SIGSEGV (rc=139 after all assertions pass) —
`exit()` LIFO destroys the leaked static `QApplication` → `~QApplicationPrivate::cleanupThreadData()`
→ `QThreadStorage<QgsProjContext*>::deleteData` → `~QgsProjContext` (qgsprojutils.cpp:50)
→ `QgsCoordinateReferenceSystem::removeFromCacheObjectsBelongingToCurrentThread`
(qgscoordinatereferencesystem.cpp:770) → `QgsReadWriteLocker` on the already-destroyed
`Q_GLOBAL_STATIC` cache lock → `lockForWrite(0x0)` → SEGV.
This build resolves `qgsprojutils.h:296-304` to the `QThreadStorage` branch (`USE_THREAD_LOCAL` undefined),
so the context dies inside `~QCoreApplication`'s thread-data cleanup — before that, `~QgsApplication`
(and our listener) run `invalidateCaches()` while guards are alive, which is the safe documented path.
Full trace in EVIDENCE.md §2. Retirement = production exit ordering (main.cpp:622-635) inside the run.

## 7. WP-D final verdicts (deep-check complete)
- `qgscredentialdialog.cpp:102` — context nullptr but lambda value-captures `realm` and touches only the function-local static cache + mutex: **safe** (no member deref after dialog death).
- `qgsoptionsdialoghighlightwidget.cpp:156` — singleShot line sits in the `#else` of a `#if 1` block: **dead code**, never compiled into the running path.
- Net WP-D defect count in whitelisted dirs: **0 fixes required**; audit table §5 stands (19 context-safe, main.cpp pair safe-by-lifetime with QPointer guard, dualview raw-canvas capture documented as residual hazard covered by fixture).
