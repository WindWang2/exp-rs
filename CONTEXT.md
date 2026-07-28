# SICNU GEO RS Domain Context

Core domain terminology and vocabulary for the SICNU GEO RS platform.

## Processing & Task Management

**Algorithm Engine**:
The unified registry, environment manager, and execution facade that manages provider loading (`GdalToolsProvider`, `OtbToolsProvider`, `QgisAlgorithmsProvider`, `GenericCliProvider`), tool path discovery, and algorithm adapters through a self-contained `AlgorithmEngine::initialize()` seam.
_Avoid_: Processing framework, Tool registry, Run manager

**Task Center**:
The central execution owner for Algorithm Tasks: it queues, tracks lifecycle (Pause/Resume/Cancel/Retry), exposes logs and parameters, applies priority scheduling and Task Pipeline gating, and makes results available for canvas loading. Workspace task views may present its tasks, but do not own their lifecycle. It delegates execution to internal worker adapters (including `JobEngine` and `QgsTask`).
_Avoid_: Job manager, Background runner, Process monitor

**Algorithm Task**:
A discrete, tracked unit of work managed by the Task Center, encapsulating an algorithm invocation, parameter map, real-time progress/log buffer, status lifecycle, and execution metrics.
_Avoid_: Async job, Processing item, Worker unit

**Task Pipeline**:
A directed acyclic graph (DAG) of dependent `AlgorithmTask` nodes where output dataset paths automatically flow as inputs into downstream algorithm nodes.
_Avoid_: Workflow graph, Execution chain, Process tree

**Resource Throttler**:
The scheduling subsystem responsible for managing task priority queues (`High`, `Normal`, `Low`), sorting queued task execution, and automatically capping max concurrent background threads to `std::thread::hardware_concurrency() - 1`.
_Avoid_: Core limiter, Thread pool filter

**Piecewise Linear Stretch**:
An image enhancement method that maps input pixel values to output display values via a set of user-defined interactive control points $(x_i, y_i)$ using linear interpolation across segments.
_Avoid_: Multi-segment curve, Custom LUT splitter

**Georeferencing Session**:
The user’s source and destination context for image registration: its Ground Control Points, fit and residual state, immutable warp snapshots, and persisted workspace state. Image-to-Image and Image-to-Map adapt the same Georeferencing Session to their different destination-pick interactions.
_Avoid_: Registration session, Georeferencer window state

**Real Data Range**:
The physical pixel value bounds ($\text{Min}$, $\text{Max}$) calculated directly from GDAL/QGIS band statistics (e.g. 16-bit DN values or Float reflectance) without clamping to 8-bit $[0, 255]$.
_Avoid_: Screen range, Display limits

## Data & Display

**Data Manager**:
The project-scoped authority for Data Asset identity, metadata, capabilities, revisions, dependencies, leases, and persistence policy. It does not own canvases, layer trees, renderers, widgets, or processing execution.
_Avoid_: Layer manager, Dataset singleton, Project layer store

**Data Asset**:
A project-addressable dataset with a stable Asset ID and revision, independent of whether or how it is displayed. Local rasters, vectors, remote maps, and virtual rasters are Data Asset kinds with different declared capabilities.
_Avoid_: Layer, File, Open dataset

**Data Collection**:
A non-renderable organizational dataset that groups related Data Assets and shared product metadata, such as a Sentinel SAFE product, Landsat scene, or HDF container.
_Avoid_: Folder, Multi-band layer, Container layer

**Band Reference**:
The address of one band inside a Raster Data Asset. A Band Reference shares its parent asset's lifecycle and becomes a Data Asset only when explicitly extracted or used to define a Virtual Raster Asset.
_Avoid_: Band layer, Band asset

**Virtual Raster Asset**:
A lazily evaluated Raster Data Asset defined by a reproducible recipe over one or more Band References or Raster Data Assets, including an explicit target grid and resampling policy when inputs differ.
_Avoid_: RGB layer, Temporary composite, Display stack

**Display View**:
A map viewport with its own coordinate reference system, extent, rotation, and temporal context. A Display View contains an ordered set of Display Layers.
_Avoid_: Data view, Canvas layer set

**Main Display View**:
The QGIS-interop Display View bound to the project’s primary `QgsMapCanvas` and `layerTreeRoot()`. It is non-removable through the secondary-view API. Secondary views are engine-only until shell chrome hosts them.
_Avoid_: The only canvas, Default layer list

