# OWNERSHIP — allowed paths, parallel domains, conflict hotspots

## Owned by this track (may modify)

- `src/app/workbench/mission_*.{h,cpp}` — the mission domain (context, stage, projection,
  timeline model/store, **new** `mission_timeline_bridge.*`, **new** `mission_runtime_store.*`).
  The two new files are additive; existing files get minimal, surgical hunks.
- `src/app/workbench/mission_timeline_panel.{h,cpp}` — **new** desktop panel (follows the
  `temporal_workbench_panel` / `processing_history_panel` pattern).
- `src/agent/spatial_tools/mission_tools.{h,cpp}` — **new** `mission:*` SpatialTool objects
  (QGIS-free by design so they are verifiable in the out-of-tree harness).
- Minimal hunks in:
  - `src/agent/spatial_tools/spatial_tool.cpp` (register the mission built-ins),
  - `src/agent/spatial_tools/spatial_tool_provider.cpp` (advertise the `mission:` family),
  - `src/agent/mcp_server.cpp` (dispatch branch for `mission:` — mirrors the
    `handleSpatialToolCall` shape),
  - `src/app/command_defs.{h,cpp}` + `src/app/main_window_workbench.cpp` +
    `src/app/main_window_connections.cpp` + `src/app/main_window.h` (shell wiring:
    commands, panel, project read/write hooks, layer delete/rename reconciliation),
  - `src/app/workbench/selection_context.{h,cpp}` (one additive field:
    `selectedMissionTaskId` + `notifyMissionTaskSelection`),
  - `pi/exp-rs-spatial.ts` — **read only unless a gap is proven** (the category literal
    already carries `mission`; the parity gate parses it).
  - `src/app/CMakeLists.txt` (add the two new workbench sources + panel),
  - `tests/CMakeLists.txt` — **append-only at the end** (repo convention, line 11462-11463).
- `.planning/glm53-mission-runtime-13/**`, `docs/adr/**` (one new ADR), `.goal-loop-ledger.md`
  (append-only).

## Forbidden (parallel domains)

- `src/workflow/**` (durable IR2 — owned by #1132), `src/processing/framework/task_center.*`
  (scheduler internals — owned by #1130), `src/operators/**` (model runtime), SAR/spectral
  algorithm trees, `src/geospatial/**` (#1137 open), offline labs (#1136 open), temporal
  phenology (#1135 open).
- `src/agent/mcp_server.cpp` is shared: only the mission dispatch hunk + the registration
  list are touched; no reformatting, no reordering of existing branches.
- `tests/CMakeLists.txt` is the known textual conflict hotspot with PRs #1135/#1136 — both
  append at the end; resolution = keep both appends.

## Shared-file protocol

`tests/CMakeLists.txt`, `src/app/CMakeLists.txt`, ADR index: edited by the main agent only,
at integration time, as append-only hunks. No subagent writes.
