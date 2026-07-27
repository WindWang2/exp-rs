# Specification: Project Data Manager and Display Separation

## Problem Statement

The application currently treats a QGIS map layer as data identity, display state, project membership, and often processing input at the same time. `LayerManager` loads raster and vector sources, creates `QgsMapLayer` instances, registers them in the global `QgsProject`, inserts them into the layer tree, synchronizes canvases, and removes the project layer when the user removes a selected layer. Session windows such as Classification use a separate `RsSessionMapWorkspace` and `QgsMapLayerStore`, while processing completion, STAC, georeferencing, OBIA, startup, and vector workflows contain additional direct `QgsProject::addMapLayer()` paths.

This model prevents the application from reliably expressing several remote-sensing workflows:

- one dataset displayed in multiple views with independent band compositions and renderers;
- data registered and processed without first being shown on a canvas;
- explicit distinction between removing a presentation and unloading data;
- safe processing inputs that are independent of the active layer;
- product and container structures such as Landsat scenes, Sentinel SAFE products, HDF, and NetCDF;
- virtual multi-source raster stacks with explicit grid and overlap policy;
- remote maps that are renderable but do not claim local pixel-analysis capabilities;
- temporary, in-memory, missing, stale, and derived data with defined lifecycles;
- structured provenance and revision-aware caching.

## Decision Summary

Introduce a project-scoped Data Manager as the authority for Data Assets and a separate Display Manager as the authority for Display Views and Display Layers.

The Data Manager owns data identity, structural metadata, capabilities, revisions, dependencies, leases, persistence policy, and provenance. It has no dependency on Qt Widgets, `QgsMapCanvas`, layer trees, renderers, or processing execution.

The Display Manager owns views and independent display layers. Each QGIS-backed Display Layer owns a distinct `QgsMapLayer` instance whose renderer and other QGIS display state are authoritative at runtime. A Display Layer references one Data Asset and belongs to exactly one Display View.

Algorithms and Algorithm Tasks reference Asset IDs and expected revisions. A processing resolver converts those references into immutable source descriptions suitable for the existing algorithm implementations. Successful outputs are committed to the Data Manager before optional display.

The existing QGIS `.qgs/.qgz` format remains the project container. Standard QGIS content preserves the main map for interoperability; SICNU GEO RS extension content preserves the Data Manager catalog, extra views, virtual recipes, identity mapping, and provenance.

## Architectural Shape

```text
Application
├─ ConnectionCatalog                         application-scoped
│  └─ public connection settings + authConfigId
│
└─ ProjectContext                            one per open project
   ├─ DataManager                            no Widgets / canvas dependency
   │  ├─ DataCollection catalog
   │  ├─ DataAsset catalog
   │  ├─ identity + revision index
   │  ├─ lease registry
   │  ├─ dependency DAG
   │  └─ derived metadata cache
   │
   ├─ DisplayManager                         QGIS GUI adapter
   │  ├─ DisplayView
   │  └─ DisplayLayer → distinct QgsMapLayer
   │
   ├─ ProcessingAssetResolver
   │  └─ AssetRef → algorithm-native source snapshot
   │
   └─ ProjectSerializer
      └─ .qgs/.qgz standard content + SICNU extension
```

The Data Manager is the deep module. Callers cross one small interface to register, inspect, lease, reload, relocate, and unload assets; source detection, deduplication, lifecycle validation, dependency impact, revision rules, and provider selection remain inside its implementation.

## Domain Model

### Identity

`AssetId` is a project-stable UUID persisted in the project. It is not a file path, QGIS layer ID, provider URI, or product identifier.

`SourceKey` is the non-secret canonical identity used for deduplication within a project. It includes provider kind, canonical source, subdataset identity, and data-interpreting open options. It excludes renderer state, opacity, band composition, passwords, tokens, and short-lived signatures.

Registering the same SourceKey twice reuses its Data Asset. A new display request creates another Display Layer, not another Data Asset.

`AssetRevision` identifies the resolved meaning of an Asset ID. An explicit reload or authorized in-place change advances the revision. Algorithm inputs and caches include the expected revision.

### Data Nodes

```text
DataNode
├─ DataCollection
│  ├─ ProductCollection
│  └─ ContainerCollection
│
└─ DataAsset
   ├─ LocalRasterAsset
   ├─ VectorAsset
   ├─ RemoteMapAsset
   └─ VirtualRasterAsset
```

