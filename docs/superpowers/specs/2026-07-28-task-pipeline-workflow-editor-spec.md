# Task Pipeline & Workflow Visual Editor Specification

## Problem Statement

Remote sensing and GIS analysts in SICNU GEO RS frequently run complex, multi-step processing workflows (e.g. Landsat surface reflectance pre-processing, spectral indices calculation, image change detection, and OBIA segmentation/classification). Currently, users must manually configure and run algorithms one by one through individual algorithm dialogs, manually tracking intermediate dataset file paths and manually feeding outputs into subsequent tools. This manual process is time-consuming, prone to human error, and lacks visual progress monitoring or reproducible workflow sharing.

## Solution

A visual **Task Pipeline & Workflow Editor UI** built with Qt's native Graphics View framework (`QGraphicsScene` / `QGraphicsView`). Users can visually construct, edit, execute, and monitor processing DAG (Directed Acyclic Graph) node pipelines. Algorithms are represented as visual nodes with input/output ports connected by smooth bezier edges. Intermediate step datasets register automatically as `TaskTemporary` Data Assets in `DataManager` (ADR 0009/0010 compliant), while nodes with the 👁️ *"Add to Map"* toggle enabled automatically add `DisplayLayers` to the active map view via `ActiveViewHost`.

## User Stories

1. As a remote sensing analyst, I want to drag algorithm nodes from a tool catalog onto a visual canvas, so that I can graphically build multi-step processing pipelines.
2. As a GIS analyst, I want to connect output ports of upstream processing nodes to input ports of downstream nodes, so that dataset outputs automatically flow as inputs into subsequent processing steps.
3. As a user, I want the node editor to enforce port type compatibility (e.g. raster to raster input, vector to vector input), so that invalid connections are prevented during workflow design.
4. As a user, I want visual nodes to store spatial $(X,Y)$ position coordinates inside the workflow JSON definition, so that visual graph layouts are preserved across saves and loads.
5. As a remote sensing user, I want to execute the entire pipeline with a single "Run Pipeline" button, so that all dependent tasks run sequentially or in parallel based on DAG topological ordering.
6. As a analyst, I want to right-click any node and choose "Run Up to This Node", so that I can execute partial pipelines and inspect intermediate results.
7. As a user, I want visual nodes to show real-time execution status overlays (⚪ Idle, 🔵 Running, 🟢 Success, 🔴 Failure, ⏸️ Paused/Gate Waiting), so that I can visually track workflow progress at a glance.
8. As a user, I want running and failed nodes to display animated progress indicators or error tooltips, so that I can diagnose issues immediately.
9. As a user, I want to double-click any node or click its log icon to open its live log stream in `TaskCenter`, so that I can view detailed execution output.
10. As a GIS user, I want intermediate node dataset outputs to automatically register as `TaskTemporary` Data Assets in `DataManager`, so that catalog lineage and derivation records are tracked without polluting the file system permanently.
11. As a user, I want to toggle an 👁️ *"Add to Map"* switch on any node's output port, so that only selected intermediate or final results add display layers to the main map canvas (`ActiveViewHost`).
12. As a user, I want a dedicated **"Workflow / 流程"** ribbon tab in the main application window, so that I can quickly access pipeline actions (New, Open, Save, Run, Stop, Clear).
13. As a user, I want a dockable central panel (`PipelineEditorDock`) for the visual node editor, so that I can dock it next to map views or float it on multi-monitor setups.
14. As a user, I want a preset workflow catalog panel containing common remote sensing recipes (e.g. Landsat NDVI & Change Detection, Atmospheric Correction, OBIA Classification), so that I can load production-ready workflows with one click.
15. As a developer, I want all pipeline sessions to clean up unused temporary datasets via `DataManager::reapTaskTemporaries()`, so that session closes do not leak disk space.

## Implementation Decisions

### Architectural Seams & ADR 0011 Compliance
- **Rendering Engine**: Native Qt Graphics View (`QGraphicsScene` / `QGraphicsView` / custom `QGraphicsItem` subclasses for nodes, ports, and bezier connection edges). Zero external 3rd-party dependencies.
- **Model Sync**: Two-way `PipelineCanvasAdapter` synchronizing visual canvas items with `WorkflowDefinition` / `WorkflowSessionController`. Spatial coordinates $(X,Y)$ are embedded in `WorkflowDefinition` JSON metadata (`"meta": {"ui": {"x": 120, "y": 250}}`).
- **Data Seam Integration**: Intermediate outputs are registered as `TaskTemporary` Data Assets in `DataManager` carrying `DerivationRecord` provenance. Display layers are added to `ActiveViewHost` only when port 👁️ *"Add to Map"* is toggled on.
- **Shell UI**: Ribbon tab ("Workflow / 流程") + central dockable panel (`PipelineEditorDock`) + preset catalog panel.

### Core Modules
1. **`sicnu_workflow_gui` Module** (`src/app/workflow/`):
   - `PipelineCanvasWidget` & `PipelineCanvasScene`: The interactive Graphics View canvas and scene.
   - `PipelineNodeItem`: Custom `QGraphicsItem` representing an algorithm node with title bar, status badge, progress ring, parameter summary, and input/output ports.
   - `PipelinePortItem`: Input/output port anchor supporting drag-to-connect bezier lines.
   - `PipelineConnectionItem`: Bezier curve item representing a data dependency edge between two ports.
   - `PipelineEditorDock`: QDockWidget host for the node editor canvas and preset sidebar.
2. **`WorkflowSessionController` Enhancements** (`src/app/shell/workflow_session_controller.h`):
   - Bridges canvas actions to `TaskCenter` execution and status signals.
   - Handles spatial coordinate serialization to `WorkflowDefinition` JSON.

## Testing Decisions

- **Headless Model & Topology Tests**: Test DAG cycle detection, topological sorting, step execution transitions, and JSON serialization using Catch2 (`test_workflow_pipeline.cpp`).
- **Data Seam Integration Tests**: Verify intermediate dataset outputs register as `TaskTemporary` Data Assets in `DataManager` and verify `reapTaskTemporaries()` cleans idle temporary files without affecting persistent assets.
- **Prior Art**: `tests/test_active_view_host_data_context.cpp` and `tests/test_python_processing_provider.cpp`.

## Out of Scope

- Distributed multi-machine task execution (execution is scoped to local `ResourceThrottler` worker pool).
- Custom 3rd-party QML node styling widgets (strictly using native Qt Graphics View).

## Further Notes

- Full alignment with ADR 0009 (Data/Display Separation), ADR 0010 (Data Asset Catalog), and ADR 0011 (Task Pipeline & Workflow Editor UI Architecture).
