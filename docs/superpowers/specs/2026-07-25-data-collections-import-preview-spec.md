# Spec: Data Collections and Complex-Product Import Preview

**Parent:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md` — Complex Products and Containers (lines 306-322), DataNode model (lines 74-88), and the Phase 1 plan's Deferred Follow-Up Order item 3.
**Status:** Proposed.

## Problem Statement

Remote-sensing data rarely arrives as a single file. A Landsat scene is a directory of band rasters plus a metadata file; a Sentinel-2 SAFE product is a nested directory tree with 10 m, 20 m, and 60 m grid groups; MODIS, HDF, and NetCDF are single containers exposing many subdatasets on independent grids or coordinate systems. Today the application flattens all of this: the satellite-import operators (`rs:landsat_import`, `rs:sentinel2_import`, `rs:modis_import`) call `SatelliteProducts::discover*` internally, stack everything matching a default band list into **one** GeoTIFF, and emit that single stacked raster as the only output. The user never sees what was discovered, never chooses which bands or grids to keep, and the original product structure (which bands belong together, which grid they share, what the product level/platform/sensor is) is destroyed on import.

The Data Manager's catalog is flat — `registerSource` is atomic and single-asset, with no grouping, no parent-child relationships, and no concept of a collection. There is no "Probe → Preview → Select → Atomic commit" discovery transaction anywhere; discovery is buried inside operators as an implementation detail, and there is no import-preview UI (the closest thing is a command-line text preview in the generic algorithm dialog).

The user-facing problem: importing a Sentinel-2 product silently discards the 20 m and 60 m groups (or worse, resamples them into a false multi-band raster); the user cannot import only the bands they need; the registered output has no record of the product structure or metadata it came from; and a re-import produces a different stacked artifact with no way to tell it represents the same scene.

## Solution

Introduce a **Data Collection** as a first-class catalog node and a **discovery transaction** that registers complex products structurally rather than as a flattened raster.

- A **Data Collection** is an organizational catalog node (a `CollectionId` UUID) that groups child Data Assets and carries shared product metadata (platform, sensor, product level, acquisition time, processing level). It is not itself renderable or processable. A collection owns its children's grouping but does not change their identity, leases, or lifecycle — a child asset is still a full Data Asset that can be displayed, leased, processed, reaped, and promoted independently.
- A **discovery transaction** lets a caller probe a complex source (a product directory, an HDF/NetCDF container, a multi-scene Landsat tile) **without mutating the catalog**, present the discovered children as an import preview, accept the user's selection, and then **atomically commit** the collection plus the chosen children in one step. A cancelled or failed import registers nothing.
- The existing `SatelliteProducts::ProductInfo` and GDAL subdataset enumeration become **provider inputs** to discovery, not the domain model — the discovery service consumes them to build a normalized preview tree (child candidate assets + their grids/metadata), and the Data Manager owns the collection + child registration.

Different grids are not flattened into a false multi-band raster: a Sentinel-2 10 m group and 20 m group register as distinct child assets under the same collection; HDF/NetCDF subdatasets with independent grids or CRS are independent child assets.

## User Stories

1. As an analyst, I want to point the importer at a Sentinel-2 SAFE directory and see what's inside before anything is registered, so that I understand the product structure before committing.
2. As an analyst, I want to select which bands and which grid groups to import from a complex product, so that I import only what I need instead of a flattened stack.
3. As an analyst, I want the imported bands grouped under their source product in the Data Manager, so that I can see "this is Sentinel-2 scene S2A_X, 10 m group" rather than a pile of unrelated rasters.
4. As an analyst, I want the 10 m and 20 m groups of one Sentinel scene to remain distinct rasters (not resampled into one multi-band file), so that the resolution semantics are preserved.
5. As an analyst, I want a cancelled import to register nothing, so that a half-imported product never appears in my catalog.
6. As an analyst, I want the collection's shared metadata (platform, sensor, acquisition date, product level) visible on the collection node, so that I can identify and distinguish products.
7. As an analyst, I want to remove (unload) a collection and have it offer to remove its children too, so that cleaning up a product does not leave orphan child assets.
8. As an analyst, I want to display or process a child asset exactly as I would a directly-registered raster, so that collection membership does not limit what I can do with a band.
9. As an analyst, I want a collection and its children to survive save and reopen with their grouping and metadata intact, so that reopening my project restores the product structure.
10. As an analyst, I want an HDF or NetCDF container's subdatasets on independent grids to register as independent child assets, so that incompatible grids are not forced into one raster.
11. As a developer, I want discovery to be a read-only probe that does not mutate the catalog, so that a preview or cancelled import leaves no trace.
12. As a developer, I want the atomic commit to register the collection and all chosen children in one step or none, so that a failed child registration cannot leave a partial collection.

## Implementation Decisions

- **Data Collection is a first-class catalog node.** A new `CollectionId` (UUID, parallel to `AssetId`) identifies a collection. The DataManager gains a collection catalog: a `CollectionRecord` holding the id, display name, product metadata, and an ordered list of child `AssetId`s. A child asset gains an optional parent `CollectionId` on its record (a flat parent pointer; **no nesting in this wave** — a collection cannot contain a collection, matching the "keep it simple until a second real caller needs nesting" discipline). This is queryable ("children of this collection"), serializable, and lighter than a full recursive DataNode tree.
- **Product metadata is a value on the collection.** A `ProductMetadata` value (platform, sensor, product level, acquisition date, processing level, plus a free-form attribute map for provider-specific fields) lives on the `CollectionRecord`. It is normalized remote-sensing semantics, not raw provider blobs; the existing `SatelliteProducts::ProductInfo` is mapped into it by the discovery service.
- **Discovery is a read-only probe behind a new service.** A `CollectionImportService` (in `sicnu_processing`, composed over the DataManager) offers `probe(source) → ImportPreview` that calls the existing `SatelliteProducts::discover*` and GDAL subdataset enumeration **without catalog mutation**, returning a normalized preview tree: candidate collection metadata + a list of child candidate assets (each with its source path, kind, grid/resolution, band info, and a human-readable name). The probe is pure: running it twice, or running it and cancelling, changes nothing.
- **Atomic commit.** `commitImport(preview, userSelection)` registers the collection and the selected children in one transactional step: it either registers the collection + all chosen children (emitting one `collectionAdded` then one `assetAdded` per child), or, on any failure, registers nothing. A failure mid-batch rolls back the partial registrations. This is the spec's "atomic catalog commit."
- **Grids are not flattened.** The probe emits one child candidate per distinct grid; the commit registers each as an independent child asset. The discovery service is responsible for not merging incompatible grids (it uses the existing subdataset/resolution metadata to split them). No resampling happens at registration.
- **Children are full assets.** A child asset is registered through the normal `registerSource` path (with the parent collection id attached); it carries its own identity, revision, capabilities, leases, and lifecycle. Promoting, reaping, or unloading a child works exactly as for a standalone asset. Unloading a collection offers (via unload planning, reusing the existing `UnloadPlan` cascade concept) to cascade to its children.
- **Serialization.** The project serializer gains a `<collections>` element alongside `<assets>`, each `<collection>` carrying its id, name, product metadata, and child asset ids. Child assets' records carry their parent collection id. Round-trip preserves grouping and metadata.
- **Provider inputs, not the model.** `SatelliteProducts::ProductInfo`, `BandFile`, and GDAL subdataset strings are consumed by `CollectionImportService::probe` and mapped to the normalized `ProductMetadata` + child-candidate shape. They are not exposed through the DataManager interface.

### Decision-rich shape (from the design, not a working demo)

```text
CollectionId                // UUID, parallel to AssetId
ProductMetadata { platform; sensor; productLevel; acquisitionDate; processingLevel;
                  QMap<QString,QString> attributes; }
