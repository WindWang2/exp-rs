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
The stateless external-agent JSON-RPC protocol adapter at the Task Center seam: it owns stdio framing, the tool allow-list / workspace-path security policy, the meta-tool catalog, and the `mcpStatusForTask` status mapping — but no execution machinery of its own. Single calls become Task Center tasks (`execution_id` = `"task-<taskId>"`); `rs:` operators route through the Tool Call Dispatcher, provider algorithms through `TaskCenter::enqueueTask`.
_Avoid_: MCP worker, Agent runner, Tool executor

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
The JSON-RPC serialization bridge (`AppInterfaceBridge` in `src/python/isolated`) consumed by `PythonAppInterfaceProxy` for out-of-process Python plugin workers. `DataManager` is its required asset authority — catalog queries, source registration (`openPath`), and the explicit plugin-driven active asset (`setActiveAsset`/`activeAssetId`, replacing the canvas current layer) — while `ActiveViewHost` is an optional enhancement bound only in GUI mode for display, canvas state, and message bar. IPC methods degrade gracefully without a view host (`no_canvas`, `no_active_layer`, `ui_unavailable`), so the seam works without any QWidget.
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
  2. **Single Seam Coupling**: `PythonAppInterfaceProxy` and `SicnuAppInterface` hold ONLY a single `ActiveViewHost*` pointer, completely isolating IPC handlers from raw C++ GUI widget trees.
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

### ADR 0023: Python Plugin Host Extraction & Headless CLI Plugin Loading Architecture
- **Context**: `py:` algorithms were unreachable from every headless surface: `PluginManager`/`PythonPluginAdapter` lived in the app shell, no headless entry point owned a `DataManager`, JobEngine had no `py:` prefix executor, and the only `py:` execution face (`PythonAlgorithmAdapter::execute` → `sendRequestAndAwait`) assumes the main thread while CLI pipeline tasks run on JobEngine worker threads with a wait loop that never pumps Qt events. Spec: `docs/superpowers/specs/2026-08-01-cli-python-plugin-host-spec.md`.
- **Decision**:
  1. **Single Hosting Core**: Extract the Python hosting machinery (worker pool, proxy/bridge wiring, `classFactory` lifecycle, `py:` algorithm registration) from `PluginManager` into the GUI-free **Python Plugin Host**. `PluginManager` keeps C++ plugins, directory scanning, and menu/window wiring, and composes the host — one implementation, no parallel headless copy.
  2. **Full Lifecycle, Degraded UI**: Headless loading runs the complete plugin lifecycle (`metadata.txt`, `classFactory(iface)`, algorithm registration); UI-dependent plugin calls degrade through the Headless Asset Seam's existing status codes (`ui_unavailable`, `no_canvas`, `no_active_layer`).
  3. **Headless DataManager Ownership**: The CLI creates and owns a `DataManager`, injects it into the host's bridge wiring, and registers completed pipeline task outputs as `TaskTemporary` Data Assets via `DataManager::registerSource` + `attachDerivationRecord`, so plugin catalog calls see real state. `OutputCommitter` is deliberately not used: its atomic temp→stable rename and `DeletableSource` ownership fit TaskCenter-owned temp files, not user-declared final output paths.
  4. **Main-Thread Marshaling**: A `py:` JobEngine prefix executor marshals `AlgorithmEngine::executeAlgorithm` to the main thread (`Qt::BlockingQueuedConnection`, direct call when already there); the CLI pipeline wait loop interleaves `processEvents()`. `PythonIpcServer` and ticket 02's verified await mechanism stay untouched.
  5. **Explicit Plugin Declaration**: The CLI loads only plugins named by repeatable `--python-plugin` options and aborts before the pipeline starts if any declared plugin fails to load; the host itself holds no loading policy, so the desktop (scan-all) and MCP (config) plug in their own.




