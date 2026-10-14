# Domain Ownership & Boundary Matrix — R12-A Science Numerics

## Scope Matrix

| Domain | Authorized Scope | Owner | Policy |
|---|---|---|---|
| **A (Current Task)** | `src/analysis/**`, `src/processing/algorithms/**`, `src/operators/rs/**`, `src/core/temporal_cube*`, `src/core/radiometric_state*`, science tests | Science Algorithm Owner | **Exclusive Read/Write** |
| **B** | `src/geospatial/**`, `src/data/**`, `src/dataset/**`, `src/experiment/**`, `src/experiment_studio/**` | Spatial / IO Owner | **Read-Only** (use public headers e.g. `atomic_fs.h`) |
| **C** | `src/agent/**`, `src/workflow/**`, `src/runtime/**`, `pi/**`, `src/processing/framework/task_center.*` | Execution Engine Owner | **Read-Only** |
| **D** | `src/app/**`, `src/gui/**`, `src/ui/**`, `src/teaching/**`, `src/lab/**` | UI / Teaching Owner | **Read-Only** |

## Cross-Domain Coordination Protocol
- For atomic file writing in `rs_segment_map.cpp`: Call existing `sicnu::geospatial::atomic_write_file` or write to temporary path with atomic rename. Do NOT alter files inside `src/geospatial/`.
- Single-writer guarantee: No other parallel worktree or subagent writes to domain A files.
- Shared domain files (`src/suitability/**`, `src/scientific_state/**`): Default read-only.
