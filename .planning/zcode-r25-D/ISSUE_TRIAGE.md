# R25-D Issue Triage — every candidate verified against live master (2610cb4741)

Legend: LIVE (defect reproducible in current code) · FIXED_IN_MASTER_BUT_OPEN · PARTIAL · REGRESSED · NOT_REPRODUCIBLE · EXTERNAL_BLOCKER · OUT_OF_ZONE (owned by another track's write zone)

| Issue | Verdict | Evidence (path:line @ master) | Action |
|---|---|---|---|
| #1535 P1 算法 runner feedback UAF | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/dialogs/async_algorithm_runner.cpp:50-58` — `feedback->setParent(m_task)` hands ownership to the task; dtor disconnect+cancel; comment documents the fix | none (comment on issue at PR time) |
| #1536 P1 guided workflow 行号换算 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/widgets/guided_workflow_widget.cpp:160-181` — `kLoadErrorEntryRows` + `labIndexOfRow(row,errorCount)`; comment cites `#1536` | none |
| #1537 P1 STAC searchNext 旧分页 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/stac_client.cpp:225-251,297-298` — `lastPageGeneration` guard + F-02 comments | none |
| #1559 P2 AgentServices workspaceService 悬空 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/main_window_docks.cpp:749-766` — destroyed-connected release with `#1559` comment | none |
| #1560 P2 runGdalTask 取消 hook 空 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/dialogs/raster_processing_dialog_base.cpp:383-404` — `GdalTaskCancelProbe` + `#1560` comment | none |
| #1561 P2 光谱剖面 GUI 线程同步 GDALOpen | **LIVE** | `src/app/widgets/spectral_profile_widget.cpp:495-501` `extractProfile` opens `layer->source()` synchronously; call path `main_window_layers.cpp:174` (identify slot, GUI thread); `setProfile` :231 → `extractProfile` :481+ | **W3 fix target** |
| #1562 P2 log panel Level 过滤空 lambda | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/log_panel.cpp:156-192` — `refilter` lambda + `RetainedLog` retained records | none |
| #1563 P2 精度报表 CSV 注入 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/classification/rs_accuracy_panel.cpp:281-289` — `csvField()` escaping in use | none |
| #1564 P2 VaCursorProbe 节流绕过 | **FIXED_IN_MASTER_BUT_OPEN** | `src/app/visualanalytics/va_cursor_probe.cpp:74-91` — dwell throttle now enforced in `startPending()` | none |
| #1565 P2 ladderLabelsZh 上界 5 | **OUT_OF_ZONE (E)** | `src/app/teaching/guided_lab_workspace.cpp` — E-track exclusive UI | handoff note only |
| #1632 P3 src/app 技术债 6 项 | **PARTIAL-LIVE (in zone)** | see below per-item | selected fixes |
| #1638 P3 src/gui vendored QGIS 桩 | **OUT_OF_ZONE** (vendored QGIS tree; "优先零修改") | `src/gui/CMakeLists.txt:1919+`, `antigravity_codeeditor_stubs.cpp` | CROSS_TRACK_FINDINGS entry |
| #1655 P3 CP11 渲染门永久熄灭 | **LIVE (in zone)** | `tests/test_cartography_production_11.cpp:157-165` `skipIfHostRenderBroken()` skips 5 render cases unless `SICNU_CP11_RENDER` set; no CI lane sets it | **W3/W7 target** — run gated render tests on this host; if host healthy, add a stable render contract |
| #1695 R17-ui P3 清单 | **PARTIAL (1 sub-item in zone)** | AsyncGdalRunner dead-code + WaitCursor lifecycle (`src/app/dialogs/async_gdal_runner.cpp`); other sub-items live in `src/app/teaching_admin/` (E) / `src/lab/` (E) | fix in-zone sub-item; handoff rest |

## #1632 per-item triage (all six re-verified against 2610cb4741)

1. `terrain_dialog.h` dead QFutureWatcher/onAnalysisFinished — **LIVE** (in zone `src/app/dialogs/`)
2. `terrain_dialog.cpp:221-254` duplicated output-path derivation with divergent baseName/completeBaseName — **LIVE** (in zone)
3. `spectral_profile_widget` two same-name classes (global + `exp_gui`), `exp_gui` unused, globalInstance sampling — **LIVE** (in zone; couples with #1561)
4. `gui_job_adapter.cpp:27-171` three duplicated ~45-line submit bodies — **LIVE** (in zone `src/app/shell/`)
5. `va_selection_hub.cpp:15-18` `clampText` third copy of surrogate-safe truncation (`sicnu::help::truncateSummary` exists) — **LIVE** (in zone)
6. `rs_sample_erase_tool.cpp:152-159` dead `mStamps` bound + unbounded rubber-band growth (sister brush tool is bounded) — **LIVE** (in zone `src/app/editing/`)

## Selection rationale (goal-fit)

Fix picks favor the task-book W-mapping:
- #1561 + #1632-3 → W3 (display/interaction performance: GUI-thread stall on /vsicurl/ identify)
- #1632-1/2 → W2-class dialog hygiene (same file, one coherent change)
- #1632-4 → W2 (single submission seam hardening)
- #1632-5 → W5 (chart/selection hub robustness, typed truncation reuse)
- #1632-6 → W6 (teaching-user editing tool safety)
- #1655 → W3/W7 (render gate honesty; run the gated cases on this host with real evidence)
- #1695-AsyncRunner → W0 lifecycle audit item
