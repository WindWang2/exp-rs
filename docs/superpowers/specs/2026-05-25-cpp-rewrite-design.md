# Antigravity RS: Full C++ Rewrite Design

**Date**: 2026-05-25
**Status**: Approved
**License**: GPL v2 (accepted — derives from QGIS source; whole product ships GPL v2)
**Approach**: Fork QGIS C++ source subset, all core + GUI in C++, Python only for agent/analysis shell

## 1. Motivation

Current Python implementation has fundamental performance and stability problems:
- PROJ DatabaseContext SIGSEGV when rasterio.open() called from background threads on CRS-bearing files
- Python GIL bottleneck: ~1s render time for LE7 (7951x7171), UI freezes
- rasterio/pyproj are wrappers around C libraries; calling them through Python adds overhead and thread-safety issues that don't exist at the C level
- No path to QGIS-level performance without moving rendering to C++

QGIS solves all of these at the C++ level: per-thread PJ_CONTEXT, GDAL RasterIO with QRecursiveMutex, QtConcurrent parallel rendering, QgsRasterPipe cloning for thread isolation.

## 2. Alternatives Considered

The §8 problem table can be solved three ways. We evaluated all three and chose source fork with eyes open about the cost.

| Option | How it solves §8 | Cost | Control | Decision |
|--------|------------------|------|---------|----------|
| **A. Python-only fix** | per-thread pyproj/subprocess isolation + QThreadPool | Lowest (weeks) | Low — never reaches QGIS render perf; still wrapping C through Python | ❌ Rejected: doesn't close the perf gap (§1) |
| **B. Depend on QGIS via PyQGIS** | `import qgis.core` — gets per-thread PJ_CONTEXT, QRecursiveMutex GDALRasterIO, QtConcurrent rendering for free; no fork, no hand-written bindings, upstream fixes flow in | Low–medium (months) | Medium — bound by what SIP exposes; runtime tied to a system QGIS install | ❌ Rejected: see rationale below |
| **C. Fork QGIS source subset** (this spec) | Same C++ mechanisms, compiled into our own libs | High (person-year class) | Highest — full control of build, API surface, packaging, and divergence | ✅ **Chosen** |

**Why C over B**, given GPL is acceptable and B is far cheaper:
- We want a single self-contained build artifact with no dependency on a system-installed QGIS of a matching version. PyQGIS pins us to whatever QGIS the host provides.
- We intend to modify core behavior (custom raster pipeline tuning, embedded OGC server config, agent-facing registry hooks) that SIP bindings don't expose and we don't want to maintain as monkey-patches.
- The pybind11 surface we expose to the agent is deliberately narrow and our own; we don't inherit SIP's full API or its overhead.

**The cost we accept by choosing C** (tracked in §15–§16): person-year build effort, a maintenance fork that diverges from upstream, hand-written bindings, and full ownership of runtime init/packaging. PyQGIS (B) is retained as the fallback if Phase P0 stalls (see §16 rollback).

## 3. Approach: Fork QGIS Source Subset

Copy source files directly from `qgis_ref/src/` into our project. Preserve original class names, file names, and APIs. Exclude modules we don't need by not compiling them, not by deleting code.

**Realism: this is a dependency closure, not a hand-picked file list.** QGIS core is ~2035 .cpp/.h files with dense internal coupling. "Not compiling a module" produces link errors, not a clean subset. The forking method is therefore iterative:

1. Start from the P0 target classes (§5).
2. Compile; collect undefined-symbol / missing-header errors.
3. Add the files that define those symbols (this pulls in `QgsApplication`, settings, units, base symbol types, and — see §6 — the expression engine).
4. Repeat until link succeeds, then move to P1.

Expect the actual file set to grow well beyond the per-directory counts in §5. Those counts are starting targets, not the final closure.

