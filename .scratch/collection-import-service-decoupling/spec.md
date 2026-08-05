# Specification: Decoupling CollectionImportService for Zero-Boilerplate Headless Execution

## Problem Statement

Currently, `CollectionImportService` requires callers to instantiate and pass a raw `ProductDiscoverer*` pointer into its constructor. In GUI dialogs (such as `LandsatImportDialog`) and headless scripts, this forces callers to manage low-level discoverer pointer lifetime and boilerplate injection. Additionally, missing default discoverer handling prevents `CollectionImportService` from being constructed cleanly with just `DataManager`.

## Solution

Deepen `CollectionImportService` to automatically fallback to `SatelliteProductsDiscoverer` when no explicit discoverer is provided (or when `nullptr` is passed). Callers can instantiate `CollectionImportService(dataManager)` with zero boilerplate, while custom unit tests retain the ability to inject fake `ProductDiscoverer` stubs.

## User Stories

1. As a Developer, I want to instantiate `CollectionImportService(dataManager)` cleanly without manually allocating or passing a `ProductDiscoverer*`, so that dataset imports require zero boilerplate.
2. As a Headless CLI User, I want dataset collection imports to execute via `CollectionImportService` without GUI dependencies or explicit discoverer setup.
3. As a Desktop User, I want dataset import dialogs to probe and commit multi-band satellite collections (Landsat, Sentinel) seamlessly.

## Implementation Decisions

- **Optional Discoverer Parameter**: Update `CollectionImportService` constructor:
  ```cpp
  explicit CollectionImportService( sicnu::data::DataManager *dataManager,
                                     ProductDiscoverer *discoverer = nullptr,
                                     QObject *parent = nullptr );
  ```
- **Internal Default Discoverer**: When `discoverer == nullptr`, `CollectionImportService` initializes a `std::unique_ptr<ProductDiscoverer>` holding a `SatelliteProductsDiscoverer`.
- **GUI Dialog Simplification**: Simplify `LandsatImportDialog` to use the zero-boilerplate `CollectionImportService service( m_dataManager );` constructor.

## Testing Decisions

- **Seam**: Test `CollectionImportService` public API (`probe`, `commit`, `importCollection`).
- **Tests to build/update**:
  - `test_collection_import_service.cpp`: Add unit tests for default `CollectionImportService( dataManager )` constructor.
  - Verify probing and committing with default discoverer.

## Out of Scope

- Modifying underlying `SatelliteProducts::discoverProduct` parser logic.

## Further Notes

Aligned with ADR 0010 (Data/Display Seams) and ADR 0053 (Deep Module Consolidation).
