# SICNU GEO RS Domain Context

Core domain terminology and vocabulary for the SICNU GEO RS platform.

## Processing & Task Management

**Algorithm Engine**:
The unified registry, environment manager, and execution facade that manages provider loading (`GdalToolsProvider`, `OtbToolsProvider`, `QgisAlgorithmsProvider`, `GenericCliProvider`), tool path discovery, and algorithm adapters through a self-contained `AlgorithmEngine::initialize()` seam.
_Avoid_: Processing framework, Tool registry, Run manager

**Algorithm Provider Adapter**:
The uniform C++ interface seam (`AlgorithmProviderAdapter`) implemented by provider plugins (`GdalToolsProvider`, `OtbToolsProvider`, `QgisAlgorithmsProvider`, `GenericCliProvider`, `PythonProcessingProvider`) to declare provider resource profiles (`InProcessThread`, `PythonWorkerProcess`, `ExternalCliSubprocess`, `QgsTaskThread`), initialize execution backends, and populate `AtomicAlgorithmRegistry`.
_Avoid_: Provider manager, Plugin loader script, Tool finder

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

**Operator**:
A headless, self-describing algorithm unit with JSON parameter and result I/O, registered by name in the Operator Registry. Operators are one kind of Algorithm: the kind shared by the headless CLI, MCP tools, and GUI task helpers (`runOperatorTask`). Provider algorithms (QGIS/OTB/GDAL) reach the Agent surface through adapter wrappers instead of being Operators.
_Avoid_: RS algorithm, Kernel op, JSON task

**Operator Registry**:
The source-of-truth registry (`RSOperatorRegistry`) where Operators are registered by name and from which the headless CLI, MCP tools, and GUI task helpers execute them. The Agent surface does not consume it directly: each Operator is mirrored into the `AtomicAlgorithmRegistry` through a thin `RsOperatorAdapter` wrapper, so the Agent sees Operators and provider algorithms through one uniform adapter interface.
_Avoid_: Algorithm list, CLI registry

**Tool Call Dispatcher**:
The single deep seam between Agent-facing surfaces (Agent Copilot, MCP Server, Headless Runner) and the Task Center for LLM tool calls (`ToolCallDispatcher` in `src/processing/framework`). It encapsulates tool-call envelope parsing, algorithm ID normalization, required parameter validation, task submission, terminal status completion watching (`TaskCenter`), transactional output asset committing via `OutputCommitter`, and synchronous/asynchronous execution (`dispatchAndAwait`). Callers interact solely through its interface without writing custom polling loops or error/output payload formatting code.
_Avoid_: Tool-call handler, Function-call runner, Agent executor

**MCP Server**:
The stateless external-agent JSON-RPC protocol adapter at the Task Center seam: it owns stdio framing, the tool allow-list / workspace-path security policy, the meta-tool catalog, and the `mcpStatusForTask` status mapping — but no execution machinery of its own. Single calls become Task Center tasks (`execution_id` = `"task-<taskId>"`); `rs:` operators route through the Tool Call Dispatcher, provider algorithms through `TaskCenter::enqueueTask`, and read-only `spatial:` tools execute inline (ADR 0122). `tools/list` enumerates the meta tools plus the unified Agent Tool Catalog (algorithms, interaction, data, spatial) with full JSON Schemas; `run_workflow` / `get_workflow_status` submit and aggregate agent-generated pipeline DAGs.
_Avoid_: MCP worker, Agent runner, Tool executor

**Spatial Tool**:
An executable agent-facing spatial capability behind one synchronous contract (ADR 0122): `name` / `description` / `inputSchema` / `outputSchema` / `execute(input)`. Spatial Tools are fast and read-only (inspection, catalogs, queries) — unlike algorithms, they never enter the Task Center; long-running work stays an algorithm call.
_Avoid_: Agent plugin, MCP function, Operator (an Operator is a Task Center algorithm; a Spatial Tool is an inline query)

**Spatial Tool Registry**:
The process-wide singleton (`SpatialToolRegistry`, `src/agent/spatial_tools/`) owning Spatial Tool instances. The `SpatialToolProvider` mirrors the registry into the Agent Tool Catalog as descriptors, so the copilot, CLI, and MCP `tools/list` see one catalog while execution stays owned by the registry.
_Avoid_: Tool catalog (that is the unified AgentToolCatalog), Interaction Tool Registry

**Model Catalog**:
The model-runtime manifest registry (`ModelCatalog`, `src/operators/framework/`) scanning `models/*/model.json` (name, task, input/output contract, framework, GPU, accuracy, weight path). `rs:infer` resolves a catalog name to its weight path; `spatial:list_models` exposes it to agents. Weights are never committed — manifests document pluggable runtimes.
_Avoid_: Model zoo, Weight manager, Inference backend