## 4. Project Structure

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
│   │   ├── expression/               # REQUIRED (see §6) — pulled in by vector/labeling/processing
│   │   ├── providers/gdal/           # GDAL raster+vector provider
│   │   ├── providers/ogr/            # OGR vector provider
│   │   ├── providers/memory/         # Memory provider
│   │   ├── project/                  # QgsProject
│   │   ├── scalebar/                 # QgsScaleBarRenderer
│   │   ├── ogc/                      # OGC utilities
│   │   ├── processing/               # QgsProcessingFramework
│   │   ├── plugin/                   # Plugin layer registry
│   │   └── ... (transitive closure of the above)
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
│   └── python/                       # pybind11 bindings (§10)
│       └── bindings.cpp
├── agent/                            # PRESERVED: Python NLP executor
├── analysis/                         # HYBRID: Python algos + C++ algos (§11)
└── tests/
```

## 5. Modules to Fork (by Priority)

### P0 — Rendering Core (first build, solves PROJ crash + performance)

| Source | Classes | Purpose |
|--------|---------|---------|
| core/raster/*.cpp/h | QgsRasterBlock, QgsRasterDataProvider, QgsRasterPipe, QgsRasterProjector, QgsRasterResampleFilter, QgsRasterIterator, QgsRasterDrawer, QgsRasterInterface | Raster data pipeline: block API, reprojection, resampling, drawing |
| core/maprenderer/*.cpp/h | QgsMapRendererJob, QgsMapRendererParallelJob, QgsMapRendererQImageJob, QgsRenderContext, QgsMapSettings | Async parallel render dispatch |
| core/proj/*.cpp/h | QgsProjUtils, QgsCoordinateTransform, QgsCoordinateReferenceSystem | PROJ thread-safe wrapper (PJ_CONTEXT per-thread) |
| core/providers/gdal/*.cpp/h | QgsGdalProvider, QgsGdalProviderBase | GDAL RasterIO direct call with QRecursiveMutex |
| core/qgsrectangle.cpp/h, qgsmaptopixel.cpp/h, qgspointxy.cpp/h | Basic geometry | Render computation dependencies |
| core/qgsapplication.cpp/h + qgsconfig.h | QgsApplication | Runtime init, data paths (§9) — pulled in by everything |

### P1 — Layer & Project (second build)

| Source | Classes | Purpose |
|--------|---------|---------|
| core/qgsmaplayer.cpp/h | QgsMapLayer | Layer base class |
| core/raster/qgsrasterlayer.cpp/h | QgsRasterLayer | Raster layer |
| core/vector/*.cpp/h | QgsVectorLayer, QgsVectorDataProvider, QgsFeature, QgsFeatureIterator | Vector layer |
| core/geometry/*.cpp/h | QgsGeometry, QgsPoint, QgsPolyline, QgsPolygon | Geometry engine |
| core/expression/*.cpp/h | QgsExpression, QgsExpressionContext | Expression engine — hard dependency of vector/labeling/processing |
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

## 6. Excluded Modules

These QGIS modules are NOT forked:
- 3D, pointcloud, mesh, vectortile, tiledscene
- Layout (print composer), annotations, diagrams
- Postgres/Oracle/GRASS/HANA/MSSQL database providers
- Temporal/elevation framework
- SIP/Python binding generation (we write pybind11 manually — see §10)
- GRASS/SAGA/OTB processing providers

**Cannot be excluded (corrected from earlier draft):**
- **Expression engine** (`core/expression/`). It was previously listed as "add later if needed." It is a hard dependency of the modules we keep: in `qgis_ref` it is referenced by 22 files in `vector/`, 18 in `labeling/`, 10 in `processing/`, 5 in `layertree/`, 4 in `raster/`. Forking symbology/labeling/processing/vector pulls it in immediately. It moves into P1.

## 7. Python Code Cleanup

### DELETE entirely (replaced by C++)

| Path | Reason |
|------|--------|
| core/ (entire directory, ~2800 lines) | All replaced by C++ core |
| gui/canvas.py | Replaced by C++ QgsMapCanvas |
| gui/qgsmapcanvas.py | Replaced by C++ QgsMapCanvas |
| gui/qgsmaprendererjob.py | Replaced by C++ QgsMapRendererParallelJob |
| gui/toolbox.py | Replaced by C++ QgsProcessingToolbox |
| gui/splash.py, gui/qgssplash.py | Replaced by C++ QgsSplashScreen |
| src/raster_ops.cpp | Merged into C++ core |
| build/ (old build) | Replaced by new CMake build |

### PRESERVE / MIGRATE

| Path | Disposition |
|------|-------------|
| agent/executor.py | PRESERVE — NLP tool dispatch, calls C++ core API via pybind11 |
| analysis/ | HYBRID — heavy algos port to C++, experimental algos stay Python; both register into one C++ registry via trampoline (§11) |

### Cleanup timing & safety gate
Delete old Python core/gui files in one batch AFTER the C++ build succeeds and rendering passes the parity gate (§17). No gradual two-system coexistence. Safety net (replaces "just delete"): tag the last fully-working Python tree as `python-final` before the batch delete, so PyQGIS-fallback or revert is one `git checkout` away (§16).

## 8. Build System

### Dependencies

```
Qt6::Core, Qt6::Gui, Qt6::Widgets, Qt6::Concurrent, Qt6::Network, Qt6::Svg, Qt6::Xml, Qt6::Sql
GDAL >= 3.8
PROJ >= 9.3
pybind11
Eigen3        # only required by the C++ analysis algorithms (k-means clustering, IHS/Brovey matrix ops); drop if all heavy algos stay Python
```

### No longer needed (Python dependencies)

```
rasterio, fiona, pyproj, shapely, matplotlib (core), numpy (core)
# numpy still required by the Python side of the hybrid analysis layer (§11)
```

### CMake structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(antigravity VERSION 1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)          # QGIS sources are Qt — MOC is mandatory
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg Xml Sql)
find_package(GDAL REQUIRED)
find_package(PROJ REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Eigen3 REQUIRED)

# Generate qgsconfig.h from qgis_ref template — QGIS sources #include it for
# version, data paths, and feature flags. Without it core will not compile.
configure_file(cmake/qgsconfig.h.in ${CMAKE_BINARY_DIR}/qgsconfig.h)

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

## 9. Runtime Initialization

QGIS core hard-faults at runtime without proper init. The forked code is not usable until this is in place:

- **`QgsApplication`** must be constructed before any core call; it sets the PROJ/GDAL search paths and the resource prefix.
- **`srs.db` + `qgis.db`** — the CRS/SRS databases must be bundled and `QgsApplication::setPkgDataPath()` pointed at them. CRS lookups fail silently or crash otherwise.
- **PROJ/GDAL data dirs** — `proj.db` and GDAL data must be discoverable (env vars or `QgsApplication` paths) for reprojection.
- **`resources/`** — symbol/SVG/colorramp resources QGIS expects on disk.
- **Packaging** — these data files ship with the build artifact; the installer/CI must stage them next to the libs.

## 10. Python Bindings (pybind11)

Replacing SIP with hand-written pybind11 is a sizable workstream, not a footnote. Scope:

- **Bound classes (agent/analysis-facing, deliberately narrow):** `QgsProcessingRegistry`, `QgsProcessingAlgorithm` (+ trampoline, §11), `QgsProject`, `QgsRasterLayer`, `QgsMapLayer`, `QgsRectangle`, `QgsCoordinateReferenceSystem`, plus the parameter/feedback value types.
- **Qt interop:** type casters for `QString`, `QVariant`, `QVariantMap`, `QImage` (zero-copy where possible), and the container types these classes expose. This is the part SIP gives for free and we now own.
- **Ownership:** explicit `py::return_value_policy` per method — registry-owned algorithms vs. caller-owned results — to avoid double-free across the boundary.

Budget this as a first-class deliverable in P1/P2, not as glue tacked on at the end.

## 11. Analysis Algorithms: Hybrid C++/Python

Resolves the earlier §6↔§10.5 contradiction. There is **one** algorithm registry — the C++ `QgsProcessingRegistry` — and it holds both C++ and Python algorithms. GUI, code, JSON chains, and the Agent all see a single unified tool list.

| Class of algorithm | Language | Examples | Rationale |
|--------------------|----------|----------|-----------|
| Heavy / hot-path | **C++** `QgsProcessingAlgorithm` subclass | k-means, IHS/Brovey pansharpen, DOS1, zonal stats | streaming blocks, no GIL, Eigen matrix ops |
| Experimental / fast-iterating | **Python** | new indices, prototype classifiers | edit-run loop without recompiling |

**Bridge mechanism (same pattern QGIS itself uses for SIP algorithms):** a pybind11 *trampoline* class `PyProcessingAlgorithm : public QgsProcessingAlgorithm` forwards `name()/parameters()/prepare()/process()` to a Python object implementing the interface. A Python algorithm registers by wrapping itself in the trampoline and calling `QgsProcessingRegistry::addAlgorithm()`. To every consumer it is indistinguishable from a native C++ algorithm.

This makes §10.5's "each module registers its algorithms" literally true regardless of the module's implementation language.

## 12. Thread Safety & PROJ Solution

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

## 13. AI Agent Long-term Architecture (forward-looking)

> Vision section. V2–V5 are **not** in the rewrite build scope (§16); only V1 ships with the C++ core. Recorded here so the registry/binding design (§10–§11) doesn't paint us into a corner.

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

## 14. Modular & Atomic Design

Core design principle: every capability is a composable, configurable atomic unit. No monolithic functions. Actions are driven by code or declarative configuration (JSON/YAML).

### 14.1 Processing Algorithm = Atomic Unit

Every operation is a `QgsProcessingAlgorithm` subclass with:

```
name()        → unique identifier: "antigravity:ndvi"
displayName() → human label: "NDVI Vegetation Index"
parameters()  → typed inputs: {input_raster: RasterLayer, red_band: Number, nir_band: Number, output_path: Destination}
prepare()     → validate & pre-allocate
process()     → execute (streaming blocks, cancellable)
outputValues()→ typed outputs: {output_raster: RasterLayer, statistics: Map}
```

This is already QGIS's design. We inherit it by forking `core/processing/`. Python algorithms implement the same interface via the trampoline (§11).

### 14.2 Algorithm Chains = JSON Config

Complex workflows (e.g., "atmospheric correction → NDVI → threshold → area statistics") are not hardcoded. They are defined as declarative chains:

```json
{
  "chain_id": "vegetation_health_assessment",
  "steps": [
    {
      "algorithm": "antigravity:dos1_correction",
      "params": {"input_raster": "$input", "output_path": "$cache/dos1.tif"}
    },
    {
      "algorithm": "antigravity:ndvi",
      "params": {"input_raster": "$cache/dos1.tif", "red_band": 1, "nir_band": 3, "output_path": "$cache/ndvi.tif"}
    },
    {
      "algorithm": "antigravity:raster_threshold",
      "params": {"input_raster": "$cache/ndvi.tif", "min": 0.3, "output_path": "$output"}
    }
  ],
  "output_mapping": {
    "result": "$output",
    "ndvi": "$cache/ndvi.tif"
  }
}
```

### 14.3 Three Ways to Invoke

| Method | Who | How |
|--------|-----|-----|
| **GUI** | User clicks toolbox | QgsProcessingDialog → algorithm → feedback |
| **Code** | Developer / script | `QgsProcessingRunner.execute("antigravity:ndvi", params)` |
| **Config** | JSON chain / Agent | AlgorithmRegistry resolves name → instantiate → chain executor runs steps sequentially, passing outputs as inputs |

### 14.4 Algorithm Registry (C++ Singleton)

```cpp
class QgsProcessingRegistry {
    // Register at plugin load or app startup (C++ algos directly,
    // Python algos via the PyProcessingAlgorithm trampoline — §11)
    void addAlgorithm(QgsProcessingAlgorithm* algo);

