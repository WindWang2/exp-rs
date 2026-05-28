# Processing Toolbox (C++ Native Algorithms) Design

## Overview

Wire up the already-compiled QGIS processing framework into the SICNU GEO RS application, providing a Processing Toolbox dock widget with C++ native algorithms for vector geometry, vector overlay/selection, raster analysis, and coordinate/projection operations.

## Current State

- QGIS processing core (`QgsProcessingRegistry`, `QgsProcessingAlgorithm`, `QgsProcessingProvider`, all parameter types) is compiled into `qgis_gui`
- QGIS processing GUI (`QgsProcessingToolboxModel`, `QgsProcessingToolboxTreeView`, `QgsProcessingAlgorithmDialogBase`) is compiled into `qgis_gui`
- **None of these are wired into the application** — no Processing menu, no toolbox dock widget, no algorithm providers registered
- No `QgsNativeAlgorithms` or any custom algorithm provider exists in the project
- The application is pure C++17, no Python at runtime

## Architecture

### 1. Custom Native Algorithms Provider

Create `SicnuNativeAlgorithms : public QgsProcessingProvider` that registers a curated set of C++ algorithms. Each algorithm inherits `QgsProcessingAlgorithm` and implements `initAlgorithm()`, `processAlgorithm()`, and `createInstance()`.

**Why not port QgsNativeAlgorithms:** The original QGIS `QgsNativeAlgorithms` contains 200+ algorithms with deep dependencies on helper classes scattered across the codebase. Porting it would be massive effort with diminishing returns. A custom provider with targeted algorithms is cleaner and more maintainable.

### 2. Algorithm Categories and Implementations

#### Vector Geometry (5 algorithms)

| Algorithm ID | Display Name | Core API | Description |
|-------------|--------------|----------|-------------|
| `buffer` | Buffer | `QgsGeometry::buffer()` | Create buffer polygons around features |
| `centroid` | Centroid | `QgsGeometry::centroid()` | Compute centroids of geometries |
| `convexhull` | Convex Hull | `QgsGeometry::convexHull()` | Compute convex hull of geometries |
| `dissolve` | Dissolve | `QgsGeometry::combine()` | Merge geometries into single geometry |
| `simplify` | Simplify | `QgsGeometry::simplify()` | Simplify geometries (Douglas-Peucker) |

#### Vector Overlay/Selection (5 algorithms)

| Algorithm ID | Display Name | Core API | Description |
|-------------|--------------|----------|-------------|
| `clip` | Clip | `QgsGeometry::intersection()` | Clip features by overlay polygon |
| `intersection` | Intersection | `QgsGeometry::intersection()` | Compute geometric intersection |
| `union` | Union | `QgsGeometry::combine()` | Compute geometric union |
| `difference` | Difference | `QgsGeometry::difference()` | Compute geometric difference |
| `extractbyattribute` | Extract by Attribute | `QgsFeatureRequest` + expression | Filter features by attribute value |

#### Raster Analysis (3 algorithms)

| Algorithm ID | Display Name | Core API | Description |
|-------------|--------------|----------|-------------|
| `cliprasterbyextent` | Clip Raster by Extent | `QgsRasterFileWriter` | Clip raster to map extent or polygon |
| `rasterlayerstatistics` | Raster Layer Statistics | `QgsRasterDataProvider::bandStatistics()` | Compute min/max/mean/stddev |
| `hillshade` | Hillshade | `QgsRasterAnalysis` | Generate hillshade from DEM |

#### Coordinate/Projection (2 algorithms)

| Algorithm ID | Display Name | Core API | Description |
|-------------|--------------|----------|-------------|
| `reprojectlayer` | Reproject Layer | `QgsCoordinateTransform` | Reproject vector layer to target CRS |
| `assignprojection` | Assign Projection | `QgsCoordinateTransform` | Assign CRS without reprojecting |

### 3. UI Integration

#### Processing Dock Widget

- New `QDockWidget` titled "Processing Toolbox" in the left dock area (tabified with Layers, Browser)
- Contains `QgsProcessingToolboxTreeView` showing registered providers and their algorithms
- Built-in search/filter via `QgsProcessingToolboxProxyModel`
- Double-click on algorithm → open execution dialog
- Right-click context menu: Execute, Add to Favorites, Remove from Favorites

#### Processing Menu

New top-level menu "Processing" with items:
- **Toolbox** — focus/raise the Processing Toolbox dock widget
- **History** — show processing history dialog
- **Model Designer** — (future, placeholder or disabled)

#### Algorithm Execution Dialog