**Active Display View**:
The Display View currently targeted by shell open/display actions. Until a view switcher exists, Active Display View equals the Main Display View.
_Avoid_: Selected layer, Focused canvas (without view identity)

**View layer tree**:
The shell UI projection of Display Layers in the Active Display View (dock title 视图图层). It is not the project data catalog.
_Avoid_: Layer manager tree, Project data list

**Display Layer**:
One independent presentation of a Data Asset inside exactly one Display View. Its QGIS adapter owns renderer, band composition, stretch, opacity, and other display state without changing the referenced Data Asset.
_Avoid_: Data Asset, Dataset, Shared layer

**Asset Lease**:
A claim held by a Display Layer, Algorithm Task, edit session, or dependent virtual asset that prevents its Data Asset from being unloaded while in use.
_Avoid_: Layer ownership, Open handle, Reference count

**Asset Revision**:
The version of a Data Asset's resolved data meaning. Explicit reload or authorized in-place modification advances the revision and invalidates revision-keyed derived state.
_Avoid_: File timestamp, Layer version

**Derivation Record**:
The structured provenance of an algorithm-produced Data Asset, including the algorithm version, parameter snapshot, and input Asset IDs and revisions.
_Avoid_: Log entry, Processing note, History string

## Python Plugin Infrastructure

**Plugin Host**:
The unified lifecycle and discovery manager (`PluginManager`) hosting both C++ (`QPluginLoader`) and Python (`classFactory(iface)`) plugins.
_Avoid_: Script runner, Plugin store

**Application Interface Facade (`iface`)**:
The application interface facade (`SicnuAppInterface` subclassing `QgisInterface`) passed into Python plugins, wrapping `QgisDesktopWindow`, `ActiveViewHost`, and `ProjectContext` while enforcing the Data/Display seam.
_Avoid_: Raw main window pointer, Global QgisApp instance

**Pipeline Node Canvas**:
The visual DAG editor canvas (`PipelineCanvasWidget` using Qt Graphics View) for constructing, editing, and monitoring processing task pipelines. Spatial metadata $(X,Y)$ is embedded in `WorkflowDefinition` JSON.
_Avoid_: Node graph window, Flow editor dialog

## Architectural Decision Records (ADRs)

### ADR 0011: Task Pipeline & Workflow Editor UI Architecture

- **Context**: Users need a visual DAG editor to construct, configure, execute, and monitor multi-step processing algorithm pipelines (e.g. Landsat pre-processing, spectral indices, OBIA classification).
- **Decision**:
  1. **Rendering Engine**: Use Qt's native Graphics View framework (`QGraphicsScene` / `QGraphicsView`) for zero extra 3rd-party dependencies and high-performance node graph rendering.
  2. **Data Model Sync**: Maintain two-way synchronization between canvas nodes/edges and `WorkflowDefinition` / `WorkflowSessionController`. Embed visual node coordinates $(X,Y)$ as spatial metadata inside `WorkflowDefinition` JSON (`"meta": {"ui": {"x": 120, "y": 250}}`).
  3. **Execution & Badging**: Support dual execution modes (Full DAG run / Step-by-step up to selected node) with real-time status color overlays (⚪ Idle, 🔵 Running, 🟢 Success, 🔴 Failure) and log stream inspection.
  4. **Data Seam Integration**: Intermediate step outputs are registered as `TaskTemporary` Data Assets in `DataManager` (ADR 0009/0010 compliant). Only output ports with the 👁️ *"Add to Map"* toggle enabled call `ActiveViewHost::openPath()` to add display layers to the canvas.
  5. **Shell Hosting**: Host the canvas in a dockable central panel (`PipelineEditorDock`) with dedicated ribbon actions under a **"Workflow / 流程"** tab and a drag-and-drop preset workflow catalog.

