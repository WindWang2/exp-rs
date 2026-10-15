# Track R13-L: File Ownership & Change Boundaries

## Allowed Production Files
- `src/app/agent_ops/agent_ops_control_center_dock.h`
- `src/app/agent_ops/agent_ops_control_center_dock.cpp`
- `src/app/teaching_admin/teaching_admin_dock.h`
- `src/app/teaching_admin/teaching_admin_dock.cpp`
- `src/app/experiment_studio/experiment_studio_dock.h`
- `src/app/experiment_studio/experiment_studio_dock.cpp`
- `src/app/workflow/pipeline_editor_dock.cpp`
- `src/app/main_window_vector.cpp`

## Allowed Test Files
- `tests/test_workflow_pipeline_ui.cpp`
- `tests/test_experiment_studio_dock.cpp`
- `tests/test_agent_ops_ui_smoke.cpp`
- `tests/test_teaching_admin_dock_smoke.cpp`

## Forbidden Files (Out of Scope / Owned by Other Active PRs)
- All `src/geospatial/**` (PR #1504, #1505)
- All `src/python/**` (PR #1505)
- All `src/runtime/gpu/**` (PR #1505)
- All `src/science_context/**` (PR #1505, #1509)
- All `src/operators/**` (PR #1506, #1508, #1509)
- All `src/plugins/**`, `src/sdk/**` (PR #1507)
- All `src/workflow/pipeline_run_coordinator.*` (PR #1507)