A Data Collection is organizational and is not automatically renderable or processable. It groups child assets and shared product metadata.

A Data Asset is the lifecycle and identity unit. Asset kind does not imply capability; callers query declared capabilities.

A Band Reference addresses a band inside a Raster Data Asset and shares the parent asset lifecycle. Extracting or deriving a band may produce another Data Asset.

### Capabilities

Capabilities replace unsafe raster/vector type assumptions. Initial capability vocabulary includes:

- `Renderable`
- `ReadablePixels`
- `BandMetadata`
- `BandStatistics`
- `QueryableFeatures`
- `EditableFeatures`
- `Temporal`
- `OfflineCacheable`
- `Exportable`
- `Relocatable`
- `DeletableSource`

Local rasters normally declare `Renderable`, `ReadablePixels`, and band capabilities. WMS, WMTS, TMS, and XYZ assets may declare `Renderable` and `OfflineCacheable` but do not claim `ReadablePixels` or `BandStatistics` merely because QGIS represents them with `QgsRasterLayer`.

### State

```text
Registered
  → Resolving
     ├─ Ready
     ├─ Missing
     ├─ Offline
     ├─ AuthenticationRequired
     └─ Error

Ready
  → Stale
  → Resolving after explicit reload
```

Project loading reconstructs registered descriptions first. Resolution is lazy. Visible Display Layers and processing requests trigger resolution; invisible catalog items do not open providers or contact remote services solely because the project was opened.

Missing, Offline, AuthenticationRequired, Error, and Stale assets remain in the project. Their identity, display configuration, and dependency information are not silently deleted.

### Persistence and Storage

Persistence and storage are orthogonal:

```text
PersistencePolicy
├─ ProjectPersistent
├─ SessionTemporary
└─ TaskTemporary

StorageKind
├─ File
├─ TemporaryFile
├─ Memory
└─ Remote
```

SessionTemporary assets are full Data Assets: they can be displayed, processed, leased, and promoted. They survive task and view closure but are not serialized and are cleaned when the project session closes.

TaskTemporary assets are internal pipeline artifacts and are cleaned when their task scope ends and no lease remains.

Project save performs a preflight for persistent objects that reference non-persistent assets. It does not silently serialize temporary paths or discard dependent views.

### Value Domain

Raster data interpretation is explicit:

- Raw DN
- Scaled physical value
- TOA reflectance
- Surface reflectance
- Temperature
- Categorical

Calibration is data semantics rather than hidden display state. Raw data remains unchanged; calibrated meaning is expressed as a Virtual Raster Asset or a materialized derived asset. Algorithms declare compatible value domains.

### Provenance and Dependencies

A Virtual Raster Asset has Strong Dependencies on its inputs. Strong Dependencies form a directed acyclic graph and block normal input unload.

A materialized derived output stores Provenance Links to its inputs. Provenance Links do not block input unload because the output is independently readable.

Every algorithm-produced Data Asset stores a structured Derivation Record containing:

- algorithm ID and version;
- parameter snapshot;
- input Asset IDs, revisions, Band References, and value domains;
- output Asset ID;
- execution and software version information;
- reference to the successful Algorithm Task.

Sensitive authentication material is excluded.

## Data Manager Interface

The exact C++ spelling may evolve during implementation, but the external seam must remain close to the following shape:

```cpp
namespace sicnu::data
{

struct AssetId;
struct AssetRef
{
  AssetId id;
  quint64 expectedRevision = 0;
};

struct RegisterRequest
{
  SourceDescriptor source;
  PersistencePolicy persistence = PersistencePolicy::ProjectPersistent;
};

struct RegisterResult
{
  AssetId assetId;
  bool reusedExisting = false;
  QVector<Diagnostic> diagnostics;
};

struct AssetQuery;
struct AssetSnapshot;
struct AssetUse;
struct UnloadPlan;
class AssetLease;

class DataManager : public QObject
{
  Q_OBJECT

public:
  RegisterResult registerSource( const RegisterRequest &request );
  std::optional<AssetSnapshot> asset( AssetId id ) const;
  QVector<AssetSnapshot> assets( const AssetQuery &query = {} ) const;

  Result<AssetLease> acquire( AssetRef asset, const AssetUse &use );
  UnloadPlan planUnload( AssetId id ) const;
  Result<void> unload( const UnloadPlan &confirmedPlan );
  Result<void> reload( AssetRef asset );
  Result<void> relocate( AssetId id, const SourceDescriptor &replacement );

signals:
  void assetAdded( AssetId id );
  void assetChanged( AssetId id, AssetChange change );
  void assetAboutToUnload( AssetId id );
  void assetRemoved( AssetId id );
};

}
```

