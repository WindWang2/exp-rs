# Architectural Audit & Seams Analysis

## 1. Deep Modules / Narrow Interfaces Audit

### Processing Framework & UI Seams
- **TaskCenter (`src/processing/framework/task_center.cpp`)**:
  - Serves as the central execution engine managing async tasks, worker thread pools, cancellation signals, and progress tracking.
  - Hardening target: Ensure cancel timing poll budgets are deterministic under heavy load; ensure detached worker threads clean up resources without race conditions during application shutdown.
- **ProcessingRegistry (`src/processing/framework/processing_registry.cpp`)**:
  - Manages algorithm discovery, metadata registration, parameter schema definitions, and algorithm lookup.
  - Seam audit: Clean isolation between `RSOperator`, QGIS algorithms, and GUI dialogs. Ensure dialogs use `AtomicAlgorithmRegistry` / parameter schemas rather than hardcoded parameter maps.
- **GUI Bridge (`src/app/dialogs/raster_processing_dialog_base.cpp`, `gui_job_adapter.cpp`)**:
  - Bridges background `RSOperator` tasks with Qt UI main thread via signals/slots.
  - Lifetime Seam: Ensure dialog destruction during background task execution cleanly detaches or cancels task without invoking dangling `QPointer` callbacks or crashing on thread context teardown.

### Python Worker & Isolated Subsystem
- **IPC & Shared Memory Isolation (`src/python/isolated/`)**:
  - C++ `PythonPluginHost` launches out-of-process Python workers (`worker_daemon.py`).
  - Data transfer occurs over IPC socket and zero-copy shared memory (`SharedMemorySegment`).
  - Architecture Seam: Resource cleanup (IPC socket files, shared memory keys, temp rasters) must be robust against worker crash or timeout.

## 2. Identified Coupling & Architectural Hazards

1. **Path Resolution Coupling in GUI and Worker Host**:
   - `AppPaths` and `PythonPluginHost` walk directory trees searching for `"CMakeLists.txt"` to locate resources and scripts.
   - **Hazard**: Breaks when application is installed in standard binary locations (`/usr/bin`, `C:\Program Files`).
   - **Fix**: Introduce robust fallback lookup: check executable dir, installed `../share/sicnu_geo_rs/`, then development tree.

2. **Error Translation Duplication**:
   - Algorithms translate GDAL/Qt errors independently.
   - **Fix**: Standardize error propagation in `RSOperator::execute()` returning unified error payloads.

3. **Thread Affinity & GUI Callbacks**:
   - Async task callbacks invoked from worker threads must dispatch to GUI thread using `QMetaObject::invokeMethod` with `Qt::QueuedConnection` or `QPointer` protection.
