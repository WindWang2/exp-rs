# Data Manager Phase 1 Implementation Plan

**Goal:** Deliver the first verified data/display separation loop for local GDAL raster and OGR vector sources, the main map view, and QGIS project round trips.

**Architecture:** A new `sicnu_data` library owns project-scoped Data Asset identity, state, revision, leases, and source-provider adapters. A testable QGIS Display Manager owns Display Views and independent `QgsMapLayer`-backed Display Layers. `LayerManager` becomes a compatibility facade while callers migrate. QGIS project XML remains the persistence container.

**Tech stack:** C++20, Qt6 Core/Xml, QGIS Core/GUI, GDAL/OGR through QGIS providers, Catch2, CMake.

**Specification:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md`

**Architecture decision:** `docs/adr/0009-separate-data-assets-from-display-layers.md`

## Global Constraints

- Use test-driven development for each behavior-bearing task.
- `src/data` must not link Qt Widgets or `qgis_gui`.
- Data Manager headers must not expose `QWidget`, `QgsMapCanvas`, `QgsLayerTree`, `QgsMapLayer`, `QgsDataProvider`, or `GDALDataset`.
- Data Manager catalog mutations occur on its owning thread.
- A QGIS Display Layer owns a distinct `QgsMapLayer`; no canonical shared map-layer object is stored in Data Manager.
- Unload and physical source deletion remain separate. Phase 1 implements unload only.
- Preserve unrelated dirty-worktree changes. Do not combine this migration with current histogram/stretch work.
- Keep `.qgs/.qgz` readable by standard QGIS.
- Each task below is one small commit unless a failing test and its minimal implementation are deliberately kept together.

## File Map

| Path | Action | Responsibility |
|---|---|---|
| `src/data/CMakeLists.txt` | Create | `sicnu_data` library without GUI dependencies |
| `src/data/asset_types.h` | Create | IDs, revision, state, capability, persistence values |
| `src/data/source_descriptor.h` | Create | Non-secret canonical source description |
| `src/data/data_asset.h` | Create | Immutable asset snapshots and queries |
| `src/data/data_result.h` | Create | Structured result and diagnostic values |
| `src/data/data_manager.h/.cpp` | Create | Deep project asset module |
| `src/data/internal/source_provider.h` | Create | Internal provider seam |
| `src/data/internal/source_provider_registry.h/.cpp` | Create | Probe/resolve adapter selection |
| `src/data/providers/gdal_raster_source_provider.h/.cpp` | Create | Local raster descriptor and metadata adapter |
| `src/data/providers/ogr_vector_source_provider.h/.cpp` | Create | Local vector descriptor and metadata adapter |
| `src/app/display/qgis_display_manager.h/.cpp` | Create | Main QGIS Display View and Display Layer ownership |
| `src/app/project_context.h/.cpp` | Create | Project-scoped composition root |
| `src/app/data_project_serializer.h/.cpp` | Create | QGIS XML extension and standard-layer adoption |
| `src/app/panels/data_manager_panel.h/.cpp` | Create | Asset catalog UI, separate from layer tree |
| `src/app/layer_manager.*` | Modify | Temporary facade over ProjectContext/Display Manager |
| `src/app/main_window*` | Modify | Construct context, install panel, route project lifecycle |
| `CMakeLists.txt` | Modify | Add `src/data` after QGIS core |
| `src/app/CMakeLists.txt` | Modify | Build/link display, context, serializer, and panel |
| `tests/test_data_manager.cpp` | Create | Deep Data Manager interface tests |
| `tests/test_data_source_providers.cpp` | Create | GDAL/OGR adapter tests |
| `tests/test_qgis_display_manager.cpp` | Create | Independent Display Layer tests |
| `tests/test_data_project_roundtrip.cpp` | Create | QGIS persistence and adoption tests |
| `tests/test_data_manager_panel.cpp` | Create | Minimal panel behavior tests |
| `tests/CMakeLists.txt` | Modify | Register the new tests |

## Task 1 — Scaffold `sicnu_data` and Domain Values

**Files:**

- Create `src/data/CMakeLists.txt`
- Create `src/data/asset_types.h`
- Create `src/data/source_descriptor.h`
- Create `src/data/data_result.h`
- Create `tests/test_data_manager.cpp`
- Modify root `CMakeLists.txt`
- Modify `tests/CMakeLists.txt`

- [x] Write failing compile-and-value tests for:
  - `AssetId` generation, equality, and string round trip;
  - nonzero `AssetRevision`;
  - `AssetState`, `AssetCapability`, `PersistencePolicy`, and `StorageKind`;
  - `SourceDescriptor` equality that excludes display state and secrets.
- [x] Add `sicnu_data` as a static library linked to Qt6 Core only; provider tasks may add `qgis_core` later, while Qt Widgets and `qgis_gui` remain forbidden.
- [x] Implement the smallest immutable value types needed by the tests.
- [x] Add a CMake architecture assertion that `sicnu_data` has no direct GUI link dependency.
- [x] Build and run `test_data_manager`.

**Verification:**

```bash
cmake --build build --target test_data_manager -j2
QT_QPA_PLATFORM=offscreen ./build/tests/test_data_manager
```

**Commit:** `feat(data): add asset identity and source descriptor values`

## Task 2 — Deep Registration and Deduplication Interface

**Files:**

- Create `src/data/data_asset.h`
- Create `src/data/data_manager.h/.cpp`
- Create `src/data/internal/source_provider.h`
- Create `src/data/internal/source_provider_registry.h/.cpp`
- Modify `tests/test_data_manager.cpp`

- [x] Write an in-memory source-provider adapter inside the test.
- [x] Write failing interface tests for:
  - registering a source returns an Asset ID;
  - registering the same SourceKey returns the same ID with `reusedExisting=true`;
  - display-only values cannot affect SourceKey;
  - asset lookup returns immutable snapshots;
  - list/query behavior distinguishes kind, state, and persistence;
  - provider-resolution errors are returned as diagnostics without UI side effects.
- [x] Implement provider registration as an internal seam.
- [x] Implement `DataManager::registerSource()`, `asset()`, and `assets()`.
- [x] Emit `assetAdded` exactly once for a newly registered SourceKey.
- [x] Keep provider registry and mutable records private to Data Manager implementation.

**Verification:** Run `test_data_manager`.

**Commit:** `feat(data): register and deduplicate project data assets`

## Task 3 — Asset Lease and Unload Planning

**Files:**

- Modify `src/data/asset_types.h`
- Modify `src/data/data_manager.h/.cpp`
- Modify `tests/test_data_manager.cpp`

- [x] Write failing tests for:
  - acquiring a View, Task, or Edit lease;
  - RAII lease release;
  - unload planning reports every active lease;
  - normal unload rejects referenced assets;
  - a confirmed cascade plan removes the asset exactly once;
  - stale unload plans are rejected after the catalog changes;
  - ordinary unload has no source-deletion side effect.
- [x] Implement move-only `AssetLease`.
- [x] Implement `planUnload()` as a read-only impact snapshot with a catalog generation.
- [x] Implement `unload(confirmedPlan)` as an atomic catalog mutation.
- [x] Add `assetAboutToUnload` and `assetRemoved` observable events.

**Verification:** Run `test_data_manager`, including sanitizer configuration if available.

**Commit:** `feat(data): protect assets with leases and planned unload`

## Task 4 — Local Raster and Vector Source Providers

**Files:**

- Create `src/data/providers/gdal_raster_source_provider.h/.cpp`
- Create `src/data/providers/ogr_vector_source_provider.h/.cpp`
- Create `tests/test_data_source_providers.cpp`
- Modify `src/data/CMakeLists.txt`
- Modify `tests/CMakeLists.txt`

- [x] Write failing tests using repository fixtures for:
  - GeoTIFF structural metadata and capabilities;
  - memory or fixture OGR vector structural metadata and capabilities;
  - canonical path SourceKey generation;
  - same source through normalized path spelling deduplicates;
  - ENVI `.hdr` and its paired binary resolve to the same SourceKey;
  - missing source registers or resolves to Missing rather than disappearing;
  - no provider result contains renderer state or credentials.
- [x] Move ENVI path-pair knowledge from `LayerManager` into the GDAL provider implementation.
- [x] Resolve only structural metadata in this task; do not calculate histograms or exact statistics.
- [x] Declare capabilities explicitly per provider.
- [x] Ensure providers return immutable display/processing source descriptions rather than open handles.

**Verification:**

```bash
cmake --build build --target test_data_source_providers -j2
QT_QPA_PLATFORM=offscreen ./build/tests/test_data_source_providers
```

**Commit:** `feat(data): resolve local raster and vector assets`

## Task 5 — Main QGIS Display Manager

**Files:**

- Create `src/app/display/qgis_display_manager.h/.cpp`
- Create `tests/test_qgis_display_manager.cpp`
- Modify `src/app/CMakeLists.txt`
- Modify `tests/CMakeLists.txt`
- Modify `src/gui/CMakeLists.txt` *(disable target-level AUTOUIC for the vendored
  QGIS GUI target, which already uses its own generated `ui` target)*

- [x] Write failing tests for:
  - creating the main Display View over a canvas/tree/store fixture;
  - adding one asset creates one Display Layer and one `QgsMapLayer`;
  - adding the same asset again creates a distinct `QgsMapLayer`;
  - each Display Layer holds an Asset Lease;
  - renderer mutation in one raster layer does not mutate the other;
  - removing a Display Layer releases its lease but leaves the Data Asset;
  - a Display Layer belongs to exactly one Display View;
  - invalid or Missing assets produce structured display diagnostics.
- [x] Implement a small Display Manager interface: create/register view, add layer, clone layer, remove layer, and snapshot.
- [x] Let the QGIS adapter own the runtime `QgsMapLayer` display state.
- [x] Store `AssetId`, `DisplayLayerId`, and `DisplayViewId` as QGIS custom properties.
- [x] Keep canvas, tree, and QGIS layer ownership entirely outside `sicnu_data`.

**Verification:** `QT_QPA_PLATFORM=offscreen ./build/tests/test_qgis_display_manager`
passes 95 assertions in 7 test cases.

**Commit:** `feat(display): create independent qgis display layers from assets`

## Task 6 — Project Context and LayerManager Compatibility Facade

**Files:**

- Create `src/app/project_context.h/.cpp`
- Modify `src/app/layer_manager.h/.cpp`
- Modify `src/app/main_window.h/.cpp`
- Modify `src/app/main_window_docks.cpp`
- Modify `src/app/main_window_project.cpp`
- Modify `src/app/CMakeLists.txt`
- Add or modify a focused integration test

- [x] Write a failing integration test that `loadRasterLayer(path)` registers one asset and adds one main-view Display Layer through a single seam.
- [x] Construct one Project Context per main window/project.
- [x] Inject its Data Manager and Display Manager into the temporary LayerManager facade.
- [x] Change `loadRasterLayer()` and `loadVectorLayer()` to:
  1. register or reuse an asset;
  2. add a Display Layer to the main view;
  3. report structured errors through the UI shell.
- [x] Change selected-layer removal to remove Display Layers only.
- [x] Retain active-layer, properties-dialog, and tree helper behavior in the facade until later callers migrate.
- [x] Ensure `newProject()` clears the Project Context through an explicit project operation rather than treating `QgsProject::clear()` as sufficient data cleanup.

**Verification:** `QT_QPA_PLATFORM=offscreen ./build/tests/test_layer_manager_data_context`
passes 36 assertions in 5 test cases. Existing `test_layers` and
`test_layer_tree_bridge` each pass 11 assertions in 3 test cases, and the
`sicnu_geo_rs` application target builds successfully.

**Commit:** `refactor(app): route main layer loading through project data context`

## Task 7 — QGIS Project Round Trip and Standard-Layer Adoption

**Files:**

- Create `src/app/data_project_serializer.h/.cpp`
- Create `tests/test_data_project_roundtrip.cpp`
- Modify `src/data/asset_types.h`
- Modify `src/data/data_asset.h`
- Modify `src/data/data_manager.h/.cpp` *(restore persisted Asset IDs through
  the Data Manager seam)*
- Modify `src/app/display/qgis_display_manager.h/.cpp` *(adopt existing QGIS
  presentation objects without recreating renderer/tree state)*
- Modify `src/app/main_window_connections.cpp`
- Modify `src/app/main_window_project.cpp`
- Modify `src/app/CMakeLists.txt`
- Modify `tests/CMakeLists.txt`

- [x] Write failing round-trip tests for:
  - stable Asset IDs after save/reopen;
  - Display Layer ID and Asset ID mapping;
  - independent renderer states survive;
  - layer order and group placement survive;
  - Data Manager extension XML coexists with standard QGIS map-layer XML;
  - reopening a standard QGIS project adopts supported layers;
  - two same-source QGIS layers adopt one Data Asset and two Display Layers;
  - unsupported provider layers remain External Display Layers.
- [x] Define one versioned SICNU extension XML root.
- [x] Serialize Data Asset descriptions without live handles, caches, or credentials.
- [x] Store QGIS identity custom properties for main-view interoperability.
- [x] On read, restore extension assets before reconciling QGIS layers.
- [x] Add an adoption guard so Display Manager-created layers do not re-enter as new external additions.
- [x] Preserve QGIS renderer and tree state as the QGIS adapter's authoritative presentation state.

**Verification:** `QT_QPA_PLATFORM=offscreen ./build/tests/test_data_project_roundtrip`
passes 80 assertions in 3 test cases. Existing layer-tree and classification
project tests pass, and the `sicnu_geo_rs` application target builds
successfully.

**Commit:** `feat(project): persist and adopt data asset relationships`

## Task 8 — Missing State and Relocation

**Files:**

- Modify `src/data/data_manager.h/.cpp`
- Modify local source providers
- Modify `src/app/data_project_serializer.cpp`
- Modify `src/app/display/qgis_display_manager.cpp`
- Modify tests from Tasks 2, 4, 5, and 7

- [x] Write failing tests for:
  - reopening after moving a source preserves Asset ID and Display Layer record;
  - missing source produces Missing state;
  - relocation validates kind/structure before mutation;
  - relocation preserves Asset ID, advances revision, and emits one change event;
  - relocation to an incompatible source is rejected;
  - renderer state is restored after the replacement layer materializes.
- [x] Implement `relocate()` as a validated Data Manager transaction.
- [x] Recompute SourceKey indexes without creating a second Asset.
- [x] Let Display Manager recreate the QGIS layer while retaining its display identity and serialized presentation state.

**Verification:** the four focused test targets pass —
`test_data_manager` (169 assertions / 23 cases),
`test_data_source_providers` (89 / 10),
`test_qgis_display_manager` (126 / 9), and
`test_data_project_roundtrip` (102 / 4), all offscreen. The `sicnu_geo_rs`
application target builds successfully.

**Commit:** `feat(data): preserve missing assets and support relocation`

## Task 9 — Separate Data Manager Panel

**Files:**

- Create `src/app/panels/data_manager_panel.h/.cpp`
- Create `tests/test_data_manager_panel.cpp`
- Modify `src/app/main_window_docks.cpp`
- Modify `src/app/CMakeLists.txt`
- Modify `tests/CMakeLists.txt`

- [x] Write minimal offscreen tests for:
  - one row per Data Asset, not per Display Layer;
  - status and temporary/persistent indicators;
  - double-click emits a request to display an Asset ID;
  - remove action invokes unload planning rather than layer removal;
  - panel selection does not change renderer state;
  - reference count reflects Display Layer leases.
- [x] Implement the panel as a projection of immutable Data Manager snapshots.
- [x] Keep all dialogs and user confirmation in the UI shell.
- [x] Wire double-click/drag intent to Display Manager, not Data Manager.
- [x] Keep the existing layer tree visible and semantically separate.

**Verification:** `QT_QPA_PLATFORM=offscreen ./build/tests/test_data_manager_panel`
passes 37 assertions in 6 test cases, and the `sicnu_geo_rs` application target
builds successfully. Light/dark theme behavior to be inspected manually.

**Commit:** `feat(ui): add project data manager panel`

## Task 10 — Vector Edit Lease

**Files:**

- Modify `src/data/asset_types.h`
- Modify `src/data/data_manager.h/.cpp`
- Modify `src/app/main_window_vector.cpp`
- Modify `src/app/display/qgis_display_manager.cpp`
- Extend `tests/test_data_manager.cpp`
- Add or extend vector editing integration tests

- [x] Write failing tests that only one Edit Lease may exist per Vector Asset.
- [x] Write a UI integration test that the non-owner Display Layer remains read-only.
- [x] Require commit or rollback before transferring edit ownership.
- [x] On successful commit, advance Asset Revision and refresh other Display Layers.
- [x] On rollback, release Edit Lease without advancing revision.
- [x] Keep existing QGIS edit buffer and undo behavior inside the edit-owning Display Layer.

The Edit Lease is wired at the main `toggleEditing`/`saveEdits` path
(`main_window_vector.cpp`). The attribute-table facade (`QgisApp`) and the
feature-form path (`QgsGuiVectorLayerTools`) are deferred bypass entry points;
they do not yet acquire the Edit Lease (recorded as follow-up work).

**Verification:** `test_data_manager` (200 assertions / 27 cases),
`test_qgis_display_manager` (143 / 10), `test_layer_manager_data_context`
(36 / 5), and `test_data_project_roundtrip` (102 / 4) all pass offscreen; the
`sicnu_geo_rs` application target builds successfully.

**Commit:** `feat(vector): enforce one edit lease per data asset`

## Task 11 — Route Remaining Phase-1 Entry Points and Add Adoption Safety Net

**Files:**

- Modify relevant main-window docks, startup loading, processing auto-load, STAC, and dialog paths
- Modify `src/app/main_window_connections.cpp`
- Add architecture/integration tests

- [x] Inventory every remaining application `QgsProject::addMapLayer()` call.
- [x] Route Phase-1 local raster/vector entry points through Project Context.
- [x] Listen for unavoidable external `layersAdded` events and adopt supported layers.
- [x] Prevent recursive adoption of Display Manager-created layers.
- [x] Change Task Center's UI auto-load receiver to register output then optionally display it; do not change Task Center execution semantics in this phase.
- [x] Add a test that a legacy direct QGIS layer is adopted and receives Asset identity.
- [x] Document deferred complex paths rather than hiding them behind untested special cases.

Explicitly migrated (now through Project Context): startup sample load
(`main.cpp`), new-vector-layer creation (`main_window_vector.cpp`), and the
processing auto-load first choice (`sicnu_algorithm_dialog.cpp`, already via
`mainWin->loadRasterLayer`/`loadVectorLayer`). `ProjectContext` installs a
`layersAdded` adoption safety net that registers + adopts local GDAL/OGR layers
entering the project outside the seam, is non-recursive for Display
Manager-created layers, and skips remote sources.

**Verification:** `rg -n "QgsProject::instance\(\)->addMapLayer|project->addMapLayer" src/app`
leaves six matches, all accounted for: OBIA classification output (×2,
`rs_obia_main_window.cpp`), georeferencer warp output (×1,
`qgsgeoref_shell_window.cpp`), and the processing auto-load fallback (×2,
`sicnu_algorithm_dialog.cpp`) — all local GDAL rasters adopted by the safety
net — plus the STAC `/vsicurl/` remote COG (×1, `stac_browser_dialog.cpp`)
deferred to the Wave 5 remote-map providers. The georeferencer session
`mLayerStore->addMapLayer` calls are session-workspace stores, not the main
project, and are deferred. `test_layer_adoption_safety_net` passes 21
assertions in 3 test cases; `test_data_manager` (200/27),
`test_qgis_display_manager` (143/10), `test_layer_manager_data_context` (36/5),
`test_data_project_roundtrip` (102/4), `test_layers` (11/3), and
`test_layer_tree_bridge` (11/3) all pass offscreen; `sicnu_geo_rs` builds.

**Commit:** `refactor(app): adopt legacy qgis layers into data manager`

## Task 12 — First-Deliverable Acceptance and Cleanup

**Files:**

- Extend focused tests as needed
- Update `CLAUDE.md` and `docs/repo-layout.md`
- Update historical display specification with a supersession note
- Remove dead compatibility code only when no callers remain

- [x] Add an end-to-end offscreen test covering one asset, two independently styled layers, one layer removal, unload rejection, project round trip, missing source, and relocation.
- [x] Run existing layer, vector editing, project, display stretch, classification workspace, georeferencing workspace, and Task Center tests.
- [x] Verify standard QGIS can parse the saved main-view layer definitions.
- [x] Record that ADR-0009 supersedes the old display specification's data-entity framing while retaining renderer-pipeline guidance.
- [x] Document `src/data`, Project Context, and Display Manager in the repository layout.
- [x] Confirm the eight specification acceptance behaviors.
- [x] Record remaining direct layer ownership paths as follow-up work with exact files.

**Verification:** `test_data_phase1_acceptance` passes 52 assertions in 1
end-to-end case. The eight specification acceptance behaviors are each covered:

1. One raster opened twice → one Data Asset + two independently styled Display
   Layers (acceptance test step 1; `test_qgis_display_manager`).
2. Removing one Display Layer leaves the Data Asset registered (step 2;
   `test_layer_manager_data_context`).
3. Unload of a referenced asset is rejected until cascade is confirmed (step 3;
   `test_data_manager`).
4. Raster and vector share Data Asset identity; vector editing allows one Edit
   Lease (`test_data_manager` edit-lease cases; `test_qgis_display_manager`
   read-only integration).
5. Save/reopen preserves Asset IDs, display relationships, order, and style
   (step 4; `test_data_project_roundtrip`).
6. Standard QGIS layers are auto-adopted without losing style or tree
   placement (`test_data_project_roundtrip`; `test_layer_adoption_safety_net`).
7. Missing sources survive project load and recover through relocation (step 5;
   `test_data_project_roundtrip` missing case).
8. `sicnu_data` has no Widgets/canvas dependency (CMake link assertion in
   `src/data/CMakeLists.txt`) and new load paths route through the Project
   Context instead of treating `QgsProject::instance()` as the data authority
   (Tasks 6 and 11).

The saved `.qgs` keeps a well-formed standard `projectlayers` element alongside
the `sicnuDataManager` extension, so standard QGIS parses the main-view layer
definitions (acceptance test step 4). Existing suites pass offscreen:
`test_data_manager` (200/27), `test_data_source_providers` (89/10),
`test_qgis_display_manager` (143/10), `test_data_project_roundtrip` (102/4),
`test_data_manager_panel` (37/6), `test_layer_adoption_safety_net` (21/3),
`test_layer_manager_data_context` (36/5), `test_layers` (11/3),
`test_layer_tree_bridge` (11/3), `test_vector_properties` (15/2),
`test_display_stretch` (41/12), `test_classification_project` (29/3),
`test_task_center` (15/3), `test_georef_session_state` (16/3),
`test_classification_window`, `test_georef_window_rpc_mode`, and
`test_georef_task_list`.

Pre-existing failures unrelated to this migration (present before Phase 1):
`test_raster_ndvi` / `test_algorithm_schema` do not compile
(`algorithm_help_catalog.h` include path); `test_georef_window` /
`test_georef_dual_window` fail to link (their CMake omits
`job_engine_qt_bridge.cpp` / `dialog_utils.cpp`); `test_task_center_dock` hangs
offscreen; `test_vector_warper_NOT_BUILT` is an unbuilt ctest placeholder. These
belong to the processing/georef/Task-Center areas, not the data/display seam.

**Commit:** `test(data): close phase one data display separation`

### Remaining direct layer-ownership paths (follow-up, exact files)

- Attribute-table and feature-form edit entry points do not acquire the Edit
  Lease: `src/app/qgis_app_facade.cpp`, `src/app/qgsguivectorlayertools.cpp`,
  `src/app/qgsattributetabledialog.cpp`.
- Georeferencer session-workspace layer stores (`mLayerStore->addMapLayer`),
  not the main project: `src/app/georeferencer/qgsgeoreferencermainwindow.cpp`,
  `src/app/georeferencer/qgsgeoref_shell_window.cpp`.
- STAC `/vsicurl/` remote COG (Wave 5 remote-map providers):
  `src/app/dialogs/stac_browser_dialog.cpp`.
- OBIA and georeferencer raster outputs currently adopted via the safety net
  (could be made explicit later): `src/app/obia/rs_obia_main_window.cpp`,
  `src/app/georeferencer/qgsgeoref_shell_window.cpp`.
- `LayerManager` retains active-layer / properties-dialog / tree-helper facade
  behavior until remaining callers migrate:
  `src/app/layer_manager.h`, `src/app/layer_manager.cpp`.

## Deferred Follow-Up Order

After Phase 1 is stable:

1. Asset-ID processing inputs and transactional output registration.
2. SessionTemporary and TaskTemporary output policies.
3. Data Collections and complex-product import preview.
4. Virtual Raster Asset dependency DAG and spatial preflight.
5. WMS/WMTS/TMS/XYZ providers, Connection Catalog, and authentication binding.
6. Multiple Display Views and migration of Classification/Georeferencing session workspaces.
7. normalized spectral metadata, derived cache, and structured Derivation Records.

Do not start later waves by widening the Data Manager interface. Add behavior behind the established seam and expose new interface concepts only when a second real caller requires them.