Interface constraints:

- no `QWidget`, `QgsMapCanvas`, `QgsLayerTree`, or `QMessageBox`;
- no long-lived mutable `QgsMapLayer *`, `QgsDataProvider *`, or `GDALDataset *`;
- snapshots returned to callers are immutable values;
- errors and unload impacts are structured results, not dialogs;
- catalog mutations occur on the Data Manager's owning thread;
- expensive discovery, statistics, and I/O use Task Center integration outside the synchronous core transaction.

The Data Manager implementation may contain internal seams, but callers do not learn the provider registry, cache implementation, dependency graph representation, or serialization layout.

## Data Source Provider Seam

Data source differences are handled by registered adapters rather than a central type switch:

```text
DataSourceProviderRegistry
├─ GdalRasterProvider
├─ OgrVectorProvider
├─ SatelliteProductProvider
├─ HdfContainerProvider
├─ WmsProvider
├─ XyzTileProvider
└─ VirtualRasterProvider
```

The provider seam is real because multiple production adapters and an in-memory test adapter vary behind it. Providers handle:

- lightweight probing and confidence;
- side-effect-free discovery;
- canonical SourceKey construction;
- structural metadata resolution;
- capability declaration;
- thread-local reader descriptions;
- descriptor serialization.

Providers do not create widgets or insert map layers. Display materialization belongs to a display adapter.

## Display Manager

A Display View represents a viewport and owns an ordered layer tree. A Display Layer represents one independent presentation of one Data Asset and belongs to exactly one Display View.

```cpp
DisplayViewId createView( const DisplayViewSpec &spec );
Result<DisplayLayerId> addLayer( DisplayViewId view, AssetId asset,
                                 const AddLayerOptions &options = {} );
Result<DisplayLayerId> cloneLayer( DisplayLayerId source, DisplayViewId target );
Result<void> removeLayer( DisplayLayerId layer );
```

For QGIS two-dimensional views:

- every Display Layer owns a distinct `QgsMapLayer`;
- the Display Layer holds an Asset Lease for its lifetime;
- `QgsMapLayer` owns authoritative runtime renderer and presentation state;
- cloning uses the QGIS adapter to clone presentation state;
- layer order, grouping, and visibility belong to the Display View's layer tree;
- removing a Display Layer never implies unloading its Data Asset.

The Data Manager never refreshes a canvas. The Display Manager observes asset lifecycle events and recreates or marks affected display adapters when an asset reloads, becomes unavailable, or is about to unload.

`RsSessionMapWorkspace` is prior art for view-local layer stores, but it currently accepts caller-owned `QgsMapLayer *` and has no Asset identity or lease. It should migrate behind the Display Manager rather than becoming a second Data Manager.

## Complex Products and Containers

Complex sources use a discovery transaction:

```text
Probe
  → Discover without catalog mutation
  → Import Preview
  → User selection
  → Atomic catalog commit
```

Landsat scenes, Sentinel SAFE products, MODIS products, HDF, and NetCDF are registered as Data Collections with selected child Data Assets.

Different grids are not flattened into a false multi-band raster. Sentinel 10 m and 20 m groups remain distinct. HDF/NetCDF subdatasets with independent grids or coordinate systems are independent child assets.

The existing `SatelliteProducts::ProductInfo` and GDAL subdataset discovery are provider implementation inputs, not the final domain model.

## Virtual Raster Assets

A cross-file or cross-subdataset band composition is a Virtual Raster Asset rather than display-layer multi-source logic.

Its recipe records:

- ordered input Band References or Raster Assets;
- target CRS;
- target grid and resolution;
- extent policy;
- resampling method by input semantics;
- NoData policy;
- value-domain transformation.

Creation requires a preflight that returns structured diagnostics:

- Compatible
- RequiresReprojection
- RequiresResampling
- PartialOverlap
- NoOverlap
- MissingCRS
- UnavailableSource
- UnsupportedDataType

RGB and ordinary multi-band stacks default to the spatial intersection of all inputs. Empty intersection rejects creation. Union with NoData is an explicit advanced choice. Different grids require an explicit target and resampling policy. Categorical inputs cannot silently use continuous resampling.

