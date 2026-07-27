# Specification: Algorithm Engine Integration and Task Center

## Problem Statement

Currently, algorithm execution across SICNU GEO RS is fragmented across multiple distinct execution channels:
- Native QGIS processing algorithms via `QgsProcessingAlgorithm`
- Standalone custom `QgsTask` subclasses instantiated directly inside specific dialogs (e.g. classification, OBIA, SIFT, georeferencing)
- Legacy custom `JobEngine` handlers

Because these algorithms execute independently, users lack a single, unified view of all ongoing, queued, or completed tasks in the main workspace. Furthermore, processing dialogs frequently block or lack a standardized way to dispatch long-running tasks into the background while keeping the main map canvas responsive.

## Solution

Build a unified **Algorithm Engine** (`AlgorithmEngine`) and a centralized **Task Center** (`TaskCenterDock`) dock panel in the main window:
1. **Algorithm Engine Facade**: Provide a single registration and dispatch engine that accepts native `QgsProcessingAlgorithm` instances and custom C++ tasks wrapped via a `TaskAlgorithmAdapter`.
2. **Task Center Dock Panel**: Implement a rich, dockable UI workspace (`TaskCenterDock`) in `QgisDesktopWindow` that displays real-time task status, progress bars, parameter schemas, live execution log streams, and task lifecycle controls (Pause/Resume/Cancel/Retry/Clear).
3. **Dual-Mode Asynchronous Dialog Dispatching**: Standardize algorithm dialogs so clicking "Run" dispatches the task to the Task Center. Users can monitor live progress within the dialog or click "Run in Background" to close the dialog while the task continues running seamlessly in `TaskCenterDock`.

## User Stories

1. As a Remote Sensing analyst, I want all processing algorithms registered in a central Algorithm Engine, so that every algorithm has consistent input validation, execution context, and logging.
2. As a GIS user, I want a Task Center dock panel in the main application window, so that I can see all active, queued, and completed algorithm tasks in one place.
3. As a user, I want to see detailed task information in the Task Center (ID, Algorithm Name, Status, Progress %, Elapsed Time, Remaining Time), so that I can monitor heavy processing jobs.
4. As a user, I want to expand any task in the Task Center to inspect its input parameter values, so that I can verify the exact settings used for a run.
5. As a user, I want to see real-time log messages and error tracebacks within the expanded task view, so that I can diagnose why a task failed or check intermediate outputs.
6. As a user, I want to pause, resume, or cancel any running task in the Task Center, so that I can free up system resources when needed.
7. As a user, I want a "Retry" button on failed tasks, so that I can quickly re-run a job without reopening and reconfiguring the dialog from scratch.
8. As a user, I want to toggle "Auto-add output layer to map canvas" in the Task Center, so that generated raster/vector outputs are automatically loaded onto the map upon completion.
9. As a user running a long processing task, I want to click "Run in Background" in any algorithm dialog, so that the dialog closes and I can continue viewing and measuring data on the map canvas.
10. As a developer, I want custom tasks (like SIFT matching, OBIA segmentation, and Classification) to easily plug into the Algorithm Engine using `TaskAlgorithmAdapter`, so that existing algorithms don't require massive rewrites.

## Implementation Decisions

- **Architecture**:
  - `AlgorithmEngine` (singleton manager in `src/processing/framework/algorithm_engine.{h,cpp}`): Registers `AlgorithmDescriptor` items, manages native `QgsProcessingAlgorithm` objects, and handles `TaskAlgorithmAdapter` instances.
  - `TaskCenter` (backend controller in `src/processing/framework/task_center.{h,cpp}`): Built on top of `QgsTaskManager` / `QgsApplication::taskManager()`. Manages task queueing, progress signal mapping, log buffer storage, and lifecycle execution (pause/resume/cancel/retry).
  - `TaskCenterDock` (UI widget in `src/app/docks/task_center_dock.{h,cpp}`): A `QDockWidget` added to `QgisDesktopWindow` containing a summary table (`QTreeWidget` / `QTableView`) and a side-by-side / bottom expandable detail inspector for parameter inspection and log viewing.
  - `TaskAlgorithmAdapter`: A adapter wrapping custom `QgsTask` tasks to expose the standard `AlgorithmDescriptor` parameter and metadata schema.

- **Data Models**:
  - `AlgorithmTaskInfo`: Struct holding `taskId`, `algorithmId`, `algorithmName`, `status` (`Queued`, `Running`, `Paused`, `Completed`, `Failed`, `Canceled`), `progressPercentage`, `startTime`, `endTime`, `parameterMap` (`QVariantMap`), `logBuffer` (`QStringList`), and `autoLoadLayer` (`bool`).

- **Main Window Integration**:
  - `m_taskCenterDock` registered in `QgisDesktopWindow::setupDocks()` and hooked up via `main_window_connections.cpp`.
  - Signal mapping connecting `TaskCenter` updates to `TaskCenterDock` and status bar indicators.

## Testing Decisions

- **Testing Philosophy**:
  - Focus strictly on external behavior, interface contracts, state transitions, and UI signal emissions.
  - Avoid binding tests to internal UI pixel coordinates or private helper functions.

- **Target Test Modules**:
  1. `tests/test_algorithm_engine.cpp`: Unit tests for `AlgorithmEngine` registry, parameter validation, and adapter instantiation.
  2. `tests/test_task_center.cpp`: Controller tests for task queueing, state transitions, progress aggregation, log buffering, and cancel/pause logic.
  3. `tests/test_task_center_dock.cpp`: Qt QTest/Catch2 offscreen tests for `TaskCenterDock` widget creation, model populating, button click signal handling, and detail expansion.

- **Prior Art**:
  - `tests/test_processing_framework.cpp`
  - `tests/test_async_algorithm_runner.cpp`
  - `tests/test_job_engine.cpp`

## Out of Scope

- Distributed/remote cloud cluster worker nodes (all execution is local multi-threaded).
- Database persistence for task history across application restarts (task history is session-scoped).

## Further Notes

- Maintains 100% compatibility with QGIS core processing APIs.
- Cleanly replaces fragmented task tracking logic across separate dialogs with a single source of truth.