    // Lookup by name — used by GUI, code, config, and Agent
    QgsProcessingAlgorithm* algorithmById(const QString& id) const;

    // List all available algorithms (for toolbox, Agent tool discovery)
    QList<QgsProcessingAlgorithm*> algorithms() const;
};
```

All built-in algorithms (NDVI, DOS1, pansharpening, k-means...) register themselves on startup. Plugins register additional algorithms. The Agent discovers available tools by querying the registry. The registry does not care whether an entry is C++ or Python-backed.

### 14.5 Module Boundaries

Each module owns its directory and registers its algorithms independently (C++ or Python per §11):

```
analysis/
├── indices/           → registers: antigravity:ndvi, antigravity:ndwi, antigravity:savi   (Python — fast iteration)
├── atmospheric/       → registers: antigravity:dos1, antigravity:dark_object_subtraction  (C++ — heavy)
├── classification/    → registers: antigravity:kmeans, antigravity:random_forest          (C++ — heavy)
├── pansharpening/     → registers: antigravity:brovey, antigravity:ihs                     (C++ — matrix ops)
└── statistics/        → registers: antigravity:zonal_stats, antigravity:raster_stats       (C++ — heavy)
```

No cross-dependencies between modules. Each is a self-contained plugin that could be loaded/unloaded independently.

### 14.6 Configuration-driven Rendering

Even rendering behavior is configurable:

```json
{
  "rendering": {
    "parallel_jobs": true,
    "max_threads": 0,
    "preview_resolution": 256,
    "debounce_ms": 30,
    "resampling": "bilinear",
    "overview_preference": "auto"
  }
}
```

This replaces hardcoded constants in the old canvas.py and allows per-project tuning.

## 15. Upstream Tracking & Maintenance

Copying source forfeits automatic upstream fixes. We accept this (§2) but mitigate:

- **Pin a QGIS tag.** Record the exact `qgis_ref` tag/commit each forked file came from in `src/QGIS_BASELINE.md`.
- **No gratuitous edits.** Keep forked files byte-identical to upstream except where a change is required; isolate every local change behind a clearly marked `// ANTIGRAVITY:` comment so re-basing onto a newer QGIS is a diffable operation.
- **Security watch.** Track GDAL/PROJ and QGIS CVE advisories for the pinned versions; re-pull affected files on advisory.
- **Periodic re-base** (best-effort, low cadence) to pull upstream raster/PROJ fixes.

