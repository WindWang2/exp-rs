# Specification: Advanced Task Async Scheduling and DAG Pipelines

## Problem Statement

While basic task registration and async monitoring exist in `TaskCenter`, complex multi-stage remote sensing analysis workflows require:
1. **Task Dependency DAGs**: Execution of dependent multi-stage workflows where downstream tasks must wait for upstream parent tasks to finish.
2. **Dynamic Upstream Parameter Flow**: Automatic substitution of parent output dataset paths into downstream child task parameter maps.
3. **Priority & Resource Throttling**: Ordering tasks by priority (`High`, `Normal`, `Low`) and bounding max concurrent background threads to prevent UI canvas starvation.
4. **Hierarchical Task Center Visualization**: Visual tree indentation for parent-child tasks and color-coded priority badges.

## Solution

Extend **`TaskCenter`** and **`TaskCenterDock`** with DAG pipeline scheduling, priority queue sorting, and resource throttling:
1. **Priority & Throttling**: Add `TaskPriority` enum (`High`, `Normal`, `Low`) to `AlgorithmTaskInfo`. Auto-cap max concurrent running tasks to `std::thread::hardware_concurrency() - 1`.
2. **DAG Dependency Gating**: Add `QList<long> parentTaskIds` to `AlgorithmTaskInfo`. Gated tasks stay `Queued` until all parent tasks reach `Completed`. If any parent task fails, downstream child tasks are automatically canceled.
3. **Upstream Placeholder Substitution**: Substitute `${task.<parent_id>.output}` template placeholders in child parameter maps with parent task output paths upon parent completion.
4. **Hierarchical UI Workspace**: Render child tasks indented under parent nodes in `TaskCenterDock` with color-coded Priority badges (`[高]`, `[中]`, `[低]`).

## User Stories

1. As a Remote Sensing analyst, I want to submit multi-stage algorithm pipelines (e.g. Atmospheric Correction → Spectral Index → Classification), so that downstream steps automatically wait for upstream steps to finish.
2. As a user, I want downstream tasks to automatically receive upstream output raster file paths via `${task.<parent_id>.output}` placeholders, so that I don't have to manually configure intermediate file paths.
3. As a user, I want high-priority jobs to run ahead of low-priority jobs in the queue, so that critical tasks complete faster.
4. As a user, I want background tasks to be capped to `hardware_concurrency() - 1` CPU cores, so that the main application window and map canvas remain responsive.
5. As a user, I want to see parent-child task relationships displayed as an expandable tree hierarchy in `TaskCenterDock`, so that pipeline dependencies are visually clear.
6. As a user, I want if an upstream task fails, downstream tasks to automatically cancel, so that invalid runs don't waste system resources.

## Implementation Decisions

- **Data Models**:
  - `TaskPriority` enum (`High`, `Normal`, `Low`) in `src/processing/framework/task_center.h`.
  - `AlgorithmTaskInfo`: Add `TaskPriority priority = TaskPriority::Normal;` and `QList<long> parentTaskIds;`.
- **Scheduling Logic**:
  - `TaskCenter::enqueueTask`: Accepts `TaskPriority` and `QList<long> parentTaskIds`.
  - `TaskCenter::dispatchQueuedTasks`: Sorts candidate queued tasks by priority (`High` > `Normal` > `Low`) and submission time. Checks if parent tasks are completed; substitutes `${task.<parent_id>.output}` placeholders in parameters.
- **UI Panel**:
  - `TaskCenterDock`: Update `m_taskTree` columns to `{ID, 算法名称, 优先级, 状态, 进度, 已用时间, 预计剩余}`. Render child tasks as child `QTreeWidgetItem` nodes under parent nodes.

## Testing Decisions

- **Testing Seams**:
  1. `tests/test_task_center.cpp`: Priority queue sorting, DAG parent dependency gating, cascade failure cancellation, and placeholder parameter substitution.
  2. `tests/test_task_center_dock.cpp`: Tree item hierarchy rendering and Priority column badge formatting.

## Out of Scope

- Distributed GPU cluster remote nodes (session stays bounded to local multi-core desktop CPU execution).

## Further Notes

- Documented in ADRs: `0002-task-priority-and-resource-throttling.md`, `0003-dag-task-pipeline-dependencies.md`, `0004-upstream-output-placeholder-substitution.md`, `0005-task-center-dock-dag-and-priority-visualization.md`.
