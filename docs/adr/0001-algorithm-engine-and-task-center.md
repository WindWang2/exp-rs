# 0001 Algorithm Engine and Task Center Integration Architecture

We decided to unify all modular remote sensing algorithms under an `AlgorithmEngine` registry and route 100% of algorithm executions through a central `TaskCenter` dock panel (`TaskCenterDock`) built on `QgsTaskManager`. 

### Context & Decision
Previously, algorithm execution was fragmented across direct `QgsTask` dialog subclassing, custom `JobEngine` handlers, and `QgsProcessingAlgorithm` calls. To provide a consistent async workflow and rich task monitoring for users, we are introducing:
1. **Algorithm Engine Adapter Pattern**: `AlgorithmEngine` acts as a unified facade that natively handles `QgsProcessingAlgorithm` while using a `TaskAlgorithmAdapter` wrapper for existing/custom `QgsTask` routines without forcing a massive rewrite of existing specialized pipelines (classification, OBIA, SIFT).
2. **Central Task Center Dock (`TaskCenterDock`)**: A dedicated main window dock widget tracking task IDs, progress, parameters, real-time log buffers, and canvas auto-loading.
3. **Dual-Mode Dialog Execution**: Dialogs dispatch tasks directly to `TaskCenter` with an optional "Run in Background" feature so users can monitor progress in the dock while keeping their workspace interactive.