A valid virtual asset receives an Asset ID and leases all Strong Dependency inputs. It can be displayed or processed lazily and can later be materialized through an Algorithm Task.

## Metadata and Derived Cache

Structural metadata is resolved synchronously when an asset is resolved:

- dimensions and band count;
- data type;
- CRS, extent, and geotransform;
- NoData and color interpretation;
- basic source and product information.

Expensive metadata is computed asynchronously:

- Real Data Range;
- mean and standard deviation;
- histograms and percentiles;
- thumbnails and overviews.

Derived cache keys include Asset ID, revision, Band Reference, spatial scope, exact-versus-sampled policy, and NoData policy. Sampled and exact results are visibly distinguished.

The Data Manager does not duplicate QGIS/GDAL raster-block or network-tile caches. A cache coordinator may clear or configure existing provider caches while the Data Manager owns only semantic derived artifacts. Offline map download is an explicit task that produces a managed offline asset.

Raw provider metadata is preserved alongside normalized remote-sensing semantics such as spectral role, wavelength, units, scale/offset, acquisition time, platform, sensor, product level, spatial resolution, and inference confidence. User overrides are persisted without destroying raw metadata.

## Processing Integration

Application-facing algorithm requests use Asset References:

```text
Algorithm Request
  → AssetId + ExpectedRevision
  → ProcessingAssetResolver
  → immutable algorithm-native source snapshot
  → Task Center execution with Asset Lease
```

Existing algorithms may continue to receive paths, provider URIs, or their existing native values internally. The resolver is the migration seam; canvas layers are not algorithm inputs.

Normal raster processing produces new assets. Authorized in-place changes require an exclusive Write Lease, atomic replacement where possible, revision advance, cache invalidation, and Display Layer reconstruction.

Task output commit is transactional:

```text
write temporary output
  → validate completeness
  → atomically publish stable output
  → register Data Asset
  → attach Derivation Record
  → emit assetAdded
  → optionally ask Display Manager to add a layer
```

Failed or cancelled tasks do not register apparently valid persistent outputs.

The Data Manager panel may offer export, convert, materialize, and promote actions, but Algorithm Engine performs the operation and Task Center owns execution.

## Vector Editing

Independent vector Display Layers retain independent QGIS presentation state, but uncommitted QGIS edit buffers are not a cross-view collaboration mechanism.

One Vector Asset may grant at most one Edit Lease at a time. The Display Layer holding that lease is editable; other Display Layers are read-only. Commit advances the Asset Revision and refreshes other display adapters. Moving editing to another view requires commit or rollback of the current edit session.

## Lifecycle and Destructive Operations

A Display Layer, Algorithm Task, edit session, or Strong Dependency holds an Asset Lease. Normal unload is rejected while leases or downstream Strong Dependencies exist.

`planUnload()` returns the complete impact before mutation. Explicit cascade unload may remove dependent virtual assets and display layers after confirmation.

Unloading never deletes source data. Physical deletion is a separate capability-limited command. External imported originals do not declare `DeletableSource` by default. Multi-file formats such as ENVI and Shapefile are treated as resource sets. Complex product directories default to unload-only.

External file changes mark an asset Stale. They do not silently reload it. Explicit reload revalidates metadata, advances the revision, invalidates derived caches, revalidates virtual dependents, and rebuilds display adapters.

## Project Persistence and QGIS Interoperability

The QGIS project remains the persistence container:

```text
.qgs/.qgz
├─ standard QGIS project state
│  ├─ main view layer tree
│  ├─ QgsMapLayer presentation state
│  ├─ CRS and layouts
│  └─ AssetId / DisplayLayerId custom properties
│
└─ SICNU GEO RS extension state
   ├─ Data Asset descriptors
   ├─ Data Collections
   ├─ virtual recipes
   ├─ extra Display Views
   ├─ identity relationships
   └─ provenance
```

The main view stays readable by ordinary QGIS. SICNU extension state is ignored by QGIS when unsupported.

Opening a standard QGIS project adopts supported layers into the Data Manager, deduplicates by SourceKey, preserves renderer and layer-tree position, and writes identity properties. Unsupported third-party layers remain External Display Layers with restricted data capabilities.

Local data is linked in place by default. Sources inside the project directory use relative paths where possible; external sources retain absolute references and relocation fingerprints. Copying data into the project and packaging a project are explicit tasks.

