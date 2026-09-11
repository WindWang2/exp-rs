# OWNERSHIP — authoritative seams and this track's edits

## Authorities (unchanged, verified on master)

| Concern | Owner | This track may |
|---|---|---|
| Agent loop | Pi (agent/) | not touch |
| Workflow execution | `WorkflowRunCoordinator` → `TaskCenter` → `JobEngine` → executors | consume `runStateChanged` + read APIs only |
| Operator surface | `RSOperatorRegistry` | not touch |
| Model execution | `IModelRuntime` / `runModelInference` | not touch |
| Map/rendering | QGIS | not touch |
| Dataset persistence | `DatasetStore` (src/dataset) | extend additively only if a verified gap demands |
| Experiment persistence | `ExperimentStore` (src/experiment) | extend additively (recorder/bridge) |
| Geospatial I/O | src/geospatial | not touch |
| Artifact publication | governed output/committer seams | not touch |
| Command/UX state | CommandRegistry/ContextRules/workbench | not touch |
| MCP entry | `src/agent/mcp_server.cpp` tool table | add opt-in args to run_workflow + extend data_platform_tools additively |
| CLI entry | `src/cli/cli_dataset_commands.cpp` | additive verbs only if needed |

## Files this track expects to CREATE

- `src/experiment/run_bridge.{h,cpp}` — ExecutionEvent vocabulary + bridge
  state machine (in `sicnu_experiment`).
- `src/experiment/bridge/CMakeLists.txt`,
  `src/experiment/bridge/workflow_experiment_adapter.{h,cpp}` — WorkflowRun →
  ExecutionEvent conversion + `WorkflowExperimentMonitor` QObject (new static
  target `sicnu_experiment_bridge` linking `Sicnu::experiment` +
  `sicnu_workflow`; layering-safe: workflow stack has no experiment edge).
- `tests/test_mlops8_bridge.cpp`, `tests/test_mlops8_e2e.cpp`.
- `docs/adr/0143-workflow-experiment-auto-recording.md`.
- `.planning/dataset-experiment-mlops-8/*` (this directory).
- `docs/experiments/auto-recording.md` (+ README/index touch-ups).

## Files this track expects to MODIFY (shared-file discipline)

- `src/experiment/run_recorder.{h,cpp}` — add `markInterrupted` (additive).
- `src/experiment/CMakeLists.txt` — only if files join sicnu_experiment.
- `src/agent/mcp_server.cpp` + `src/agent/data_platform_tools.{h,cpp}` —
  run_workflow opt-in recording args (backward compatible; agent track owns
  the file — keep the diff small and additive).
- `src/agent/CMakeLists.txt`, `src/cli/CMakeLists.txt` — link the new bridge
  target where used.
- `CMakeLists.txt` (root) — one `add_subdirectory(src/experiment/bridge)`
  line after `src/workflow`.
- `tests/CMakeLists.txt` — new test targets.
- `CHANGELOG.md`, `docs/experiments/*` — documentation sync.

## Files this track must NOT restructure

`workflow_run_coordinator.*` (execution-plane track #828 owns the seam; if a
coordinator change seems required, prefer an additive consumer-side solution
and record why in ARCHITECTURE.md).
