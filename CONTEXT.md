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
