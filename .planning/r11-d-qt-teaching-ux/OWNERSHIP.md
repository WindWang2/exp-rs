# Ownership & Cross-Domain Boundaries — Direction D

## Write Scope (Direction D Exclusive)

The following paths belong to Direction D:
- `src/app/**` (specifically UI, shell, ribbon, dock widgets, main_window vector/clipboard)
- `src/ui/**`, `src/gui/**`
- `src/help/**`, `data/help/**`
- `src/teaching/**`, `src/teaching_admin/**`
- `src/experiment_studio/**`, `src/app/experiment_studio/**`
- `tests/**` (Direction D-specific test cases, append-only to `tests/CMakeLists.txt`)

## Read-Only Boundaries (Other Tracks)

- **Track A (Science & Algorithms)**: `src/core/**`, `src/operators/**`, `src/algorithms/**`, `src/radiometric/**` — **READ-ONLY**.
- **Track B (Geospatial & Data Fabric)**: `src/data/**`, `src/dataset/**`, `src/geospatial/**` — **READ-ONLY**.
- **Track C (Agent & Execution Runtime)**: `src/sdk/exprs/**`, `src/python/isolated/**`, `src/processing/framework/task_center.*` — **READ-ONLY**.

## Collision Avoidance with In-Flight PRs

- **PR #1476**: Do not touch `guided_workflow_widget.cpp`, `rs_scan_pool.h`, `fault_transforms.cpp`, or `script_adapters.*`.
- **PR #1478**: PR #1478 modified `pipeline_editor_dock.cpp`, `batch_assessment.cpp`, `workbenches.json`, `command_defs.cpp`, `experiment_studio_dock.cpp`.
  - For `terminology_provider.cpp`, PR #1478 did NOT touch it (deferred as known P2 residual). Track D will address the deduplication fix and fail-closed resource checks.
  - For `main_window_vector.cpp`, PR #1478 did NOT touch it. Track D will address clipboard & selection feedback.
  - For `ribbon_controller.cpp`, PR #1478 did NOT touch it. Track D will address Vector Editing tool gating and availability contracts.
  - For `experiment_studio_dock.cpp`, PR #1478 added synthetic checks. Track D will optimize the matrix table loading model without conflicting with #1478's checks.
