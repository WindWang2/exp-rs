# RETIREMENT — `_Exit` / leak workaround adjudication (Track 2, R4)

Contract (from brief WP-B/WP-F): every call site gets exactly one verdict —
`退役` (root cause fixed + defense removed + double-run green) / `保留` (root cause named, why-not-this-round, retirement condition) /
`语义保留` (not an atexit-defense: semantic exit, documented). No verdict may just repeat the old comment; each row must add root-cause id (DECISIONS A-n), retirement condition, and verification commit.

Baseline: 30 real call sites in 28 files + 1 leak point (`test_view_link.cpp:84`) = **31 adjudication rows**. Count can grow (new sites discovered), never shrink.

| # | file:line | family | root cause (A-n) | verdict | condition / reason | commit |
|---|---|---|---|---|---|---|
| 1 | test_classification_window.cpp:38 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 2 | test_dual_viewport_sync.cpp:30 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 3 | test_gcp_canvas_crs.cpp:30 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 4 | test_gcp_canvas_item.cpp:28 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 5 | test_gcp_contains.cpp:25 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | first tracer-bullet candidate | |
| 6 | test_gcp_list_widget.cpp:26 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 7 | test_georef_dual_window.cpp:28 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | lambda-static app variant | |
| 8 | test_georef_dual_window_workbench.cpp:38 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 9 | test_georeferencing_session.cpp:35 | atexit | A-1 | 退役 | shared TeardownListener + heapQCoreApplication; Phase-5 review P0: include had landed at EOF → TU did not compile and first green came from a stale binary; include moved to top block, rebuilt (marker-string check), double-run green ×2 | review-fix commit | | |
| 10 | test_georef_session_adapters.cpp:20 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | lambda-static app variant | |
| 11 | test_georef_task_list.cpp:20 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 12 | test_georef_window.cpp:30 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 13 | test_georef_window_rpc_mode.cpp:32 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 14 | test_georef_window_warp_lock.cpp:21 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 15 | test_hooks_comprehensive.cpp:43 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 16 | test_layout_designer.cpp:52 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 17 | test_layout_designer_lifecycle.cpp:47 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | ok-variant | |
| 18 | test_layout_tools.cpp:60 | atexit | A-2 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | leaked heap QgsApplication variant | |
| 19 | test_pick_canvas_mode.cpp:25 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 20 | test_project_context_run_mirror.cpp:58 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | ok-variant | |
| 21 | test_project_session_boundary.cpp:53 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | ok-variant | |
| 22 | test_rms_scatter.cpp:26 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 23 | test_secondary_map_view_session.cpp:56 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | ok-variant | |
| 24 | test_spectral_curve.cpp:24 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 25 | test_twincanvas_sync.cpp:30 | atexit | A-1 | 退役 | FastExitListener → shared TeardownListener; app heap-owned; red-step stack captured (rc=139, EVIDENCE §2); double-run green ×2 | see git log retire(r4-qt-teardown) | | |
| 26 | test_workbench_full_shell_lifecycle.cpp:81 | atexit | A-1 | 退役 | app was already heap-owned (kept); shared listener ordered teardown; Phase-5 review P0: include at EOF fixed → rebuilt + double-run green ×2 with listener verified in binary | review-fix commit | ok-variant; full-shell binary | |
| 27 | test_chunk_resume_11.cpp:101 | injection | — | 语义保留 | hard process death IS the tested semantics (crash recovery #697): mid-run `_Exit(64/70/0)` simulates killed worker; not an exit-phase defense | |
| 28 | test_chunk_resume_11.cpp:115 | injection | — | 语义保留 | same | |
| 29 | test_chunk_resume_11.cpp:128 | injection | — | 语义保留 | same | |
| 30 | tests/support/offline_probe.h:254 | probe | — | 语义保留 | `_Exit(77)` = probe subprocess marker exit; used as inter-process contract, not teardown masking | |
| 31 | test_view_link.cpp:84 (leak, no `_Exit` on master) | leak | A-4 | 退役 | NO listener (own main retained): scoped stack QgsApplication destroyed after exitQgis() and before glibc exit — production ordering (main.cpp:622-635 shape); commit 9c3bc4bb2; double-run green ×2 | 9c3bc4bb2 | #1319/#1335 precedent; retire via ordered teardown + heap-owned QgsApplication | |

Rolling counts: **退役 27 / 语义保留 4 / 保留 0** (31/31 adjudicated). Retirement floor (≥8) exceeded. Per-row double-run evidence in EVIDENCE.md §3.

## Out-of-scope note (Phase-5 review P2)

Lowercase `_exit(` sites in `tests/` (~20 Windows-only `#ifdef _WIN32` main tails, e.g. `tests/sicnu_test_main.cpp:54`, plus fork-child `::_exit(` in `tests/helper_external_process.cpp` / `tests/fixtures/isolation_plugin/`) are NOT part of this adjudication: the measured Linux baseline (§BASELINE 6) is the uppercase `_Exit(` family. The Windows tails mask teardown on Windows exactly as the retired defense did on Linux and are hereby **named as a follow-up track candidate** (they are inert on this Linux CI path).