Credentials are never stored in Data Asset descriptors, SourceKeys, project extension XML, provenance, or logs. Remote descriptors store only an `authConfigId`. Missing authentication yields AuthenticationRequired without changing the Asset ID.

## Data Manager and Layer Tree User Model

The Data Manager panel and Display View layer tree remain separate:

| Data Manager | Display View layer tree |
|---|---|
| all project Data Assets | Display Layers in one view |
| structural metadata and status | order, groups, and visibility |
| export, convert, relocate, reload, unload | renderer, bands, opacity, remove from view |
| reference counts and dependency navigation | current display selection |

Double-clicking or dragging a Data Asset asks the Display Manager to create a Display Layer. Removing a layer only removes presentation. Unloading an asset invokes impact planning.

The application exposes both "Add to Data Manager" and "Open and Display". Complex imports and batch imports do not automatically create a layer for every discovered asset. Algorithm output display is an explicit option.

## Threading

- The Data Manager catalog mutates on its owning thread.
- QGIS Display Layers are created, mutated, and destroyed according to GUI-thread requirements.
- Background tasks receive immutable resolved-source snapshots.
- Each Display Layer or worker creates its own active provider/reader session.
- Mutable `GDALDataset *`, `QgsDataProvider *`, and GUI `QgsMapLayer *` are not shared across workers.
- Asset descriptions and safe derived caches may be shared.
- Tasks validate ExpectedRevision before execution and again before committing results.

## Existing-Code Migration

The migration seam begins at current `LayerManager::loadRasterLayer()` and `loadVectorLayer()` callers.

`LayerManager` becomes a temporary compatibility facade:

```text
legacy loadRasterLayer(path)
  → DataManager.registerSource(path)
  → DisplayManager.addLayer(mainView, assetId)
```

Direct layer insertion paths in Task Center output loading, processing dialogs, STAC, georeferencing, OBIA, startup, and vector workflows migrate incrementally to the same seam.

The prior Unified Display Framework specification remains useful for QGIS renderer and map-renderer mechanics, but its framing of `QgsMapLayer` as the data entity is superseded by ADR-0009. A QGIS map layer is now a Display Layer adapter; Data Asset identity exists independently.

## First Deliverable

The first closed loop supports local GDAL raster and OGR vector sources, one main Display View, QGIS project persistence, legacy project adoption, independent display layers, unload protection, Missing state, and relocation.

It is complete when:

1. opening one raster twice creates one Data Asset and two independently styled Display Layers;
2. removing one Display Layer leaves the Data Asset registered;
3. unloading a referenced asset is rejected until an explicit cascade is confirmed;
4. raster and vector sources share Data Asset identity while vector editing allows one Edit Lease;
5. save and reopen preserve Asset IDs, display relationships, order, and style in `.qgz`;
6. standard QGIS layers are automatically adopted without losing style or tree placement;
7. missing sources survive project load and recover through relocation;
8. the Data Manager has no Widgets/canvas dependency and new application loading paths stop treating `QgsProject::instance()` as the data authority.

## Testing Decisions

Tests cross the Data Manager or Display Manager interface rather than provider internals.

- Data Manager tests use an in-memory source-provider adapter to verify deduplication, revision, state, leases, dependency impact, persistence policy, and structured errors.
- GDAL/OGR adapter tests verify canonical SourceKeys and structural metadata using small repository fixtures.
- Display Manager tests verify independent QGIS layer instances, renderer isolation, view ownership, and layer-removal semantics.
- Project round-trip tests verify stable Asset IDs and adoption of a standard QGIS project.
- Missing-source tests verify retention and relocation without widget construction.
- Architecture tests verify that `src/data` does not depend on Qt Widgets or `qgis_gui`, and that processing input resolution does not consume GUI map-layer pointers.

Tests assert observable behavior through the highest available seam. Internal provider-selection details, QObject wiring, cache containers, and dependency-graph representation are not test surfaces.

## Deferred After the First Deliverable

- WMS, WMTS, TMS, XYZ, and Connection Catalog UI;
- complex product import preview;
- Data Collections and normalized satellite metadata UI;
- Virtual Raster creation and preflight UI;
- Asset-ID conversion for all processing entry points;
- structured Derivation Records for every algorithm;
- multiple simultaneous Display Views;
- offline remote-map materialization;
- project packaging;
- full derived-cache budget coordination.

The types and seams introduced in the first deliverable must leave room for these capabilities without implementing hypothetical adapters prematurely.
