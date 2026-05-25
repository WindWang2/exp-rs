# Antigravity RS: Full C++ Rewrite Design

**Date**: 2026-05-25
**Status**: Approved
**Approach**: Fork QGIS C++ source subset, all core + GUI in C++, Python only for agent/analysis shell

## 1. Motivation

Current Python implementation has fundamental performance and stability problems:
- PROJ DatabaseContext SIGSEGV when rasterio.open() called from background threads on CRS-bearing files
- Python GIL bottleneck: ~1s render time for LE7 (7951x7171), UI freezes
- rasterio/pyproj are wrappers around C libraries; calling them through Python adds overhead and thread-safety issues that don't exist at the C level
- No path to QGIS-level performance without moving rendering to C++

QGIS solves all of these at the C++ level: per-thread PJ_CONTEXT, GDAL RasterIO with QRecursiveMutex, QtConcurrent parallel rendering, QgsRasterPipe cloning for thread isolation.

## 2. Approach: Fork QGIS Source Subset

Copy source files directly from `qgis_ref/src/` into our project. Preserve original class names, file names, and APIs. Exclude modules we don't need by not compiling them, not by deleting code.

## 3. Project Structure

```
exp-rs/
├── CMakeLists.txt
├── src/
│   ├── core/                         # From qgis_ref/src/core/
│   │   ├── raster/                   # 107 files
│   │   ├── maprenderer/              # 16 files
│   │   ├── geometry/                 # 88 files
│   │   ├── vector/                   # 51 files
│   │   ├── layertree/                # 28 files
│   │   ├── labeling/                 # 36 files
│   │   ├── symbology/                # 98 files
│   │   ├── proj/                     # 25 files
│   │   ├── providers/gdal/           # GDAL raster+vector provider
│   │   ├── providers/ogr/            # OGR vector provider
│   │   ├── providers/memory/         # Memory provider
│   │   ├── project/                  # QgsProject
│   │   ├── scalebar/                 # QgsScaleBarRenderer
│   │   ├── ogc/                      # OGC utilities
│   │   ├── processing/               # QgsProcessingFramework
│   │   ├── plugin/                   # Plugin layer registry
│   │   └── ... (select core .cpp/.h files)
│   ├── gui/                          # From qgis_ref/src/gui/
│   │   ├── qgsmapcanvas.cpp/h
│   │   ├── layertree/
│   │   ├── maptools/
│   │   ├── providers/gdal/
│   │   └── ... (select GUI widgets)
│   ├── app/                          # From qgis_ref/src/app/
│   │   └── qgisapp.cpp/h            # Main window
│   ├── server/                       # OGC Server
│   │   └── ... (from qgis_ref/src/server/)
│   └── python/                       # pybind11 bindings
│       └── bindings.cpp
├── agent/                            # PRESERVED: Python NLP executor
├── analysis/                         # PRESERVED: Python analysis algorithms
└── tests/
```

## 4. Modules to Fork (by Priority)

### P0 — Rendering Core (first build, solves PROJ crash + performance)

