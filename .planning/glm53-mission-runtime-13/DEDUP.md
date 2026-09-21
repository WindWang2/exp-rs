# DEDUP — what already exists vs. the real gaps

Evidence: live `origin/master` at 79adfe78a, PR #1121 body/diff, source archaeology of
`src/app/workbench/`, `src/agent/`, `src/app/main_window_*`, `pi/exp-rs-spatial.ts`,
`tests/CMakeLists.txt`.

## Already implemented (do NOT redo)

| Capability | Authority | Evidence |
| --- | --- | --- |
| MissionContext value model + QJson (kind `mission_context`, v1.0), sidecar + project-XML dual write, SHA-256 fingerprint | `src/app/workbench/mission_context.{h,cpp}`, `mission_context_store.{h,cpp}` (D18, #991) | mission_context.cpp:253-321; mission_context_store.cpp:16-268 |
| Project read/write hooks for the mission context (restore/persist, status bar, failure dialog) | `main_window_connections.cpp:113-116, 347-414` | grep of callers |
| Mission stage machine, task timeline value model, event log, revision, fail-closed transitions, run-authority binding, retry lineage, reconciliation + rename rewrite | `mission_stage.{h,cpp}` (#1121) | mission_stage.h:187-326 |
| One projection for GUI/MCP/Pi + surface identity registry (mission:context / mission:timeline / mission:advance declared) | `mission_projection.{h,cpp}` (#1121) | mission_projection.cpp:168-199 |
| Paged timeline model with row-scoped dataChanged + instrumentation | `mission_timeline_model.{h,cpp}` (#1121) | model cpp:24-123 |
| MCP allow-list prefix `mission:` and Pi category `mission` | `surface_registry.cpp:174`; `pi/exp-rs-spatial.ts:83` | grep |
| Stage/parity/scale gates (3 registered targets) | `tests/test_mission_stage|surface_parity|scale_benchmark.cpp` | tests/CMakeLists.txt:10246-10248 |
| Layer rename/delete reconciliation *algorithms* | `mission_stage.h:239,305,310,322` | no production caller exists |

## Real gaps this Track closes (code evidence for each)

| # | Gap | Evidence |
| --- | --- | --- |
| G1 | `mission_timeline_bridge` does not exist: the timeline is never embedded into `MissionContext::metadata`; the header comment at `mission_timeline_store.h:12-14` describes a file that was never written | repo-wide grep: 2 hits, both comments |
| G2 | Timeline persistence has no authority and no production caller: `mission_timeline_store.cpp` is in no CMake target; nothing in `src/` calls save/load; only the unregistered e2e test does | `src/app/CMakeLists.txt:124-154`, `tests/CMakeLists.txt:10222-10225` |
| G3 | Two writable transports would drift: a `.mission-timeline.json` sidecar plus the MissionContext sidecar/XML can both be written with no precedence rule | design gap; no bridge exists |
| G4 | No schema migration for either artifact; both readers are strict single-version (`mission_context.cpp:290-296`, `mission_stage.cpp:583-585`); legacy 12.0 timeline sidecars cannot be adopted | grep `migrate` in src/app |
| G5 | No `mission:*` SpatialTool objects exist; `SpatialToolProvider::provideTools()` prefix filter lacks `mission:`; `mcp_server.cpp` tools/call chain has no `mission:` branch, so an allowed id falls through to "Algorithm not registered" | spatial_tool_provider.cpp:23-46; mcp_server.cpp:928-948, 966-978 |
| G6 | No mission surface in the desktop shell: no timeline panel/dock, no `mission.*` commands registered in `command_defs.cpp`, no selection channel for a mission task, no project read/write hook for the timeline, no layer delete/rename reconciliation wiring | grep `mission` in src/app finds only the D18 context |
| G7 | No mission↔execution binding in the runtime: nothing resolves `MissionRunRef{kind,id}` against TaskCenter / WorkflowRunCoordinator / PipelineRunCoordinator, and nothing prevents a crashed-and-reopened project from reporting a task as Running whose run no longer exists | no caller of the run authorities from the mission layer |
| G8 | `test_mission_workbench_e2e.cpp` is registered in no target — save/reopen/resume/recovery behaviour is currently unverified by CI-visible gates | grep in tests/CMakeLists.txt |

## Pivots

None required — no live master commit and no open PR implements any of G1-G8.

## Non-goals (respected, per prompt)

No rewrite of `src/workflow/**` durable IR, TaskCenter scheduler, temporal/phenology
algorithms, Model Runtime / SAR / Spectral / DataManager work; no MissionContext copy
or "MissionContext2".