CollectionRecord { CollectionId id; QString displayName; ProductMetadata metadata;
                   QVector<AssetId> childAssetIds; }

// Discovery (read-only, no catalog mutation)
ChildCandidate { SourceDescriptor source; AssetKind kind; QString displayName;
                 QString gridLabel; QVector<BandInfo> bands; }
ImportPreview { ProductMetadata metadata; QString collectionDisplayName;
                QVector<ChildCandidate> children; }
Result<ImportPreview> CollectionImportService::probe( const SourceDescriptor &source );

// Atomic commit (all-or-nothing)
struct CommitImportRequest { ImportPreview preview; QVector<int> selectedChildIndices;
                             PersistencePolicy persistence; };
struct CommitImportResult { CollectionId collectionId; QVector<AssetId> childAssetIds;
                            QVector<Diagnostic> diagnostics; };
CommitImportResult CollectionImportService::commit( const CommitImportRequest &request );
```

## Testing Decisions

- **The seam is the DataManager (collections) + CollectionImportService (probe/commit).** Collection registration, querying, serialization, and lifecycle are tested through the DataManager interface, mirroring the existing asset tests (`test_data_manager.cpp`). Probe and commit are tested through the `CollectionImportService` interface against real product fixtures (staged Sentinel-style and Landsat-style directories, and an HDF/NetCDF fixture if available).
- **External behavior only:** probe does not mutate the catalog (assert assets/collections empty after probe); commit is atomic (a forced failure mid-commit leaves no collection and no children); children are independent assets (lease/promote/reap a child); grids are not flattened (a two-grid preview commits two child assets); round-trip preserves grouping + metadata.
- **Discovery-mapping is tested** with a fake/stub discoverer for hermetic unit tests (the real `SatelliteProducts::discover*` is an integration test against real fixtures), so the probe→preview→commit shape is tested without depending on real satellite data being present.
- **Prior art:** `test_data_manager.cpp` (register/query/lease/unload), `test_data_project_roundtrip.cpp` (serialization round-trip — collections must round-trip like assets), `test_output_committer.cpp` (atomic publish — the import commit mirrors its all-or-nothing posture).

## Out of Scope

- **Recursive/nested collections** (a collection containing a collection). The spec's DataNode tree allows it, but this wave uses a flat parent pointer (one level). Nesting is deferred until a real caller needs it (e.g. a Sentinel-2 product containing per-grid sub-collections).
- **Virtual Raster Assets** (Deferred #4). A cross-child band composition is a separate wave; this spec only registers the children, it does not compose them.
- **Normalized spectral metadata** (Deferred #7). Per-band wavelength/role/units normalization is a later wave; the `BandInfo` carried by a child candidate is the raw provider value, not the normalized spectral model.
- **Re-import / change detection** across sessions. Recognizing that a re-imported product is the same scene as an existing collection is deferred.
- **Streaming / partial-read of containers** (reading one HDF subdataset without opening the whole file) is a provider performance concern, out of scope.
- **The existing stack-to-GeoTIFF import operators** (`rs:landsat_import` etc.) are not removed in this wave; they continue to work. Migrating the import dialogs to the new collection-import path is the final slice, after the data model and discovery service are stable.

## Further Notes

- This wave makes the parent spec's lines 306-322 ("Complex Products and Containers" → Probe/Preview/Select/Atomic-commit) and 74-88 (DataNode/DataCollection model) actually true. It is the first introduction of a non-asset catalog node, so the data-modeling decisions here (flat parent pointer vs. tree, collection lifecycle vs. asset lifecycle) set the precedent for later waves.
- The discovery service deliberately lives in `sicnu_processing` (composed over the DataManager), not in `src/data`, because discovery depends on `SatelliteProducts` and GDAL subdataset logic that already lives in the processing layer; the DataManager stays discovery-agnostic and owns only the collection/child catalog.
- Wave ordering within this spec: (1) Data Collection catalog node + query + serialize + lifecycle, (2) the discovery probe (read-only, with stub + real discoverer), (3) the atomic commit transaction, (4) migrate one satellite-import dialog to the new probe→preview→commit path. Each is a small commit; (2)/(3) are testable with a stub discoverer before (4) touches the UI.
