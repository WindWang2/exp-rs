# UI_FLOW_MATRIX — R24-C (O-C0)

Six real user task paths through the shipped surfaces. "Expected/Actual" are grounded in source (file:line @ worktree HEAD) and in executed automated tests (see TEST_RESULTS.md for run logs; ✅ = run GREEN in this track's build).

## Path 1 — Import raster → inspect → run algorithm → layer result (GUI toolbox path)
- Entry: Data Manager panel / Import → `DataManagerPanel` (src/app/panels/data_manager_panel.cpp) `displayRequested` → `ActiveViewHost::registerAndDisplay` (src/app/active_view_host.cpp:44 area) → asset registered + layer added.
- Algorithm: Processing Toolbox dialog → `SicnuAlgorithmDialog` (src/app/dialogs/sicnu_algorithm_dialog.cpp) → submitted via `ProcessingJobAdapter` to `TaskCenter` (src/processing/framework/task_center.h:265).
- Progress/result: `RsJobPanel` (src/app/shell/rs_job_panel.cpp:161-164, queued taskAdded/taskUpdated) + auto-load `layerAutoLoadRequested` (task_center.h:511) with epoch guard F-05/F-06 (main_window_docks.cpp:682-722).
- Expected state chain: Queued→Running→Completed + layer added; failure typed with reasons.
- Automated evidence: test_task_center / test_ui_task_center_contract / test_classification_task_center families ✅(runs listed in TEST_RESULTS.md).
- Honesty note: none of these steps advertises an unavailable format; output write is staged-publication (r10-A).

## Path 2 — Select layer → parameters → run → cancel (cooperative cancel path)
- Layer tree selection → `SelectionContext` snapshot (src/app/workbench/selection_context.h:202; debounce ≤150ms) → `ContextRules::unavailabilityReason` (selection_context.h:102-191) → CommandRegistry `refreshAll` (main_window_workbench.cpp:414-421).
- Run: `TaskPanelHost` Run→Stop in flight (task_panel_host.h:57) → `TaskCenter::cancelTask` typed reasons (task_center.h:88-97).
- Cancel is cooperative (upstream cascade `cascadeCancelTargetsLocked` task_center.cpp:3701-3704; owned-orphan `StructuredJoin` :1385-1408).
- Automated evidence: test_workflow_cancel, test_r19_02_workflow_cancel_order, test_task_center (cancel window case known-flaky per #1714/#1677 evidence — not a regression).

## Path 3 — Pi asks → tool discovery → run_workflow → status → map display (agent path)
- Pi extension spawns binary `--mcp` (pi/exp-rs-spatial.ts:60-100 → pi/mcp_bridge.ts:192).
- Handshake: initialize (30s deadline) → tools/list includeSchemas (exp-rs-spatial.ts:181) → union surface `collectSurfaceTools()` (src/agent/tool_catalog/surface_registry.cpp).
- Execute: MCP tools/call → `ToolCallDispatcher` → TaskCenter async `execution_id` (mcp_server.cpp:1924-1991); workflow via `run_workflow` → `WorkflowRunCoordinator::startTrackedPipeline` (mcp_server.cpp:2883-2935) — THE SAME coordinator the GUI ProcessingHistoryPanel and CLI use (single fact chain, no parallel status table).
- Status/progress: Surface-11 `notifications/progress` (mcp_server.cpp:1367-1419, subscribed+rate-limited); cancel via `notifications/cancelled` keyed by rpc id with poller blacklist (mcp_server.cpp:707-747, 1177-1190).
- Result display: workspace tools (`spatial:display_*`) render through the same ActiveViewHost raster display service.
- Automated evidence: pi suite (node --test, this track: 41 tests 0 fail on Windows, full set on POSIX), test_mcp_server (real stdio wire incl. progress+cancel+artifact_read), test_r14_C_mcp_workflow_cancel, test_r22_agent_* fail-closed guards ✅.

## Path 4 — Layer editing → undo/redo → exit (edit lease path)
- Start editing: `updateEditingUI` gates 19 digitizing commands (main_window_menus.cpp:246) on `QgsVectorLayer::startEditing`; attribute table + vector tools bracket `acquireEditLease/commit/rollback` (main_window_vector.cpp:777-856; unified by #1677).
- One-editor-per-asset: `DataManager::acquire(LeaseKind::Edit)`; siblings made read-only (`setAssetLayersReadOnly` main_window_vector.cpp:758).
- Undo/redo: layer-scoped `vl->undoStack()` (main_window_vector.cpp:136-151); agent-side structured undo only for symbology (workspace_commands.cpp) — layout edits deliberately stay on QGIS-native undo (workspace_commands.h:5-9).
- Exit cleanup: leases released in destructor (main_window.cpp:431-432).
- Automated evidence: test_r14_D_vector_edit_honesty, test_layers, test_layer_sync_contract ✅.

## Path 5 — MapSpec → compose → preview → export (cartography path)
- Author: `cartography:*` tools or CartographyDock (src/app/cartography/cartography_dock.h:39) — dock submits THE SAME RSOperators (cartography_dock.h:83-93) to TaskCenter as headless (single execution authority).
- Compose: `resolveCompositionPass` (composition.h:225) → `MapSpecCompiler::compile` (mapspec_compiler.cpp:285) builds QgsPrintLayout via LayoutService.
- Preview: dock renders page 0 of the compiled layout at bounded DPI ("a projection, never a second renderer", cartography_dock.h:14-16).
- Export: `exportMapLayout` atomic temp→sha256→rename (export.h:97) png/pdf/svg; atlas png per-page ≤512 (export.h:159); one-shot `produceMap` with cooperative cancel (produce.cpp:134).
- Automated evidence: test_cartography_{library,templates,quality,production_11}, test_layout_tools, test_mapspec ✅; NEW this track: layout menu honesty case (no action-less Atlas/Report menus) ✅.

## Path 6 — Task failure → diagnose → recover (workflow recovery path)
- Failure surfaces: TaskCenter typed failure → RsJobPanel row + ProcessingHistoryPanel (both subscribe taskUpdated/runStateChanged queued); MCP `get_execution_status`/`get_workflow_status` read the same run records.
- Diagnose: `explainRun`/`explainDump` (workflow_run_coordinator.h:113-115; TaskCenter::explainDump); workflow failures map to `WorkflowRepairEngine` rules (CrsMismatch→gdal:reproject etc.).
- Recover: `resumeRun` skips steps whose outputs verify (size/mtime/SHA + operator stamp, workflow_run.h:70-91); checkpoint atomic tmp+fsync+rename (workflow_checkpoint.h:37-44); crash recovery `recoverInterruptedRuns` (:63); MCP `resume_workflow` mirrors it.
- Automated evidence: test_workflow_crash_recovery_r4, test_workflow_recovery, test_workflow_checkpoint_cache, test_workflow_resume_provenance, test_r14_C_mcp_workflow_cancel ✅.

## Honest-UI additions verified this track
- Layout designer menu bar carries only menus with actions (NEW regression case, r24-c); Atlas/Report capability reported as nullptr per port convention.
- Workspace-state contract now actually lists charts (the tool surface already promised "charts" in its schema description — tests/surface_diff_snapshot.json:18124).