## 16. Effort, Phasing & Risk

**Effort is person-year class.** Rough order-of-magnitude per phase (1 senior C++/Qt engineer; ranges, not commitments):

| Phase | Scope | Rough effort |
|-------|-------|--------------|
| P0 | Rendering core + runtime init (§9) + first pixels on screen | 6–10 weeks |
| P1 | Layer/project + expression engine + pybind11 surface (§10) | 6–10 weeks |
| P2 | Symbology + labeling | 4–8 weeks |
| P3 | GUI canvas + map tools | 4–6 weeks |
| P4 | OGC server + plugins + analysis migration (§11) | 4–8 weeks |

### Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Dependency closure balloons beyond estimate (§3) | High | High | Time-box P0; if closure unbounded at the time-box, fall back to PyQGIS (§2 option B) |
| Runtime data/init (§9) blocks first render | Medium | High | Treat §9 as part of P0 acceptance, not an afterthought |
| pybind11 surface (§10) larger than budgeted | Medium | Medium | Bind the narrow agent-facing set only; defer the rest |
| Maintenance fork diverges, CVEs missed | Medium | Medium | §15 baseline tracking + security watch |
| Big-bang Python delete (§7) leaves no working app | Low | High | `python-final` tag + parity gate (§17) before delete |

### Rollback
The `python-final` git tag (§7) is the rollback point. If P0 fails the time-box, revert to it and pursue PyQGIS (§2 option B), which reuses the preserved Python shell.

## 17. Testing & Parity Strategy

The cutover in §7 is gated on objective parity, not "looks done":

- **Port the existing 29 tests** (`tests/*.py`) to drive the C++ core through the pybind11 module.
- **Pixel-parity gate:** render LE7 (7951x7171) and a CRS-bearing sample through both the old Python path and the new C++ path; require per-band RMSE below a fixed threshold before deleting Python core/gui.
- **Thread-safety regression:** the original PROJ-SIGSEGV repro (background `open()` on a CRS-bearing file) must run clean under the C++ path — this is the headline acceptance test for §12.
- **Performance gate:** LE7 render wall-time must beat the ~1s Python baseline and not freeze the UI thread (off-main-thread render confirmed).