- Use `QgsProcessingAlgorithmDialogBase` (already compiled)
- Supports: parameter input widgets (auto-generated from algorithm definition), progress bar, log output, background execution
- Connection: `QgsProcessingToolboxTreeView::activateAlgorithm()` → create dialog → `exec()`

#### Result Handling

- Vector outputs: auto-add to layer tree via `QgsProject::instance()->addMapLayer()`
- Raster outputs: auto-add to layer tree
- Use `QgsProcessingContext::takeResultLayers()` to retrieve result layers from execution

### 4. Integration in main.cpp

```
main()
├── QgsApplication init
├── QgsApplication::processingRegistry()->addProvider(new SicnuNativeAlgorithms())
├── QgisDesktopWindow constructor
│   ├── setupMapCanvas()
│   ├── setupDockWidgets()
│   │   ├── Layers dock (existing)
│   │   ├── Browser dock (existing)
│   │   ├── Processing Toolbox dock (NEW)
│   │   └── Overview dock (existing)
│   ├── setupMenus()
│   │   └── Processing menu (NEW)
│   └── connect toolbox signals
└── app.exec()
```

## Changes

### New Files

1. **`src/processing/sicnunativealgorithms.h`** — Provider class header
2. **`src/processing/sicnunativealgorithms.cpp`** — Provider implementation (registers all algorithms)
3. **`src/processing/algorithms/qgsbufferalgorithm.h/.cpp`** — Buffer algorithm
4. **`src/processing/algorithms/qgscentroidsalgorithm.h/.cpp`** — Centroid algorithm
5. **`src/processing/algorithms/qgsconvexhullalgorithm.h/.cpp`** — Convex Hull algorithm
6. **`src/processing/algorithms/qgsdissolvealgorithm.h/.cpp`** — Dissolve algorithm
7. **`src/processing/algorithms/qgssimplifyalgorithm.h/.cpp`** — Simplify algorithm
8. **`src/processing/algorithms/qgsclipalgorithm.h/.cpp`** — Clip algorithm
9. **`src/processing/algorithms/qgsintersectionalgorithm.h/.cpp`** — Intersection algorithm
10. **`src/processing/algorithms/qgsunionalgorithm.h/.cpp`** — Union algorithm
11. **`src/processing/algorithms/qgsdifferencealgorithm.h/.cpp`** — Difference algorithm
12. **`src/processing/algorithms/qgsextractbyattributealgorithm.h/.cpp`** — Extract by Attribute
13. **`src/processing/algorithms/qgscliprasterbyextentalgorithm.h/.cpp`** — Clip Raster by Extent
14. **`src/processing/algorithms/qgsrasterlayerstatisticsalgorithm.h/.cpp`** — Raster Layer Statistics
15. **`src/processing/algorithms/qgshillshadealgorithm.h/.cpp`** — Hillshade
16. **`src/processing/algorithms/qgsreprojectlayeralgorithm.h/.cpp`** — Reproject Layer
17. **`src/processing/algorithms/qgsassignprojectionalgorithm.h/.cpp`** — Assign Projection

### Modified Files

1. **`main.cpp`**:
   - Add `#include <processing/sicnunativealgorithms.h>`
   - Add `#include <processing/qgsprocessingtoolboxtreeview.h>`
   - Register provider in `main()`: `QgsApplication::processingRegistry()->addProvider(new SicnuNativeAlgorithms())`
   - Add `setupProcessingDock()` method to `QgisDesktopWindow`
   - Add Processing menu
   - Connect toolbox double-click to algorithm execution dialog

2. **`CMakeLists.txt`** (root — `/home/kevin/projects/exp-rs/CMakeLists.txt`, which builds `sicnu_geo_rs`):
   - Add new source files to the `sicnu_geo_rs` target

## Phase 2 (Separate Spec — Python Plugin Support)

Not included in this spec. Will be designed separately:
- Embed CPython runtime (`Py_Initialize`)
- Python ↔ C++ bridge (pybind11 or SIP)
- Python console UI (`QgsCodeEditorPython` + custom REPL)
- Python plugin loader and registry
- Python processing algorithm provider

## Verification

1. Build succeeds with no errors
2. Launch app → "Processing Toolbox" dock visible in left panel
3. Toolbox tree shows "SICNU Native" provider with 4 algorithm groups
4. Search filter works (type "buffer" → shows Buffer algorithm)
5. Double-click "Buffer" → algorithm dialog opens with correct parameters
6. Execute Buffer on a vector layer → output layer added to tree
7. Processing menu items work (Toolbox, History)