| Source | Classes | Purpose |
|--------|---------|---------|
| core/raster/*.cpp/h | QgsRasterBlock, QgsRasterDataProvider, QgsRasterPipe, QgsRasterProjector, QgsRasterResampleFilter, QgsRasterIterator, QgsRasterDrawer, QgsRasterInterface | Raster data pipeline: block API, reprojection, resampling, drawing |
| core/maprenderer/*.cpp/h | QgsMapRendererJob, QgsMapRendererParallelJob, QgsMapRendererQImageJob, QgsRenderContext, QgsMapSettings | Async parallel render dispatch |
| core/proj/*.cpp/h | QgsProjUtils, QgsCoordinateTransform, QgsCoordinateReferenceSystem | PROJ thread-safe wrapper (PJ_CONTEXT per-thread) |
| core/providers/gdal/*.cpp/h | QgsGdalProvider, QgsGdalProviderBase | GDAL RasterIO direct call with QRecursiveMutex |
| core/qgsrectangle.cpp/h, qgsmaptopixel.cpp/h, qgspointxy.cpp/h | Basic geometry | Render computation dependencies |

### P1 — Layer & Project (second build)

| Source | Classes | Purpose |
|--------|---------|---------|
| core/qgsmaplayer.cpp/h | QgsMapLayer | Layer base class |
| core/raster/qgsrasterlayer.cpp/h | QgsRasterLayer | Raster layer |
| core/vector/*.cpp/h | QgsVectorLayer, QgsVectorDataProvider, QgsFeature, QgsFeatureIterator | Vector layer |
| core/geometry/*.cpp/h | QgsGeometry, QgsPoint, QgsPolyline, QgsPolygon | Geometry engine |
| core/project/*.cpp/h | QgsProject | Project management |
| core/layertree/*.cpp/h | QgsLayerTree, QgsLayerTreeModel | Layer tree |
| core/qgsmaplayerstore.cpp/h | QgsMapLayerStore | Layer store |

### P2 — Symbology & Labeling (third build)

| Source | Classes | Purpose |
|--------|---------|---------|
| core/symbology/*.cpp/h | QgsSymbol, QgsLineSymbol, QgsFillSymbol, QgsRendererCategory | Vector symbology |
| core/labeling/*.cpp/h | QgsLabelingEngine, QgsPalLabeling | Label engine |
| core/qgsrendercontext.cpp/h | QgsRenderContext | Render context |
| core/scalebar/*.cpp/h | QgsScaleBarRenderer | Scale bar |

### P3 — GUI Components (fourth build)

| Source | Classes | Purpose |
|--------|---------|---------|
| gui/qgsmapcanvas.cpp/h | QgsMapCanvas | Map canvas |
| gui/layertree/*.cpp/h | QgsLayerTreeView | Layer tree view |
| gui/maptools/*.cpp/h | QgsMapToolPan, QgsMapToolZoom | Map interaction tools |

### P4 — OGC Server & Plugin System

| Source | Classes | Purpose |
|--------|---------|---------|
| src/server/*.cpp/h | QgsServer, QgsService | OGC WMS/WFS/WCS server |
| core/ogc/*.cpp/h | QgsOgcUtils | OGC utilities |
| core/qgspluginlayer.cpp/h, qgspluginlayerregistry.cpp/h | QgsPluginLayer, QgsPluginLayerRegistry | Plugin layer registration |
| python/qgisplugin* | Python plugin loader | Python plugin system |

## 5. Excluded Modules

These QGIS modules are NOT forked:
- 3D, pointcloud, mesh, vectortile, tiledscene
- Layout (print composer), annotations, diagrams
- Postgres/Oracle/GRASS/HANA/MSSQL database providers
- Temporal/elevation framework
- Expression parser (add later if needed)
- SIP/Python binding generation (we write pybind11 manually)
- GRASS/SAGA/OTB processing providers

## 6. Python Code Cleanup

### DELETE entirely (replaced by C++)

| Path | Reason |
|------|--------|
| core/ (entire directory, ~2400 lines) | All replaced by C++ core |
| gui/canvas.py | Replaced by C++ QgsMapCanvas |
| gui/qgsmapcanvas.py | Replaced by C++ QgsMapCanvas |
| gui/qgsmaprendererjob.py | Replaced by C++ QgsMapRendererParallelJob |
| gui/toolbox.py | Replaced by C++ QgsProcessingToolbox |
| gui/splash.py, gui/qgssplash.py | Replaced by C++ QgsSplashScreen |
| src/raster_ops.cpp | Merged into C++ core |
| build/ (old build) | Replaced by new CMake build |

### PRESERVE

| Path | Reason |
|------|--------|
| agent/executor.py | NLP tool dispatch, calls C++ core API via pybind11 |
| analysis/ | Spectral indices, atmospheric correction; calls C++ core data API |

### Cleanup timing
Delete all old Python core/gui files in one batch AFTER C++ build succeeds and basic rendering is verified. No gradual migration — two coexisting systems only create confusion.

## 7. Build System

### Dependencies

```
Qt6::Core, Qt6::Gui, Qt6::Widgets, Qt6::Concurrent, Qt6::Network, Qt6::Svg, Qt6::Xml, Qt6::Sql
GDAL >= 3.8
PROJ >= 9.3
pybind11
Eigen3
```

### No longer needed (Python dependencies)

```
rasterio, fiona, pyproj, shapely, matplotlib (core), numpy (core)
```

### CMake structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(antigravity VERSION 1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg Xml Sql)
find_package(GDAL REQUIRED)
find_package(PROJ REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Eigen3 REQUIRED)

add_subdirectory(src/core)       # libantigravity_core.so
add_subdirectory(src/gui)        # libantigravity_gui.so
add_subdirectory(src/app)        # antigravity executable
add_subdirectory(src/python)     # _antigravity_core.so (pybind11)
add_subdirectory(src/server)     # libantigravity_server.so (optional)
```

### Build order

1. `libantigravity_core.so` — no GUI deps, Qt6 Core + GDAL + PROJ
2. `libantigravity_gui.so` — depends on core + Qt6 Widgets
3. `libantigravity_server.so` — depends on core
4. `antigravity` executable — depends on core + gui
5. `_antigravity_core.so` — pybind11 module, depends on core

## 8. Thread Safety & PROJ Solution

QGIS's C++ approach eliminates all thread-safety problems we had in Python:

| Problem | Python (before) | C++ (QGIS approach) |
|---------|-----------------|---------------------|
| PROJ thread safety | pyproj has no per-thread context → SIGSEGV | PJ_CONTEXT per-thread via QgsProjUtils |
| GDAL concurrency | rasterio.open() in BG thread unsafe | GDAL RasterIO + QRecursiveMutex in QgsGdalProvider |
| GIL bottleneck | numpy/warp on main thread, UI freezes | QtConcurrent::map, no GIL |
| Render cancellation | cancel() sets flag, thread may continue | QgsFeedback + GDAL AOI check |
| Data pre-read | Manual pre-read hack | QgsRasterIterator streaming block reads |

Key QGIS source files for thread safety:
- `core/proj/qgsprojutils.cpp` — PJ_CONTEXT per-thread
- `core/providers/gdal/qgsgdalprovider.cpp` — QRecursiveMutex on GDALRasterIO
- `core/raster/qgsrasterpipe.cpp` — Pipe cloning for thread isolation
- `core/maprenderer/qgsmaprendererparalleljob.cpp` — QtConcurrent parallel rendering

## 9. AI Agent Long-term Architecture

```
┌─────────────────────────────────────────────────┐
│                  AI Agent Layer                  │
│  ┌───────────┐ ┌───────────┐ ┌───────────────┐  │
│  │ Scene     │ │ Analysis  │ │ Result        │  │
│  │ Understand│ │ Planner   │ │ Interpreter   │  │
│  │ (多模态)  │ │ (推理链)   │ │ (自然语言)     │  │
│  └─────┬─────┘ └─────┬─────┘ └───────┬───────┘  │
│        │             │               │          │
│  ┌─────┴─────────────┴───────────────┴───────┐  │
│  │           Tool Registry (C++ core)        │  │
│  │  ProcessingAlgorithms + QgsProject + IO   │  │
│  └──────────────────┬────────────────────────┘  │
└─────────────────────┼───────────────────────────┘
                      │ pybind11 / JSON-RPC
┌─────────────────────┼───────────────────────────┐
│                 C++ Core                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ Raster   │ │ Vector   │ │ Processing       │ │
│  │ Pipeline │ │ Engine   │ │ Framework        │ │
│  │ (QGIS)   │ │ (QGIS)   │ │ (QgsProcessing)  │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ OGC      │ │ Plugin   │ │ Analysis (custom) │ │
│  │ Server   │ │ System   │ │ NDVI/Pansharp/.. │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
└─────────────────────────────────────────────────┘
```

### Agent ↔ C++ Core interfaces

| Interface | Method | Purpose |
|-----------|--------|---------|
| QgsProcessingAlgorithm | pybind11 | Agent calls processing (NDVI, classification, atmospheric correction...) |
| QgsProject | pybind11 | Agent queries/modifies project state (layers, CRS, extent) |
| QgsRasterLayer | pybind11 | Agent reads raster metadata, statistics, samples pixel values |
| OGC Server | HTTP | Agent publishes analysis results as WMS/WFS for external GIS |
| Plugin System | Python/C++ | Third-party algorithm extensions, custom Agent capabilities |

### Agent evolution roadmap

| Phase | Capability | Implementation |
|-------|-----------|----------------|
| V1 (current) | Hardcoded tool dispatch | JSON dispatch, fixed tool list |
| V2 | Structured tool calling | C++ QgsProcessingFramework exposed as unified tool API, Agent calls by name+params |
| V3 | Multimodal scene understanding | Agent reads raster thumbnails/statistics, classifies scene type (urban/agriculture/maritime...) |
| V4 | Autonomous analysis planning | Given goal ("assess vegetation health in this area"), Agent selects algorithm chain, parameters, output format |
| V5 | Result interpretation & iteration | Agent interprets output (classification accuracy, anomaly regions), auto-tunes parameters and re-analyzes |

### C++ Core provisions for Agent

- `QgsProcessingAlgorithm` framework has built-in `name()/displayName()/parameters()/prepare()/process()` — natural tool interface
- `QgsProcessingFeedback` supports progress reporting and cancellation — Agent monitors long-running tasks
- OGC Server publishes Agent analysis results as WMS instantly — consumable by external GIS
- Plugin System allows dynamic algorithm registration — Agent loads extensions on demand
