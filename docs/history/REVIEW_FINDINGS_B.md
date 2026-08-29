# REVIEW_FINDINGS_B — Agent / Pi / MCP / Workflow / GUI / Result Interaction

Date: 2026-08-27. Base: `origin/master` @ `19cf9269ac` (includes ExecutionPlane #578, DataSourceResolver #577).
Scope: PART B only (src/agent, pi, MCP, workflow/planner, tool catalog/dispatcher integration, app/GUI, result presentation). PART A read-only.

## End-to-end flow status (request: "提取建筑物并显示在地图上" / "NDVI 并显示结果")

| Stage | Status | Evidence |
|---|---|---|
| User → Copilot entry | Real | `src/agent/agent_copilot_dock_widget.cpp:249/266` |
| Workspace snapshot | Real | `src/agent/workspace_snapshot.cpp:122` |
| Intent parsing / planning | **Prompt convention only** | `agent_copilot_dock_widget.cpp:270-300` |
| PlanRequest detection | Heuristic (`steps` array) | `tool_call_dispatcher.cpp:219-224, 358-384` |
| Tool selection | Prompt convention (OpenAI tool defs) | `agent_tool_catalog.cpp:479-522` |
| Preflight | Real but **not wired into agent path** | `algorithm_preflight.cpp`; `execution_plane.cpp:63` |
| Execution via ExecutionPlane/TaskCenter | Real (algorithms); interaction/view/spatial tools bypass by design | `tool_call_dispatcher_task_center.cpp:37`; `execution_plane.cpp:63` |
| Output commit + asset registration | Real | `output_committer.cpp:83`; `data_manager.cpp:238` |
| Layer load | Real but **double-tracked / broken** (see P0-L1) | `task_center.cpp:1118`; `qgis_display_manager.cpp:288`; `main_window_docks.cpp:536` |
| Result verification | **None** (committer only checks openable) | `output_committer.cpp:62-72` |
| Final answer | **Not evidence-based** — tool results never fed back to LLM | `agent_copilot_dock_widget.cpp:352/548` |

## P0 — crash / UAF / wrong result

- **P0-A1 Dock UAF**: `watchToolCallCompletion` stores `[this]` callbacks in `m_pendingToolCallCompletions`; dtor only disconnects `m_client`. Background task completing after dock close → UAF. `agent_copilot_dock_widget.cpp:407-432, 538-545`.
- **P0-A2 Interaction registry dangling handlers**: singleton `InteractionToolRegistry` lambdas capture `ViewControlService*`/`RasterDisplayService*` owned by the dock; after dock destroy any `view:`/`raster:`/`roi:` call (MCP, second copilot) is UAF. `interaction_tool_registry.cpp:396-525`, registered at `agent_copilot_dock_widget.cpp:164`.
- **P0-A3 OutputCommitter handler captures raw `DataManager*`**, used from async ExecutionPlane watch callback. `tool_call_dispatcher.cpp:65-99`.
- **P0-L1 Double/broken layer load**: TaskCenter emits `layerAutoLoadRequested` with the *temp* path (`task_center.cpp:1117-1118` → `main_window_docks.cpp:536`), while `OutputCommitter::commit` *moves* temp→stable (`output_committer.cpp:83`) and `QgisDisplayManager` auto-adds the stable asset (`qgis_display_manager.cpp:288/406`). Result: duplicate layers or layers pointing at moved-away temp files. Also `OutputCommitter::displayRequested` has no listener (`output_committer.cpp:213`).
- **P0-L2 Algorithm dialog double load**: `sicnu_algorithm_dialog.cpp:886-888` loads result layers directly AND `submitJob(autoLoad=true)` triggers `layerAutoLoadRequested`.
- **P0-R1 Final answer without evidence**: LLM streams the "final" message before/without tool results; no second LLM round with execution results; no verification gate before claiming success.
- **P0-R2 Output verification absent**: committer checks only structural openability; no size/band/CRS/NoData sampling, no vector feature/geometry checks.

## P1 — execution correctness / verification / map finalization

- **P1-E1 Plan path bypasses ExecutionPlane/OutputCommitter**: `agent_workflow_executor.cpp:103/124` uses `submitPipeline(autoLoad=false)`; plan-step outputs never committed/registered/loaded.
- **P1-E2 Result payload lacks identity**: `buildCommittedResultPayload` returns only stable path/status/error — no assetId/layerId/CRS/bbox/featureCount. `tool_call_dispatcher.cpp:521-566`; `execution_plane.cpp:140`.
- **P1-E3 No repair/retry wiring**: `TaskCenter::retryTask` exists (`task_center.cpp:1143-1157`) but agent never uses it; no bounded repair (band mismatch, param correction). Failures only surface `errorMessage`.
- **P1-E4 Agent state model is a single bool** (`m_isStreaming`); no planning/running/verifying/presenting stages. No run history.
- **P1-E5 No session/history**: `sendPrompt` resets `m_messageHistory` each turn (`agent_copilot_dock_widget.cpp:278-288`) — multi-turn closed loop impossible without at least per-run follow-up.
- **P1-M1 No zoom to result when canvas already has layers**: `active_view_host.cpp:264-285, 311-345` only sets extent when no visible layers existed.
- **P1-M2 `refreshCanvasLayers` reads `QgsProject::checkedLayers()` instead of the DisplayManager view tree** — wrong layers in multi-view. `active_view_host.cpp:540-551`.
- **P1-M3 Misclassified vector output**: dispatcher defaults suffix `tif` when output path has no extension → asset kind wrong. `tool_call_dispatcher.cpp:69-70`.
- **P1-X1 Cross-surface semantic forks**: `list_layers`/`describe_dataset`/`get_lineage` differ between MCP meta tools, `data:*` registry, unified catalog (`mcp_server.cpp:208-300` vs `interaction_tool_registry.cpp:537-782` vs `data_tool_provider.cpp:8-123`); ROI/view schemas fork (`canvas:draw_roi` vs `roi:set`; `canvas:zoom_to_extent` vs `view:set_extent`).
- **P1-X2 Structured error codes dropped at MCP boundary**: `SpatialToolResult.errorCode` flattened to string (`mcp_server.cpp:1646-1647`); interaction `{status,errorMessage}` flattened (`mcp_server.cpp:1120-1124`).
- **P1-X3 Workspace path validation missing** for `spatial:*` tools and `run_workflow` (`mcp_server.cpp:1628-1652` vs 1093-1096).

## P1 — GUI / TaskCenter UI

- **P1-U1 `WaitingResource`/`Cancelling` not rendered**: `task_center_dock.cpp:93-104` shows "未知"; cancel button disabled for them (`:210-213`); `rs_job_panel.cpp:402-406` filter drops them; `workflow_session_controller.cpp:431-439` ignores them; `gui_job_adapter.cpp:164-173` no progress callback.
- **P1-U2 Copilot Stop doesn't cancel TaskCenter tasks** — only `m_client->cancel()` for streaming. `agent_copilot_dock_widget.cpp:251-256`.
- **P1-U3 Tool-call cards show no progress/stage/artifact**; success path silent. `agent_copilot_dock_widget.cpp:462-477`.
- **P1-U4 `onClearClicked` doesn't clear `m_pendingToolCallCompletions`** — stale callbacks write into reset labels. `agent_copilot_dock_widget.cpp:229-247`.

## P2 — observability / consistency / maintainability

- Dead UI: `task_center_dock` autoLoad checkbox unused; dock itself commented out in `main_window_docks.cpp:535`.
- `rs_job_panel.cpp:720-755` `loadPathsToMain` no dedup → duplicate layers.
- `rs_job_panel.cpp:339-346` progress truncated to int.
- `task_center_dock.cpp:116-159` full tree rebuild per update.
- `provider_algorithm_adapter.cpp:350` own `std::thread` for cancel polling.
- `ViewControlService`/`RasterDisplayService` use `BlockingQueuedConnection` (`raster_display_service.h:98`, `view_control_service.h:82`).
- Pi bridge flattens all results to text; `details:{}` empty (`pi/exp-rs-spatial.ts:324-327`).
- Tool-id normalization differs per surface (`agent_tool.cpp:49-55` vs `interaction_tool_registry.cpp:325-333` vs `pi/exp-rs-spatial.ts:195-197`).
- MCP headless detection via `view:get_state` presence is fragile (`mcp_server.cpp:561-562`).

## P3 — enhancements

- Building extraction is a tracer bullet (`rs:infer` NCHW raster only; no vectorization despite catalog claiming polygon/vector outputs). `rs_inference_operator.cpp:84`; `models/README.md`.
- WorkspaceSnapshot lacks per-DisplayLayerId visibility/tree order.
- Spatial tool `outputSchema`s are empty shells.

## Test coverage gaps (vs required list)

Missing: repairable failure, retry limit, processing complete→verify, verification-gated final response, final map state, agent-driven layer visibility, run inspector state transitions. Partial: GUI close during run, unrepairable failure. Reusable fakes: `FakeDispatcherHarness` (test_tool_call_dispatcher.cpp:53), `BehavioralStubAdapter` (test_execution_plane.cpp:84), `TestMcpServer` (test_mcp_server.cpp:39), `ContractStubAdapter` (test_preflight.cpp:32), `RsSyntheticRasterBuilder` (synthetic_raster_builder.h:31), `QgisFixture` (test_layer_tree_bridge.cpp:17).

## Fix/development plan (work packages)

- WP1 P0 lifetime + double-load fixes (A1-A3, L1-L2, U4) + regressions.
- WP2 OutputVerifier (raster/vector, light NoData sampling) + result payload enrichment (assetId, kind, verification summary).
- WP3 AgentRunCoordinator: stages, bounded repair (MAX_REPAIR_ATTEMPTS=2), verification gate before success, evidence-fed final response (tool-result follow-up round).
- WP4 GUI: TaskCenter UI states (U1), copilot stop→cancel (U2), lightweight run inspector section (U3/E4), MapFinalizer via QgisDisplayManager/ViewControlService (M1).
- WP5 Tests per gap list; second deep review; PR.