**Algorithm Capability Sidecar**:
An optional per-algorithm JSON manifest under `data/processing/algorithm_meta/` (task, input, output, gpu, accuracy, notes, tags) loaded by the `AlgorithmMetaStore` overlay and attached to MCP discovery responses as a `catalog` object. Descriptors stay untouched — the sidecar is pure capability documentation for agent task→algorithm matching.
_Avoid_: Algorithm descriptor (that is the registry's schema object), toolbox manifest (that gates CI coverage)

**Pi Bridge**:
The external agent-runtime adapter (`pi/exp-rs-spatial.ts`, ADR 0122): a dependency-free Pi extension that spawns the desktop binary with `--mcp`, performs the JSON-RPC handshake, and registers every server tool as a Pi tool. Pi owns the agent loop / planning / memory / reasoning; exp-rs owns spatial understanding, algorithms, workflow execution, and models. Pi ships no MCP client by design, so the bridge owns the transport.
_Avoid_: Pi plugin, MCP gateway, Agent harness (the harness is Pi itself)

**Classification Pipeline**:
The deep, GUI-free module in `src/analysis/classification` (`RsClassificationPipeline`) that owns the full pixel-classification flow: vector sample extraction from training polygons, feature scaling (`RsFeatureScaler`), stratified holdout split (`RsClassificationSplit`), classifier training & OpenCV backend creation, model persistence & superset sidecar parsing (`loadModelSidecar`), predict-only mode (`modelLoadPath`), tiled prediction with dtype escalation, class-map writing, and accuracy assessment (`RsAccuracyAssessment`). The GUI classification task and `RsSupervisedClassificationOperator` are thin adapters at its seam. Post-processing (sieve/majority/clump/recode) is a separate stage outside the pipeline.
_Avoid_: Classify task, Training helper, ML pipeline

**Piecewise Linear Stretch**:
An image enhancement method that maps input pixel values to output display values via a set of user-defined interactive control points $(x_i, y_i)$ using linear interpolation across segments.
_Avoid_: Multi-segment curve, Custom LUT splitter

**Georeferencing Session**:
The sole owner of the user's source and destination context for image registration: the Ground Control Point list, the transform fit (including RPC refinement) and per-point residuals, warp-readiness validation, and immutable warp snapshots. Residuals and RMS are reported in source-image pixels. Shell windows and the GCP table observe the session (`fitChanged`) and render its state read-only; Image-to-Image and Image-to-Map adapt the same session to their different destination-pick interactions. Warp execution reaches Task Center through an injected executor seam, never a hardwired singleton.
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

**Georeferencing Session (`RsGeoreferencingSession`)**:
The single deep module owning GCP pairing, transform fitting (`refit()`), residual calculations, dirty state tracking (`isDirty()`), `QSettings` workflow snapshot persistence (`saveWorkflow`/`restoreWorkflow`), `WorkflowRuntime` session mirror (`enableWorkflowMirror`), and Task Center warp task dispatch.
_Avoid_: Session state sidecar, Workflow bridge sidecar, Split GCP manager, Dialog dirty flag

## Python Plugin Infrastructure

**Plugin Host**:
The unified lifecycle and discovery manager (`PluginHost`) hosting both C++ (`QPluginLoader`) and Python (`classFactory(iface)`) plugins. It owns no Python hosting machinery of its own: it composes the Python Plugin Host and adds menu injection and window wiring.
_Avoid_: Plugin manager, Script runner, Plugin store

**Python Plugin Host**:
The GUI-free lifecycle owner for Python plugins (`classFactory(iface)`): it owns the `PythonWorkerProcessPool`, the `PythonAppInterfaceProxy` / Headless Asset Seam wiring, and the registration of plugin-provided `py:` processing algorithms. It requires a `DataManager` asset authority but no QWidget — UI-dependent plugin calls degrade through the Headless Asset Seam (`ui_unavailable`, `no_canvas`, `no_active_layer`). The desktop Plugin Host composes it and adds menu/window wiring; headless surfaces (the CLI pipeline runner, later the MCP Server) consume it directly, declaring which plugins to load explicitly.
_Avoid_: Headless plugin manager, Script host, CLI plugin loader

**Application Interface Facade (`iface`)**:
The application interface facade (`SicnuAppInterface` subclassing `QgisInterface`) passed into Python plugins, wrapping `QgisDesktopWindow`, `ActiveViewHost`, and `ProjectContext` while enforcing the Data/Display seam.
_Avoid_: Raw main window pointer, Global QgisApp instance

**Headless Asset Seam (`AppInterfaceBridge`)**:
The deep JSON-RPC serialization and IPC method dispatch module (`AppInterfaceBridge` in `src/python/isolated`) consumed by `PythonAppInterfaceProxy` for out-of-process Python plugin workers. It owns JSON-RPC request decoding and response payload generation (`catalog.*`, `data.*`, `canvas.*`, `ui.push_message_bar`, `processing.register_algorithm`). `DataManager` is its required asset authority — catalog queries, source registration (`openPath`), and the explicit plugin-driven active asset (`setActiveAsset`/`activeAssetId`, replacing the canvas current layer) — while `ActiveViewHost` is an optional enhancement bound only in GUI mode for display, canvas state, and message bar. IPC methods degrade gracefully without a view host (`no_canvas`, `no_active_layer`, `ui_unavailable`), so the seam works headlessly without any QWidget.
_Avoid_: GUI proxy, QgisInterface IPC shim

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
**Workspace Snapshot**:
An immutable, serializable C++ value object (`WorkspaceSnapshot`) captured atomically from `DataManager` (Data Asset metadata, paths, band counts, CRS) and `ActiveViewHost` (map canvas extent, scale, active layer). Serves as the single seam for `AgentContextResolver` to generate LLM system prompts without touching live Qt/QGIS GUI widgets.
_Avoid_: Workspace state map, Context dict, UI state dump

### ADR 0013: AI Agent Copilot UI & Streaming LLM Client Architecture
- **Context**: Users need a natural language conversational interface (AI Copilot Panel) in the GIS shell to query workspace layers, auto-generate single/multi-step algorithm execution plans, and run tool calls directly on the system.
- **Decision**:
  1. **Shell UI Hosting**: Host the AI Agent in a right-dockable panel (`AgentCopilotDockWidget`) with a streaming message history view, reasoning chain cards (DeepSeek-R1 support), tool call progress widgets, and interactive plan approval cards.
  2. **Streaming LLM Client**: Build native C++ `LlmStreamingClient` over `QNetworkAccessManager` with SSE (Server-Sent Events) stream parsing, supporting OpenAI-compatible APIs (DeepSeek, Qwen, Ollama, vLLM, OpenAI). Auto-injects algorithm tool schemas from `AtomicAlgorithmRegistry`.
  3. **Workspace Context Resolver**: `AgentContextResolver` dynamically captures an immutable `WorkspaceSnapshot` from `DataManager` and `ActiveViewHost` facades to inject structured context snapshots into LLM System Prompts without GUI widget coupling.
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

### ADR 0015: ActiveViewHost Deepening & GIS Shell Facade Architecture
- **Context**: Passing multiple raw UI pointers (`QgsMapCanvas*`, `QgsMessageBar*`, `ActiveViewHost*`) into IPC proxies (`PythonAppInterfaceProxy`) and plugin facades (`SicnuAppInterface`) creates shallowness and coupling friction.
- **Decision**:
  1. **Facade Encapsulation**: Deepen `ActiveViewHost` to absorb canvas extent/scale queries (`mapCanvasExtent()`, `mapCanvasScale()`), user message bar alerts (`pushMessageBarAlert()`), active layer state (`activeLayer()`), and dataset opening (`openPath()`).
  2. **Single Seam Coupling**: `PythonAppInterfaceProxy`, `SicnuAppInterface`, and `LayerTreeMenuProvider` hold ONLY a single `ActiveViewHost*` pointer, completely isolating IPC handlers and layer-tree context menus from raw C++ GUI widget trees.
  3. **Dual-Layer QGIS Bridge**: `SicnuAppInterface` delegates `QgisInterface` C++ API overrides directly to `ActiveViewHost`, preserving full QGIS C++ plugin source compatibility.

### ADR 0016: TaskCenter Deepening & Native DAG Task Pipeline Engine Architecture
- **Context**: Executing visual DAG task pipelines previously required `WorkflowSessionController` to maintain manual step-index iteration loops (`m_executionQueue`, `m_currentQueueIndex`) in Qt UI slots, creating shallowness and preventing headless execution across Agent and CLI surfaces.
- **Decision**:
  1. **Native Pipeline Seam**: Deepen `TaskCenter` to natively accept DAG pipelines via `TaskCenter::submitPipeline(WorkflowDefinition)` and `submitPipelineJson(std::string)`.
  2. **Automated Upstream Parameter Resolution**: `TaskCenter` resolves `$stepId.output` placeholders in downstream task parameters automatically upon upstream task completion before launching dependent tasks.
  3. **Reactive UI & Headless Seam**: `WorkflowSessionController`, `AgentWorkflowExecutor`, and headless CLI execute pipelines through `TaskCenter`'s single interface seam, observing reactive `taskUpdated` signals for node status badging without maintaining UI execution loops.

### ADR 0018: CollectionImportService Deepening & One-Step Dataset Import Architecture
- **Context**: Importing Remote Sensing product scenes (Sentinel-2 SAFE, Landsat scenes, HDF5 containers) previously forced callers across GUI, Python API, and LLM Agent tools to manually orchestrate two-step probe and commit loops to select and register child band assets.
- **Decision**:
  1. **One-Step Import Seam**: Deepen `CollectionImportService` with `importCollection(sourcePath, persistence, autoLoad, pathOpener)` to automatically probe, select all discovered children, and register the `DataCollection` and child `DataAsset` items in `DataManager` in a single atomic transaction. The service stays free of GUI types so it can run headlessly (processing layer).
  2. **Opt-In Canvas Auto-Loading via Display Seam**: When `autoLoad = true` and a `pathOpener` callback is provided, after a successful commit the service invokes the opener with the **primary committed child** path (preferring the registered asset's `canonicalSource`). GUI / Agent hosts bind `pathOpener` to `ActiveViewHost::openPath` (or equivalent) so map loading preserves the Data/Display seam (ADR 0010/0015). Headless callers omit the opener.
  3. **Headless Testability**: `importCollection()` is tested headlessly using stub discoverers in `test_collection_import_service.cpp`, including autoLoad opener capture and failure rollback.

### ADR 0019: Classification Pipeline Deepening & Single Train/Predict Seam Architecture
- **Context**: Pixel classification was implemented three times with proven divergence — the GUI task (`RsClassificationTask`), the supervised Operator, and the OBIA Operator each carried their own training, extraction, and prediction logic (SVM hyperparameters had already drifted, and defect fixes made in the GUI path never reached the Operator path). Classification was the last subsystem violating the "teaching UI consumes the operator kernel" norm.
- **Decision**:
  1. **Single Deep Module**: Introduce the **Classification Pipeline** module in `src/analysis/classification` owning sample extraction, feature scaling, stratified split, training, model persistence, tiled prediction, class-map writing, and accuracy assessment. `RsClassificationTask` and the classification Operators (`rs:supervised_classification`, `rs:kmeans_classification`, `rs:obia_classify`) become thin async/JSON adapters at its seam. Post-processing (sieve/majority/clump/recode) remains a separate stage outside the pipeline.
  2. **Synchronous Core, Own Progress Sink**: The pipeline runs synchronously on the caller's thread behind a minimal progress/cancel sink it owns; the GUI task bridges to `QgsFeedback`, Operators bridge to `RSOperatorContext`. No `QgsTask` or operator-kernel types cross into the module.
  3. **One Model Format**: The module owns a single superset sidecar (method + scaler + class metadata + format version); the legacy `.scale.json` sidecar is dropped with no legacy read path.
  4. **Parity Through the Schema**: Stratified split, scaling, and accuracy assessment become optional Operator schema params, with OA/Kappa/confusion matrix returned in results on request — GUI checkboxes and Agent/CLI params map to the same pipeline options.
  5. **Interface as Test Surface**: The primary test suite crosses the pipeline's seam with `QCoreApplication` only; task/Operator tests shrink to thin-adapter smokes.

### ADR 0020: Georeferencing Session Deepening & Sole GCP/Fit Ownership Architecture
- **Context**: The Georeferencing Session existed but was a write-through copy: the shell window owned the real GCP list (`QgsGCPList`) and re-implemented the fit, so the two diverged (deleted GCPs never left the session copy) and the two fit implementations disagreed on RMS semantics (source pixels vs destination units). `fitChanged` had zero subscribers; GCP state was mirrored across nine stores including the table model's cached transform pointer and GDAL geotransform readers.
- **Decision**:
  1. **Sole Ownership**: The session owns the GCP list, the transform fit (RPC/DEM refinement ported from the shell's `recomputeFit`), per-point residuals, and warp-readiness validation. The GCP table model and canvas markers render session state read-only; `applyAcceptedMatches`, the shell's `mTransform`/`mLastRms`, and the hand dual-writes are deleted. Pairing interaction and `.points`/QSettings persistence remain shell concerns.
  2. **Source-Pixel RMS**: Residuals and RMS are reported in source-image pixels (QGIS georeferencer convention), computed by back-transforming destination coordinates to source. The destination-unit variant in the old session `refit` is dropped.
  3. **Injected Executor Seam**: Warp submission reaches Task Center through a minimal executor interface accepted by the session's constructor — a TaskCenter adapter in production, a fake in tests. The session never touches the Task Center singleton directly.
  4. **Interface as Test Surface**: A headless Catch2 suite crosses the session seam (`QCoreApplication` + fake executor) covering add/remove/enable → refit, RMS semantics, min-GCP validation, the RPC/DEM branch, snapshot immutability, and warp submit/cancel; window tests shrink to UI wiring.

### ADR 0021: Tool Call Dispatcher Deepening & Single Tool-Call Seam Architecture
- **Context**: LLM tool calls were parsed three times (`TaskCenter::enqueueToolCall`, `AtomicAlgorithmRegistry::executeToolCall`, `AgentWorkflowExecutor::executeToolCall`) with divergent envelope coverage, and executed through two paths: the registry ran calls synchronously off-record, while `LlmStreamingClient` and the Copilot dock each submitted the same call — up to three executions per logical call, twice blocking the GUI thread for 10–30 minutes; the client's own result signal had no consumers. Multi-step plan orchestration polling and MCP tool execution remain separate follow-ups.
- **Decision**:
  1. **Single Deep Module**: Introduce the **Tool Call Dispatcher** in `src/processing/framework` owning envelope parsing (all historical shapes), algorithm-id normalization, and classification into single Tool Call vs Plan Request. It stays GUI-free so it can run and be tested headlessly.
  2. **Typed TaskCenter Handoff**: Single calls enter the Task Center through a typed submission (normalized algorithm id + parameter map); the string-JSON `enqueueToolCall`, the registry's synchronous `executeToolCall`, and the `submitToolCall` pass-through are deleted.
  3. **Callback-Primary Results**: Callers submit with a completion callback and never block the GUI thread; a blocking variant with a default 30-minute, per-call-overridable timeout exists only for tests and headless callers.
  4. **Client as Pure Transport**: `LlmStreamingClient` emits each parsed tool call exactly once (streamed envelope shape) and never executes; the Copilot dock is the sole submitter — single calls to the dispatcher, Plan Requests to the existing plan-approval flow.
  5. **Interface as Test Surface**: The three legacy parsers' test cases merge into the dispatcher's headless suite; the registry and executor tool-call entry points and their tests are removed with them.

### ADR 0022: MCP Server Deepening & Stateless Task Center Adapter Architecture
- **Context**: `McpServer` ran a parallel execution path invisible to TaskCenter: its own worker threads (`AlgorithmWorker`, `OperatorWorker`), an in-process execution registry (`mExecutions`), and cancel flags (`mOperatorCancelFlags`) — so MCP executions had no progress/cancel/result visibility in the Task Center and bypassed its scheduling, throttling, and lifecycle rules.
- **Decision**:
  1. **Stateless Protocol Adapter**: `McpServer` becomes a stateless JSON-RPC protocol adapter at the Task Center seam. The execution registry, worker threads, cancel flags, and execution counter are deleted; `execution_id` is the TaskCenter task id (`"task-<taskId>"`). The stdio framing, the 10 meta-tool handlers, and the allow-list / workspace-path security policy (`isAlgorithmIdAllowed`, `isOperatorIdAllowed`, `validateWorkspacePaths`) are kept.
  2. **Split Routing**: `handleExecuteOperator` (`rs:` ids) submits through the **Tool Call Dispatcher** (gaining required-parameter validation and underscore→colon id normalization); `handleExecuteAlgorithm` (`gdal:`/`otb:`/`qgis:` ids) submits directly to `TaskCenter::enqueueTask`. Both use `autoLoad=false` (MCP has no canvas; results travel via `resultPayload`) and report `status: "running"` with the new `execution_id`.
  3. **Status Mapping & Truthful Cancel**: `mcpStatusForTask(AlgorithmTaskInfo)` maps Queued/Running/Paused → `"running"`, Completed → `"completed"` + `result`, Failed → `"failed"` + `errorMessage`, Canceled → `"canceled"`, with `progress` and last-log-line `progressText`. `cancel_execution` reports `"canceled"` only when `TaskCenter::cancelTask` accepted the cancel; for already-terminal tasks it reports the task's actual status — never a blanket `"canceled"`.
  4. **Catalog as Protocol Contract**: The ~160-line `tools/list` construction is compressed into a static meta-tool table; names, descriptions, and input schemas stay byte-equivalent — the catalog is the client protocol contract.
  5. **Shared Json↔Variant Converters**: MCP's private `jsonValueToVariant` / `variantToJsonValue` / `jsonObjectToVariantMap` move into the shared converter header (`src/processing/framework/json_params_converter.h`) next to `jsonParamsToVariantMap`; the duplicated `envFlagEnabled` moves to a shared `env_flag.h` used by both MCP and the STAC client.
  6. **Interface as Test Surface**: Headless tests fabricate `AlgorithmTaskInfo` in all six states, drive a registered no-op operator through execute → status → cancel via the real TaskCenter, and assert cancel truthfulness for terminal tasks; the pre-existing six MCP sections pass unchanged.

### ADR 0029: Collapse WorkflowRunner Pass-Through into WorkflowRuntime
- **Context**: `WorkflowRunner` was a shallow static pass-through class (`workflow_runner.h` / `workflow_runner.cpp`) containing a single static method `run()`. Its sole caller in the entire codebase was `WorkflowRuntime::runStep`.
- **Decision**:
  1. **Delete `WorkflowRunner`**: Delete `workflow_runner.h` and `workflow_runner.cpp` completely and remove from `CMakeLists.txt`.
  2. **Deepen `WorkflowRuntime`**: Inline operator creation, `RSOperatorContext` stack setup, and `RSOperatorError` $\rightarrow$ `std::runtime_error` exception translation directly inside `WorkflowRuntime::runStep`.

### ADR 0030: Absorb WorkflowRegistry into WorkflowRuntime
- **Context**: `WorkflowRegistry` was a shallow in-memory container wrapper. Callers (`RsGeoreferencingSession`, `WorkflowSessionController`, `RsClassifyWorkflowBridge`, unit tests) were forced to manage dual-object boilerplate (`WorkflowRegistry` + `WorkflowRuntime`).
- **Decision**:
  1. **Delete `WorkflowRegistry`**: Delete `workflow_registry.h` and `workflow_registry.cpp` completely and remove from `CMakeLists.txt`.
  2. **Deepen `WorkflowRuntime`**: Add `registerDefinition`, `findDefinition`, `hasDefinition`, and `registeredDefinitionIds` directly to `WorkflowRuntime`. Auto-register built-in workflows on initialization.
  3. **Caller Simplification**: All caller subsystems and test suites instantiate `WorkflowRuntime` directly as a single deep module.

### ADR 0031: Integrate PlaceholderGrammar into WorkflowSession
- **Context**: `PlaceholderGrammar` existed as a pure parsing helper module. Parameter values recorded in `WorkflowSession` often contained upstream artifact references such as `$step1.output` or `${step1.portName}`, forcing external callers to manually invoke `substitutePlaceholders`.
- **Decision**:
  1. **Add `resolveParams(stepId)`**: `paramsFor(stepId)` returns raw recorded parameter JSON (for UI form editing), while `resolveParams(stepId)` returns parameter JSON with all placeholders dynamically substituted.
  2. **Artifact Lookup Locality**: Resolves placeholders against `m_artifacts` (via `artifactOnSuccess`, `stepId.portName`, or `portName`).
  3. **`WorkflowRuntime` Deepening**: `WorkflowRuntime::runStep` calls `s->resolveParams(stepId)` to pass fully resolved parameter maps directly to operators.

### ADR 0032: Stateful LlmConfigManager Facade Architecture
- **Context**: `LlmConfigManager` was a shallow static `QSettings` utility. UI widgets and clients had to re-query `QSettings` or manually synchronize when active LLM provider profiles changed.
- **Decision**:
  1. **Stateful `QObject` Singleton**: Converted `LlmConfigManager` to a `QObject` singleton (`LlmConfigManager::instance()`) with in-memory profile caching.
  2. **Reactive Signals**: Added `activeProfileChanged` and `profilesChanged` signals. `AgentCopilotDockWidget` connects to them to auto-update model selection UI reactively.
  3. **Zero-Breakage Backwards Compatibility**: Retained static methods (`activeProfile()`, `setActiveProfile()`, etc.) as forwarding wrappers delegating to `LlmConfigManager::instance()`.

### ADR 0033: Deepen RsClassifyWorkflowBridge Signal Synchronization
- **Context**: `RsClassifyWorkflowBridge` was a thin helper requiring `QgsClassificationMainWindow` to manually invoke step transitions and completion sync methods across UI slots.
- **Decision**:
  1. **`QObject` Bridge Deepening**: `RsClassifyWorkflowBridge` inherits from `QObject` and provides `bindController(RsClassifyWorkflowController *controller)`.
  2. **Automated Signal Binding**: Opens the `"lab.classify.supervised"` workflow session automatically and connects to `currentStepChanged` and `completionChanged` controller signals.
  3. **Caller Simplification**: `QgsClassificationMainWindow` initializes the bridge via `bindController`, eliminating manual step-sync boilerplate.

### ADR 0034: Consolidate Georeferencing Session Warp Execution
- **Context**: `RsGeoreferencingSession` used an abstract interface `RsGeorefWarpExecutor` with `RsGeorefTaskCenterExecutor` being its only production implementation.
- **Decision**:
  1. **Direct TaskCenter Integration**: Warp tasks submit directly to `TaskCenter::instance()` by default.
  2. **Deleted Shallow Seam**: Deleted `rs_georef_task_center_executor.h`/`.cpp` and removed `RsGeorefWarpExecutor`.
  3. **Test Injection**: Added `CustomWarpExecutor` struct (`submit`, `cancel`) for headless Catch2 mock injection.

### ADR 0035: Collapse PythonAppInterfaceProxy into AppInterfaceBridge
- **Context**: `PythonAppInterfaceProxy` was a shallow middleman class wrapping `AppInterfaceBridge` to handle `ui.add_plugin_menu` IPC action creation.
- **Decision**:
  1. **Absorb Proxy Seam**: Deepened `AppInterfaceBridge` to accept an optional `QMenu *parentMenu`, handle `ui.add_plugin_menu`, and bind `PythonIpcServer`.
  2. **Direct Ownership**: `PythonPluginAdapter` owns `AppInterfaceBridge` directly (`m_bridge`).
  3. **Deleted Shallow Class**: Deleted `python_app_interface_proxy.h`/`.cpp` and removed them from build manifests.

### ADR 0036: Deepen ActiveViewHost Canvas Viewport Seams
- **Context**: `ActiveViewHost` exposed read-only viewport queries (`mapCanvasExtent()`, `mapCanvasScale()`), but callers were forced to dereference `activeViewHost->mapCanvas()` directly for viewport mutations.
- **Decision**:
  1. **Encapsulate Viewport Seam**: Deepened `ActiveViewHost` with `setExtent`, `setCenter`, `zoomToFullExtent`, and `refreshCanvas` methods.
  2. **Headless Degradation**: Ensured all viewport mutation methods degrade gracefully as no-ops when running headlessly without a map canvas.

### ADR 0037: Consolidate WorkflowSessionController Execution & Task Center Signals
- **Context**: `WorkflowSessionController` managed asynchronous single-operator and pipeline execution state with internal fields (`m_pendingTaskId`, `m_activePipelineId`, `m_runInFlight`) unexposed to shell and lab window callers.
- **Decision**:
  1. **Encapsulate Execution Queries**: Exposed `isRunInFlight()`, `activeSessionId()`, `activeStepId()`, `activePipelineId()`, and `pendingTaskId()` getters.
  2. **Unified Cancellation**: Added `cancelActiveRun()`, consolidating single-task and pipeline cancellation in one primary method and aliasing `stopWorkflow()` to it.

### ADR 0038: Consolidate RsClassifyWorkflowBridge Artifact Sync
- **Context**: `RsClassifyWorkflowBridge` required UI callers to manually invoke artifact setters whenever raster paths changed on `RsClassifyWorkflowController`.
- **Decision**:
  1. **Automated Completion Sync**: `syncCompletionsFromController` automatically syncs step completion state across `RsClassifyStep::Count`.
  2. **Backward-Compatible Setters**: Retained `setSourceRasterArtifact` and `setClassifiedOutputArtifact` for explicit manual overrides.

### ADR 0039: Deepen RsObiaMainWindow Task Execution & Cancellation Seams
- **Context**: `RsObiaMainWindow` managed OBIA background tasks via private `m_pendingTaskId` and `PendingOp` fields without exposing execution state queries or task cancellation to window callers or test suites.
- **Decision**:
  1. **Public Execution State**: Made `enum class PendingOp` public and exposed `pendingOp()` getter along with public `isBusy()`.
  2. **Unified Cancellation Seam**: Implemented `cancelActiveTask()` to cancel background operations via `TaskCenter::instance().cancelTask()` and reset pending UI state cleanly.

### ADR 0040: Deepen QgisDisplayManager Layer Tree & Visibility Seams
- **Context**: `QgisDisplayManager` managed presentation instances and Data Asset leases, but callers adjusting layer visibility or tree Z-ordering had to access QGIS `QgsLayerTree` nodes directly outside manager seams.
- **Decision**:
  1. **Visibility Seam**: Exposed `setLayerVisible(DisplayLayerId, bool)` and `isLayerVisible(DisplayLayerId) const` on `QgisDisplayManager`.
  2. **Tree Ordering Seams**: Exposed `moveLayerTop(DisplayLayerId)` and `moveLayerBottom(DisplayLayerId)` to manage layer node positions in the associated `QgsLayerTree`.

### ADR 0041: Encapsulate Raster Stretch Resolution in DisplayStretchPipeline
- **Context**: Display stretch resolution and application logic was exposed via free functions (`validate`, `resolve`, `apply`) in `display_stretch.h`.
- **Decision**:
  1. **Pipeline Class**: Encapsulated stretch validation, resolution, and target application into a `DisplayStretchPipeline` deep module class.
  2. **Backward-Compatible Free Functions**: Maintained inline free function wrappers forwarding to `DisplayStretchPipeline` for existing callers.

### ADR 0042: Deepen AuthResolver Credential Cache Seams
- **Context**: `AuthResolver` was a single-method interface (`applyAuthConfig`). Callers needing to check config existence or enumerate available configs had to reach into `QgsAuthManager` directly, bypassing the injectable seam.
- **Decision**:
  1. **Query Seam**: Added `hasAuthConfig(authConfigId)` pure-virtual to `AuthResolver` for config existence checks.
  2. **Enumeration Seam**: Added `knownAuthConfigIds()` pure-virtual returning opaque ids (never credential material).
  3. **Credential Discipline Preserved**: Neither new method exposes passwords or secrets.

### ADR 0043: Route SicnuPythonApi Through ActiveViewHost
- **Context**: `SicnuPythonApi` had inconsistent seam discipline — 7 methods reached directly into `QgsMapCanvas*` and `QgsProject::instance()` while `addRasterLayer`/`addVectorLayer` correctly used `ActiveViewHost`.
- **Decision**:
  1. **Removed `m_canvas` and `initialize(QgsMapCanvas*)`**: All canvas access now routes through `m_activeViewHost`.
  2. **Added `ActiveViewHost::setScale(double)`**: Complements existing viewport methods (ADR 0036).

### ADR 0044: Consolidate Plugin Load Context
- **Context**: `PluginHost::loadPythonPlugin` manually unpacked 3 raw pointers from `SicnuAppInterface` and threaded them through `PythonPluginHost::loadPlugin` and `PythonPluginAdapter`.
- **Decision**:
  1. **`PluginLoadContext` struct**: Consolidated `DataManager*`, `QMenu*`, `ActiveViewHost*` into a single context object.
  2. **Backward-Compatible Overloads**: Inline delegating constructors/methods accept the old 3-pointer signature.

### ADR 0045: Add PythonWorkerProcessPool Health Snapshot
- **Context**: `PythonWorkerProcessPool` exposed `activeWorkerCount()` and `availableWorkerCount()` as separate queries with no crash history visibility.
- **Decision**:
  1. **`PoolHealthSnapshot` struct**: Composite query with `total`, `active`, `available`, `totalRestarts`, and `isHealthy()` predicate.
  2. **`poolHealth() const`**: Single deep-module query traversing the node list once.

### ADR 0046: Delete AgentContextResolver Pass-Through Shim
- **Context**: `AgentContextResolver` had collapsed into a pass-through — `buildContextSnapshot` forwarded one line to `WorkspaceSnapshot::capture().toJson()` while the JSON overload of `formatSystemContextPrompt` hand-duplicated `toSystemPromptHeader()`; the sole production caller used the JSON round-trip branch.
- **Decision**:
  1. **Delete `AgentContextResolver`**: removed the class, its unit test, and its CMake registrations.
  2. **Call the seam directly**: `AgentCopilotDockWidget::sendPrompt` now invokes `WorkspaceSnapshot::capture( m_dataManager, m_viewHost ).toSystemPromptHeader()`.
  3. **Delete `WorkspaceSnapshot::toJson()`**: no consumers remain; prompt-content coverage moved to `toSystemPromptHeader()` tests.

### ADR 0047: Add Asynchronous Plan Execution to AgentWorkflowExecutor
- **Context**: `AgentWorkflowExecutor::executeAgentPlan` was blocking-only (polling `waitForPipeline` up to 60 min), so `AgentCopilotDockWidget` spawned a detached `std::thread` reading UI-owned members — a lifetime/thread-safety hazard owned by a UI class.
- **Decision**:
  1. **Make `AgentWorkflowExecutor` a `QObject`**: the `TaskCenter::taskUpdated` watcher connection auto-disconnects on destruction.
  2. **Add `executeAgentPlanAsync(planJson, callback, context)`**: returns the pipelineId immediately; invokes the callback exactly once at pipeline terminal state, marshaled onto `context`'s thread via `QMetaObject::invokeMethod`.
  3. **Extract shared parse/normalize and planResult assembly helpers**: one owner of the result shape for both paths; sync output stays byte-identical.
  4. **Remove the dock's detached thread and dead signals**: plan execution runs through the async API; `planApprovalRequested` / `toolExecutionFinished` deleted (zero connections anywhere).

### ADR 0048: Consolidate Tool-Call Envelope Argument Extraction
- **Context**: `AgentCopilotDockWidget::planArgumentsFor` re-implemented in QJson-land the envelope-shape parsing `ToolCallDispatcher::parseEnvelope` already owned; the same QJson→`Json::Value` conversion appeared at eight call sites with no shared helper.
- **Decision**:
  1. **`ToolCallDispatcher::argumentsFor(envelope)`**: public static delegating to `parseEnvelope` — one owner of envelope shape for `classify`/`submit`/`rejectionReason` and the plan path.
  2. **`jsonValueFromQJson(const QJsonValue&)`** in `json_params_converter.h`: shared recursive QJson→`Json::Value` converter; migrated the agent dock, plan-preview handler, and canvas-sync test to it.
  3. **Delete `planArgumentsFor`**: the plan approval card takes dispatcher-extracted arguments; the dock's run-button and completion paths read `Json::Value` directly instead of round-tripping through `QJsonDocument`.

### ADR 0049: Make LlmStreamingClient a True Pure Transport
- **Context**: `sendChatCompletion(messages, enableTools)` reached into `AtomicAlgorithmRegistry` to inject tool schemas — the caller could not choose which tools went on the wire; request assembly was inline with no test seam; the body honored `m_profile.stream` while the SSE parser only reads `data:` lines, so `stream=false` responses were silently dropped.
- **Decision**:
  1. **`sendChatCompletion(messages, tools = {})`**: caller-supplied tools; the Copilot dock now performs the registry lookup and converts via `json_params_converter.h` helpers.
  2. **`buildChatRequest(profile, messages, tools)`**: pure static returning the `QNetworkRequest` + JSON body pair; `sendChatCompletion` is build → post → SSE-parse.
  3. **Always send `"stream": true`**: the transport is SSE-only; `LlmProviderProfile.stream` stays for persistence/UI but no longer shapes the wire.
  4. **Remove the dead `agent_tool_call_exporter.h` include**.

### ADR 0050: Relocate StacClient to src/app/ and Absorb COG Asset Selection
- **Context**: `StacClient` (src/agent/stac_client.{h,cpp}) was compiled into the app library while living in src/agent/ (absent from its CMakeLists) — an orphan whose sole consumer, `StacBrowserDialog`, also owned COG asset sniffing and `/vsicurl/` prefixing.
- **Decision**:
  1. **Move `StacClient` to `src/app/stac_client.{h,cpp}`**: app and test CMake paths and the dialog include updated; class name and API unchanged.
  2. **`StacClient::selectCogHref(const QJsonObject&)`**: static helper selecting the COG asset (`.tif` href or `image/tiff` type, first in asset-key order), validating via `validateAssetHref`, prefixing `/vsicurl/`; empty when unusable. The dialog calls the one-liner.

### ADR 0051: TaskCenter Owns the JobEngine Listener
- **Context**: `TaskCenter::watchSubmittedJob` spawned a detached polling `std::thread` per job, re-implementing forwarding JobEngine's listener already provides — a watcher alive at process exit caused the flaky ctest #23 SEGFAULT. Dead `JobEngineQtBridge` held the single listener slot; `JobEngine::cancel` invoked CancelHooks while holding `m_mutex` (re-entrancy deadlock).
- **Decision**:
  1. **TaskCenter installs one JobEngine listener** (`onJobRecord`) replacing `watchSubmittedJob`; watcher threads deleted; dedup/delta logic ported with identical observable behavior; listener re-installed on every submit (self-healing after test-side resets); post-submit snapshot catch-up covers fast-finishing jobs; terminal `mark*` methods are terminal-idempotent; the destructor joins engine workers (`shutdown()`) so an in-flight job cannot notify destroyed state.
  2. **Delete `JobEngineQtBridge`** (files, app/test CMake entries, `main_window_docks.cpp` instantiation, stale includes/comments).
  3. **`JobEngine::cancel` invokes the CancelHook after releasing `m_mutex`**, mirroring `notify()`; cancels landing in the worker's pick-vs-arm window arm a pre-set flag the worker adopts.
- **Consequences**: process-exit SEGFAULT race eliminated; single forwarding path; cancel hooks deadlock-free by contract; Running cancels cannot miss the flag window; tests installing their own listener must respect the single slot (ADR 0052 will add job-record pruning).

### ADR 0052: JobEngine Record Retention Policy (Prune Completed Jobs)
- **Context**: `JobEngine` retained every `JobRecord` (full `logLines` + `result` JSON) for the process lifetime — unbounded growth in production; `TaskCenter::clearCompletedTasks` cleared only its own task map and did not propagate to the engine.
- **Decision**:
  1. **`JobEngine::pruneCompleted(maxKeep)`**: evicts the oldest terminal records beyond `maxKeep` ("oldest" = `finishedAtMs`, then `createdAtMs`, then `id`); queued/running records never touched; returns count removed; `clearCompleted()` is `pruneCompleted(0)`.
  2. **`JobEngine::removeCompleted(jobIds)`**: exact-set terminal-record eviction so TaskCenter prunes only the cleared tasks' records, leaving untracked engine jobs (direct submissions) intact.
  3. **`TaskCenter::clearCompletedTasks` propagates**: after clearing its own map it drops the cleared tasks' listener-dispatch/dedup state and removes exactly their engine records; straggler terminal records for pruned jobs no-op as unknown jobIds.
- **Consequences**: bounded engine retention with `list()` as the inspection seam; terminal notifies never lost (pruning happens after the terminal state is set under `m_mutex`, listener receives a copy); "cleared" now means gone from both layers.

### ADR 0053: Delete Dead Classification Task Adapters
- **Context**: `RsClassificationTask` duplicated `RsClassificationPipeline::Config` (only `algoName` vs `methodName` differs) and the main window hand-mapped the same fields again in its apply and preview `submitJob` lambdas; `RsCvTask` was the only QgsTask-coupled file in the headless analysis dir; `RsClassificationPipeline::runCrossValidation` was a 14-line pass-through to `RsCrossValidation::kFold`. All three were production-dead — only tests constructed them.
- **Decision**:
  1. **Delete `RsClassificationTask`** (`rs_classification_task.{h,cpp}`); the main window builds `RsClassificationPipeline::Config` directly at both call sites (`algoName` → `methodName`) and drops the duplicated field-mapping blocks; the e2e test migrates to the pipeline seam (ADR 0019 decision 5).
  2. **Delete `RsCvTask`** (`rs_cv_task.{h,cpp}`); the task-center tests now subclass `QgsTask` locally.
  3. **Delete `RsClassificationPipeline::runCrossValidation`**; the main window calls `RsCrossValidation::kFold` directly, porting the fixed-fraction (0.5) progress bridge into its cancel lambda.
- **Consequences**: one Config vocabulary across GUI and pipeline; `qgis_analysis` is GUI-free again; tests use the sanctioned seam; CV behavior (kFold call, 50% fixed-fraction progress, cancellation) unchanged.

### ADR 0054: Give RsSegmentMap a Write Side; Re-point RsObiaTask onto paint + classify
- **Context**: `RsSegmentMap` was read-only, so writers hand-rolled GDAL code: the hierarchy operator's `writeLabelGeoTiff` and `RsObiaTask::writeOutput` — 178 lines duplicating `RsClassRaster::paint` (same dtype escalation, palette, row loop) without its incomplete-output cleanup; `run()` steps 3–5 also re-implemented `RsObjectClassify::classify`'s train-row selection / fit-if-needed / predict.
- **Decision**:
  1. **`RsSegmentMap::toGeoTIFF(path, refPath, error)`**: UInt32, LZW, georef copied from the reference, NoData=0, fail-closed — missing or size-mismatched reference fails, partial output removed, error conventions matching `paint`.
  2. **Delete the operator's `writeLabelGeoTiff`**; the hierarchy operator calls `toGeoTIFF` (outputs gain NoData=0 metadata; otherwise identical).
  3. **`RsObiaTask` delegates**: `writeOutput` → `RsClassRaster::paint` (dtype keyed on class ids only, not color keys); run() steps 3–5 → one `RsObjectClassify::classify` call; accuracy-assessment block kept.
- **Consequences**: one label writer + one paint path; partial outputs now cleaned up instead of leaked; dtype escalation pinned to class ids (high color key no longer forces UInt16); grid-size mismatch fails instead of reading out of bounds; "No labeled segments" error text and accuracy result preserved (tests pin them); segutil stack (0060), OTB adapter (0058), ROI majority (0060) deferred.

### ADR 0055: Move JM Sample Extraction into the Analysis Layer; Consolidate Class-Map Writing + Dtype Policy
- **Context**: `recomputeJmMatrix` hand-collected ROI pixel indices and read each band with per-pixel 1×1 `GDALRasterIO` — the exact pattern `RsTrainingDataExtraction::buildMatrices` eliminated — and skipped NoData/ignore filtering, so NoData pixels leaked into JM stats; the OBIA classify operator re-implemented the 255/65535 dtype escalation via `segutil::writeClassGeoTiff`, and the hierarchy operator hand-rolled the `classField`→`class`→`id` fallback owned by `RsTrainingDataExtraction::classFieldIndex`.
- **Decision**:
  1. **`RsJmSeparability::computeAll(X, y)`**: consumes `RsTrainingDataExtraction` output, splits into per-class buckets (≥2 samples), returns the full pairwise JM map; the main window's JM path now calls `extract()` + `computeAll` — scanline-grouped reads and the same NoData/ignore filtering as classify.
  2. **`RsPostProcess::saveLabelRaster` becomes the canonical class-map writer**: adopts the ADR 0019 S4 three-tier dtype policy (Byte ≤ 255, UInt16 ≤ 65535, Int32 beyond), palette only for Byte, optional NoData marker (NaN = none); the OBIA classify operator delegates its write (LZW + NoData=0 preserved).
  3. **`rs_obia_hierarchy_operator`** uses `RsTrainingDataExtraction::classFieldIndex` for training-field resolution.
  4. **Pipeline inline writer left as-is**: it writes during tile-streamed predict with crop offsets, palette index 0, options-fallback create — not a clean win now.
- **Consequences**: JM stats exclude NoData/ignore pixels (previously leaked) with pixel dedup matching training extraction; one canonical dtype policy + one class-field fallback; OBIA keeps UInt16 for 256..65535 ids (not the old saveLabelRaster Int32); GUI post-process task passes NaN and keeps its no-NoData behavior; `segutil::writeClassGeoTiff` remains for ADR 0060 scope.

### ADR 0056: Collapse the GCP type Duplication onto QgsGcpPoint
- **Context**: `RsGeorefGcpPair` (session) duplicated `QgsGcpPoint`'s four core fields plus pointType, forcing the shell to convert both ways around the `.points` codec; the save conversion built an empty `QgsCoordinateReferenceSystem()`, so saved GCPs lost their destination CRS; `QgsGcpPoint::mResidual` was a third residual store whose only writer was that save path, and its doc referenced the deleted `QgsGCPList::updateResiduals()`.
- **Decision**:
  1. **Session stores `QgsGcpPoint` values**; `RsGeorefGcpPair` deleted, both shell conversion loops removed; GCPs are created with the panel CRS (or coord-dialog CRS) at add time.
  2. **`.points` v2 gains an optional 10th `crs` column** (authid); the loader prefers it per-point and falls back to the caller-supplied CRS for older files.
  3. **`QgsGcpPoint::residual()`/`mResidual` removed** — residuals live only in `RsGeorefFitResult`, pushed to the view layer via `QgsGeorefDataPoint::setResidual()`; the codec writes format-compat zeros.
- **Consequences**: lost-CRS bug fixed (regression-tested round-trip); one GCP vocabulary across session, warp snapshot, codec, and table; one residual owner (`RsGeorefFitResult`) with stale doc references gone.

### ADR 0057: Consolidate the Fit/Residual Engine and Give RPC an Interface Seam
- **Context**: `RsGeoreferencingSession::refit()` hand-orchestrated enabled-GCP collection, min-count gating, the RPC before/after double-fit, and per-point source-pixel residuals — a residual loop duplicated with `pixelRms()`, a triplicated min-GCP probe, and shell re-validation of what `createWarpSnapshot()` already gates; `QgsRpcGcpTransformer` configuration lived behind concrete-only methods, forcing four `dynamic_cast<QgsRpcGcpTransformer>` sites (session, clone path, warp task, image warper).
- **Decision**:
  1. **Fit/residual engine on `QgsGeorefTransform`**: static `fit(gcps, method, rasterPath, demPath, demZOffset, invertYAxis)` returns one `RsGeorefFitResult` (moved from the session header) — enabled-GCP collection, min-count gating, RPC double-fit, source-pixel residuals, RMS; shared `enabledGcpCount` / `collectEnabledGcps` / `minimumGcpCountFor` statics absorb the triplicated probe and shell re-validation; `refit()` collapses to one call.
  2. **RPC interface seam**: one optional virtual `QgsGcpTransformerInterface::setRpcOptions(sourceRasterPath, demPath, zOffset, refine)` (default no-op returning false) plus a `demPath()` query; the four downcasts deleted; the clone path copies RPC state through the implementation's own `clone()` (kills `copyRpcStateIfPresent`); `RsWarpTask` uses the new concrete `cloneTransform()`.
- **Consequences**: one fit seam — residual math appears once, session shrinks ~130 lines, RMS values / error strings / RPC refinement semantics unchanged; no fragile downcasts — RPC configuration flows through the interface; vendored files touched minimally (two additive virtuals; `QgsRpcGcpTransformer::setRpcOptions` gains the source-path argument).

### ADR 0058: Delete the App Layer's Duplicate OTB Segmentation Adapter
- **Context**: `RsObiaSegmentation::runOtb` re-implemented OTB `Segmentation` CLI orchestration (QProcess lifecycle, cancel polling, temp files) in a different dialect (`-mode meanshift -out shp labels.tif`, shp discarded by the GUI) while `RsOtbSegmenter::segment` already covered the same job in `-mode raster` dialect with input-exists + size validation and temp-dir hygiene — two CLI dialects for one OTB binary.
- **Decision**:
  1. **`runOtb` delegates** to `RsOtbSegmenter::segment(rasterPath, spec, isCanceled)`; `RsObiaSegmentationConfig` maps 1:1 onto `RsLevelSpec` (maxIteration → maxIterations); ~110 lines of QProcess orchestration deleted; no spec extension needed.
  2. **preferOtb→fallback policy kept** as a documented, deliberate divergence from the hierarchy path's no-fallback rule, with an explicit comment.
  3. **`-mode meanshift` vector dialect retired** for OBIA paths; raster mode produces the label image directly (identical GUI-visible behavior) plus stricter validation.
- **Consequences**: one OTB CLI dialect for OBIA paths; cancellation plumbed through `RsOtbSegmenter` unchanged; tests pin `usedOtb`/fallback semantics, not log strings; future candidates untouched — `src/operators/otb/otb_segmentation_operator.cpp` (already raster dialect) and `src/processing/providers/otb_tools/algorithms/otb_segmentation.cpp` (still the retired `-mode meanshift` dialect).

### ADR 0059: Delete the Vendored Vector Warper Cluster; Add Helmert/Projective Numeric Tests
- **Context**: `QgsVectorWarper` (138+166 lines) and `QgsGcpGeometryTransformer` (97+64 lines) were vendored from QGIS for upstream-API parity but have zero production callers — only their own tests use them; the geometry transformer is a ~40-line pass-through to `QgsAbstractGeometryTransformer` (from qgis_core), mirroring the ADR 0020 S3 `QgsGCPList` deletion. Meanwhile `QgsLeastSquares::helmert`/`::projective` (GSL LU/SVD, SICNU-hardened with `SingularException`) had no numeric coverage — and were untestable anyway because no CMake code ever set `HAVE_GSL`, so both always threw `QgsNotSupportedException`.
- **Decision**:
  1. **Delete the warper cluster**: `qgsvectorwarper.h/.cpp`, `qgsgcpgeometrytransformer.h/.cpp`, `tests/test_vector_warper.cpp`, `tests/test_gcp_geometry_transformer.cpp`, and their CMake source-list entries; dead vendored surface is not a goal.
  2. **Wire GSL into the build**: `find_package(GSL)` + `GSL::gsl` linked into `qgis_analysis`, `HAVE_GSL` set before `qgsconfig.h` configure — the GSL-backed helmert/projective fits become live (previously dead code).
  3. **Numeric tests**: `test_least_squares.cpp` gains helmert (known rotation+scale+translation recovery from by-construction correspondences; singular input → `SingularException`) and projective (known homography recovered; degenerate input) cases; `test_gcp_transformer.cpp` gains Helmert/Projective round-trips plus `invertYAxis` cases, including ports of upstream QGIS reference values.
- **Consequences**: ~400 lines of dead vendored code gone; the helmert fit (author's own "derived it myself late at night" doubt) and the SVD projective fit are pinned correct against synthetic ground truth and upstream literals (tolerance 1); GSL is now a real build dependency for the georeferencer, matching upstream QGIS.

### ADR 0060: Converge the Segmentation Operator Stack onto the Analysis Layer
- **Context**: `segutil::segmentQuantize` (operators, cv::Mat, no nodata, weaker merge) is a second teaching segmenter beside `RsSimpleSegmenter` (analysis, RsSegmentMap, nodata-aware); the majority tie-break rule (max votes, ties → smaller id) is re-implemented four times — `rs_parent_link.cpp` P1, `labelFromRoi` in the hierarchy operator (point-in-polygon), the classify operator's votes loop (ALL_TOUCHED rasterize), and an inline test copy; `writeLabelGeoTiff` duplicates `RsSegmentMap::toGeoTIFF` (ADR 0054) and `writeClassGeoTiff` is orphaned since ADR 0055.
- **Decision**:
  1. **One teaching segmenter**: `rs:obia_segment` and `rs:obia_classify` (quantize mode) delegate to `RsSimpleSegmenter::segmentMultiBand`, which gains optional `isCanceled`/`onProgress` hooks (GUI caller stays source-compatible); labels write via `RsSegmentMap::toGeoTIFF`; nodata = band-1 declared value, else NaN; label 0 = nodata.
  2. **One majority kernel**: `majorityKeyWithTieBreak` (`rs_majority_vote.h`) — P1, `RsRoiLabeler`, and the operators' decisions all delegate; vote-collecting loops stay per-site.
  3. **One ROI labeler**: `RsRoiLabeler::labelByMajority` — canonical membership is center-of-pixel rasterize via the existing `RsPixelRasterizer` (shared with training extraction, windowed allocation, matches the hierarchy path's pixel-center semantics); retires point-in-polygon and ALL_TOUCHED (double-counted shared boundaries).
  4. **Deletions**: segutil loses `segmentQuantize`/`mergeSmallRegions`/`writeLabelGeoTiff`/`writeClassGeoTiff`/`rasterizeGeometry` (keeps `segmentGrid`); `labelFromRoi` deleted; the inline test copy becomes kernel tests; `rs:obia_hierarchy` gains registration + schema + OTB-gated smoke coverage.
- **Consequences**: quantize-path output changes (different blur kernel, merge algorithm, id assignment) — accepted, no test pins old ids/counts; classify ROI labels converge to center-of-pixel boundary semantics (hierarchy path keeps pixel-center semantics via rasterize, gains windowed allocation); one `RsSegmentMap` stack for teaching segmentation; segutil shrinks to the grid fallback.

### ADR 0061: Consolidate Classification Operator Helper Duplications; Replace the KMeans Magic String with a Backend Virtual
- **Context**: backend construction existed in four copies (three byte-identical `makeBackend` bodies in the operators plus the pipeline predict-only "bayes" string sniff); the `(classId * 47) % 360` color formula and the `mt19937(42)` subsampling policy were re-implemented per adapter; per-band NoData discovery was duplicated between training extraction and the pipeline; the pipeline branched on `methodName == "KMeans"` to gate the Hungarian cluster→class remap, forcing the K-Means operator to pass a lowercase `"kmeans"` workaround string.
- **Decision**:
  1. **`RsClassifierBackendFactory`** (analysis classification layer, beside `RsClassifierBackend`): one `create(methodName)` — case-insensitive `bayes` → NormalBayes, `kmeans` → K-Means, fallback SVM — plus `createKMeans(k)`; the supervised / OBIA / K-Means operators and the pipeline predict-only path all construct here.
  2. **`rs_classification_utils.h`** owns `rsSynthesizedClassColor` (exact formula) and `rsShuffleAndKeep` (mt19937(42) subsample policy, shared by training extraction and the K-Means operator).
  3. **`rsCollectBandNodata`** (beside `RsPixelIgnoreOptions`) owns per-band GDAL NoData discovery (training extraction + pipeline tile path).
  4. **`RsClassifierBackend::needsLabelRemap()`** virtual — K-Means returns true only when fitted with real (non-zero) labels, so the pipeline gates the Hungarian remap on the backend: the `"KMeans"` string branches and the lowercase workaround are deleted; dead `canonicalMethod` / `readLegacyMethodFromMeta` helpers in the supervised operator deleted.
  5. **New remap test** trains K-Means with permuted label ids (5/9) and asserts predictions map to the training labels in the accuracy path and the written class map, with a lowercase `"kmeans"` methodName to pin the trap removal.
- **Consequences**: one construction path, color formula, sampling policy and NoData discovery; remap semantics observably unchanged (identity when no table; the unsupervised operator's all-zero dummy trainY keeps raw 1..K cluster ids); `"kmeans"` strings now construct K-Means instead of falling back to SVM — only reachable via sidecar predict-only, which still fails cleanly (K-Means has no `load()`).

### ADR 0062–0122: Index

ADR 0062 onward moved to per-file records in `docs/adr/` (full context, decision, and consequences in each file). Titles for orientation:

- **ADR 0062**: Unified Algorithm Execution Seam
- **ADR 0063**: Task Center RSS Watermark Throttling
- **ADR 0064**: Shared Memory Zero-Copy Data Channel
- **ADR 0065**: Semantic Band Roles
- **ADR 0066**: Raster Grid Compatibility
- **ADR 0067**: QA Cloud Shadow Snow Masking
- **ADR 0068**: Unified Product Import Dialog
- **ADR 0069**: Radiometric Calibration Workflow Integration
- **ADR 0070**: Atmospheric Correction Workflow Integration
- **ADR 0071**: Orthorectification Dialog
- **ADR 0072**: Change Detection 2 — Methods, Thresholds, Cleanup, Area
- **ADR 0073**: Large Raster Memory Policy Classification
- **ADR 0074**: Classification Model Metadata Compatibility
- **ADR 0075**: Minimum Noise Fraction
- **ADR 0076**: Spectral Information Divergence
- **ADR 0077**: Linear Spectral Unmixing
- **ADR 0078**: RX Anomaly Detection
- **ADR 0079**: Spectral Resampling
- **ADR 0080**: Endmember Extraction (PPI)
- **ADR 0081**: Spectral Library Domain
- **ADR 0082**: Wavelength-Aware Spectral Profile
- **ADR 0083**: Reusable Preprocessing DAG
- **ADR 0084**: ROI Mean Spectrum
- **ADR 0085**: Fusion/PCA Scalability Review
- **ADR 0086**: Provenance Lineage in the Data Manager Panel
- **ADR 0087**: Semantic Band Roles in the Agent Workspace Snapshot
- **ADR 0088**: Apply QA Mask to Product
- **ADR 0089**: Post-Classification Change
- **ADR 0090**: Semantic Band Roles in MCP describe_dataset
- **ADR 0091**: Grid Harmonization — Reproject with Reference
- **ADR 0092**: Spectral Library Matching Workbench
- **ADR 0093**: Per-Class Classification Diagnostics
- **ADR 0094**: Classification Probability Output
- **ADR 0095**: Change Detection Align DAG
- **ADR 0096**: Wavelength-Aware Library Matching
- **ADR 0097**: ROI Mean-Spectrum Tool
- **ADR 0098**: Shared Grid Builder
- **ADR 0099**: Task-Centric RS Menu
- **ADR 0100**: Continuum-Removal Profile View
- **ADR 0101**: Post-Classification Dialog
- **ADR 0102**: Shared Band-Role Combo
- **ADR 0103**: Spectral Index Adopts Band-Role Combo
- **ADR 0104**: Real Raster Previews in Comparison Dialog
- **ADR 0105**: Code Review Remediation
- **ADR 0106**: Apply-Mask Offset Precompute
- **ADR 0107**: Shared Raster-Layer Combo
- **ADR 0108**: Operator Result Summary in Dialogs
- **ADR 0109**: Shared CRS Selector
- **ADR 0110**: Batch Processing RS Operators
- **ADR 0111**: Dialog Grid Preflight
- **ADR 0112**: MCP Lineage Query
- **ADR 0113**: Batch Parameter Overrides
- **ADR 0114**: Radiometric State Metadata
- **ADR 0115**: Change Detection Statistical Threshold & MMU
- **ADR 0116**: Change Detection Dialog Alignment
- **ADR 0117**: Execution Estimates
- **ADR 0118**: Batch QGIS Parameters
- **ADR 0119**: Review Remediation Round 2
- **ADR 0120**: Agent-Ready Atomic Architecture
- **ADR 0121**: Agent Interaction Layer
- **ADR 0122**: Pi-Based Spatial Intelligence Layer
