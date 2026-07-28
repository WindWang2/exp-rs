# 03 — TaskCenterDock UI Workspace & Main Window Integration

**What to build:** The dockable `TaskCenterDock` panel in `QgisDesktopWindow` featuring the main task list, progress bars, expandable parameter & log inspector, lifecycle buttons (Pause/Cancel/Retry/Clear), auto-load layer toggle, and offscreen GUI tests in `test_task_center_dock.cpp`.

**Blocked by:** 02 — TaskCenter Controller & Task Execution Manager

**Status:** completed

- [x] Implement `TaskCenterDock` UI component in `src/app/panels/task_center_dock.{h,cpp}`
- [x] Connect `TaskCenterDock` into `QgisDesktopWindow` in `main_window_docks.cpp` and `main_window_connections.cpp`
- [x] Implement expandable detail inspector for parameter schemas and live log streaming
- [x] Add auto-load output layer toggle handler
- [x] Add offscreen GUI unit tests in `tests/test_task_center_dock.cpp`
