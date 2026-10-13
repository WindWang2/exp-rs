# Goal & Work Packages — Direction D: Qt/QGIS Workbench, Interaction States, Teaching & Help

## Scope & Mandate

Direction D focuses on the existing Qt 6 / QGIS desktop workbench, layer & tool interactions, workflow UI, undergraduate teaching labs, teacher assessment console, Experiment Studio, and F1 context help systems.

No new GUI architectures or duplicate UI frameworks will be introduced. The goal is correctness, state honesty, data protection, virtualization, and stability across existing components.

## Work Packages (WP-D1 – WP-D12)

- **WP-D1: UI State Honesty & Action Availability Contracts**
  - Fix silent failures in clipboard and selection actions (`copyFeatures`, `cutFeatures`, `pasteFeatures`, `selectAll` in `QgisDesktopWindow`).
  - Provide immediate, explicit user feedback when actions cannot proceed (e.g. no active vector layer, empty selection, incompatible clipboard features).

- **WP-D2: Layer Tree & Map Interactions**
  - Verify layer selection, visibility toggling, deletion, and layer validity checks.
  - Guard against stale pointers on active layers and prevent map tool race conditions.

- **WP-D3: Vector Editing State Contracts & Ribbon Alignment**
  - Connect Ribbon Vector Editing tab tools to `CommandRegistry` or sync their enabled/disabled states with `QgisDesktopWindow::m_editingToolActions`.
  - Prevent digitizing/editing tools from operating when editing mode is disabled or on non-vector layers.
  - Ensure dirty edits trigger save confirmation dialogs.

- **WP-D4: Workflow UI & Pipeline Canvas State Integrity**
  - Prevent silent/truncated file writes on pipeline definitions (`writePipelineDefinitionFile` atomic file write via temporary file + rename).
  - Track dirty status across canvas modifications.

- **WP-D5: Large Dataset Table Virtualization**
  - Replace naive `QTableWidget` iteration in `ExperimentStudioDock` (`m_matrixTable`) with a virtual model (`QAbstractTableModel`) or optimized batch loading to handle 1,000–10,000 rows without allocating tens of thousands of individual `QTableWidgetItem` objects on the heap.

- **WP-D6: Teaching Lab Student Workflow Completeness**
  - Ensure accurate error feedback and state progression in the undergraduate lab cockpit.

- **WP-D7: Teaching Admin Batch Assessment Robustness**
  - Verify `TeachingAdminDock` cancel handling, progress reporting, and output publishing.

- **WP-D8: F1 Context Help & Terminology Provider Deduplication**
  - Fix Issue #1465: `terminology_provider.cpp` deduplication ID bug where suffixed IDs fed only the related lookup map while registration used the unsuffixed id.
  - Fail-closed error handling when resources fail to open.

- **WP-D9: Experiment Studio Truthfulness**
  - Maintain explicit empty states when no study is loaded.
  - Prevent synthetic fallback data from masking empty or failed studies.

- **WP-D10: Lifecycle & Teardown Safety**
  - Ensure safe disconnection of Qt signals and thread joins on dock / dialog destruction.

- **WP-D11: Visual Consistency & Design Tokens**
  - Consistent layout spacing, button sizing, disabled state hints, and accessible descriptions.

- **WP-D12: Documentation & Automated Test Coverage**
  - Comprehensive Catch2 regression tests for all fixed issues.
  - 100% green local build & CTest runs under `-j2`.