### ADR 0012: Atomic Algorithm Adapter & LLM Agent Tool Calling
- **Context**: Autonomous AI Agents (OpenAI Tool Calling / Qwen Function Call) and Task Pipeline editors require a strongly typed, platform-agnostic schema interface for executing algorithms without code modification.
- **Decision**:
  1. **Atomic Adapter Seam**: Define `AtomicAlgorithmAdapter` interface and `AlgorithmDescriptor` C++ struct with 12 `DataType` port type enums (`Raster`, `Vector`, `Table`, `Numeric`, `Integer`, `String`, `Boolean`, `Enum`, `BoundingBox`, `Crs`, `Json`, `Any`).
  2. **Heterogeneous Adapter Family**: Provide zero-modification reflection wrappers for `RsOperatorAdapter`, `QgsProcessingAdapter` (QGIS algorithms), and `PythonPluginAdapter` (Python plugins).
  3. **Central Registry Singleton**: Manage all algorithm adapters in a thread-safe `AtomicAlgorithmRegistry` singleton with auto-population on application startup.
### ADR 0013: AI Agent Copilot UI & Streaming LLM Client Architecture
- **Context**: Users need a natural language conversational interface (AI Copilot Panel) in the GIS shell to query workspace layers, auto-generate single/multi-step algorithm execution plans, and run tool calls directly on the system.
- **Decision**:
  1. **Shell UI Hosting**: Host the AI Agent in a right-dockable panel (`AgentCopilotDockWidget`) with a streaming message history view, reasoning chain cards (DeepSeek-R1 support), tool call progress widgets, and interactive plan approval cards.
  2. **Streaming LLM Client**: Build native C++ `LlmStreamingClient` over `QNetworkAccessManager` with SSE (Server-Sent Events) stream parsing, supporting OpenAI-compatible APIs (DeepSeek, Qwen, Ollama, vLLM, OpenAI). Auto-injects algorithm tool schemas from `AtomicAlgorithmRegistry`.
  3. **Workspace Context Resolver**: `AgentContextResolver` dynamically inspects `DataManager` (active Data Assets, asset paths, CRS, band references) and `ActiveViewHost` (selected layers, canvas extent) to inject structured context snapshots into LLM System Prompts.
  4. **Canvas Pipeline Synchronization**: Multi-step DAG plans from LLM responses automatically instantiate as visual `WorkflowDefinition` node graphs on `PipelineCanvasWidget` for interactive preview, parameter tuning, and execution via `AgentWorkflowExecutor::executeAgentPlan`.
  5. **Config & Profile Persistence**: `LlmConfigManager` manages API Key, Base URL, Model Name, and Temperature settings via `QSettings` with pre-configured provider templates (DeepSeek, Qwen, Ollama, OpenAI) and a ⚙️ **Model Settings** dialog.

### ADR 0014: Out-of-Process Python Plugin Host Architecture
- **Context**: Embedded CPython engine calls (`PyRun_SimpleString`) inside the main C++ application process introduce crash vulnerabilities: a Segfault in any third-party Python plugin crashes the entire C++ GIS application host.
- **Decision**:
  1. **Out-of-Process Isolation**: Completely cut over `PythonPluginAdapter` to delegate Python plugin discovery, loading, and execution to an out-of-process worker daemon via `PythonWorkerProcessPool`.
  2. **Dedicated Worker Allocation**: Assign a dedicated daemon process from `PythonWorkerProcessPool` to each loaded Python plugin to guarantee zero global Python state pollution and absolute crash boundary isolation.
  3. **IPC & Shared Memory**: Execute plugin commands over line-delimited JSON-RPC 2.0 Unix Domain Sockets (`PythonIpcServer`), transferring high-throughput raster matrices using zero-copy POSIX shared memory (`SharedMemorySegment` / `/dev/shm`).
  4. **Declarative UI Proxy**: Bind `PythonAppInterfaceProxy` inside `PythonPluginAdapter` during `initialize()` to translate `iface.addPluginToMenu()` IPC requests into C++ `QAction` menu items with automatic lifetime cleanup upon plugin `unload()`.
  5. **Sub-Second Auto-Healing**: If a Python plugin worker process segfaults or calls `os._exit()`, `PythonWorkerProcessPool` catches the process failure, auto-restarts a pre-warmed replacement worker in sub-second time, and emits a clean failure signal without crashing the `exp-rs` C++ main process.
  6. **API Compatibility Layer**: Expose `iface.activeLayer()`, `iface.mapCanvas().extent()`, `iface.messageBar().pushMessage()`, and `iface.addRasterLayer()` over JSON-RPC 2.0 (`catalog.get_active_layer`, `canvas.get_state`, `ui.push_message_bar`, `data.add_layer`), providing QGIS 3.x Python plugin source compatibility.
