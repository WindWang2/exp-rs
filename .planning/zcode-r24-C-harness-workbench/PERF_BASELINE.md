# PERF_BASELINE — R24-C

Policy per goal §4: no tight wall-clock pass thresholds; fixed fixtures, 5 samples, report values + variance; correctness gates stay structural.

## WP1 charts projection (the only hot-path change)
Changed path: `buildWorkspaceState()` now reads `ChartRegistry::listCharts()` (mutex-copied array of bounded specs ≤ registry size) and emits ≤ `kMaxWorkspaceSummaryCharts` (25) JSON entries. The registry read is O(N) with N = live charts; workspace capture is called per agent conversation turn (debounced upstream), not per frame.

5-run samples of `test_r24_c_workspace_charts.exe` (whole-process wall, incl. process spawn + QgsApplication::initQgis — the dominant fixed cost; the case itself creates 32 charts and runs 5 workspace captures):

| run | 1 | 2 | 3 | 4 | 5 |
|-----|---|---|---|---|---|
| ms  | 1136.0 | 867.9 | 844.1 | 859.5 | 849.3 |

Steady-state ≈ 845–868 ms (first run pays cold page-cache: +270 ms). For contrast, the pre-existing `test_workspace_state.exe` (no charts) runs in the same ~0.85 s band on this host — the 25-chart projection is below measurement noise at conversation-capture frequency. No regression expected on canvas/GUI paths: the projection runs on the agent capture path only, never in a paint/event loop.

## #1699 conversions (perf-relevant test semantics)
All converted gates keep their Release base values (scale 1.0 ⇒ identical budgets to the old literals +1 ms); the sanitizer widening is test-lane-only. The two clamped windows (`min(wallClockBudgetMs(200),400)`) LOSE discriminative headroom only above 400 ms, which is by construction below the 500 ms regime boundary they classify — no coverage loss in Release.

## Layout menu removal
Deletes two `menuBar()->addMenu()` calls at designer construction — strictly less work per dialog open; no measurable path added.

## teaching_admin discovery move
Discovery moves from the GUI thread to the worker thread — GUI-thread blocking during class-directory scans is REMOVED (the defect #1695-3 describes); total wall time of a batch is unchanged (the same scan runs once in the worker before grading; the old code ALSO scanned inside runBatchAssessment).
