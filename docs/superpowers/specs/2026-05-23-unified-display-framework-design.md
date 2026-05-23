# Unified Display Framework (QGIS Architecture Port)

## 1. Overview
This design implements a unified geospatial display framework for the Antigravity RS platform, strictly adhering to the architectural patterns found in the QGIS source code. The goal is to move from a monolithic, UI-blocking canvas to a decoupled, multi-threaded rendering pipeline capable of handling large-scale raster and vector datasets smoothly.

## 2. Core Architectural Components
The framework is built on four primary pillars that separate data, state, visual strategy, and execution.

### 2.1 MapCanvas (`gui/canvas.py`)
The orchestrator of the map view.
- **Responsibility:** Manages user interaction (MapTools), view state (Extent, CRS, Rotation), and triggers asynchronous refreshes.
- **Key Pattern:** **Mediator**. It coordinates between the Layer Tree and the Rendering Engine.

### 2.2 MapLayer & DataProviders (`engine/core/display/`)
The data entities.
- **QgsMapLayer (Abstract):** Base class for all displayable data. Stores state like opacity, visibility, and name.
- **QgsDataProvider (Abstract):** Decouples the physical data source (GDAL, OGR, Web Services) from the layer logic.
- **Concrete Types:** 
    - `RasterLayer` + `GDALDataProvider`
    - `VectorLayer` + `OGRDataProvider`

### 2.3 Renderer Engine (`engine/core/display/renderers/`)
The visual strategy.
- **Responsibility:** Determines *how* data is drawn (colors, symbols, line widths).
- **QgsFeatureRenderer (Abstract):** Defines the interface for drawing vector features.
- **Concrete Types:** `SingleSymbolRenderer` (V1), `CategorizedSymbolRenderer` (Future).

### 2.4 Rendering Pipeline (`engine/core/display/pipeline/`)
The asynchronous execution engine.
- **MapSettings:** A lightweight, thread-safe snapshot of the current view (Extent, visible Layers, CRS).
- **MapRendererJob:** An asynchronous worker that iterates through layers, requests data from providers, applies renderers, and composes a final `QImage`.
- **Key Pattern:** **Producer-Consumer**. Keeps the UI responsive during heavy I/O and drawing.

## 3. Directory Structure
```text
engine/core/display/
├── base/
│   ├── map_layer.py        # Base class for all layers
│   ├── data_provider.py    # Base class for I/O
│   └── map_settings.py     # Immutable view snapshot
├── renderers/
│   ├── base.py             # FeatureRenderer interface
│   └── vector/
│       └── single_symbol.py
├── pipeline/
│   └── renderer_job.py     # Multi-threaded drawing logic
├── raster/
│   ├── layer.py            # Raster-specific logic
│   └── provider.py         # GDAL wrapper
└── vector/
    ├── layer.py            # Vector-specific logic
    └── provider.py         # OGR wrapper
```

## 4. Interaction Model (MapTools)
Interaction logic will be extracted from the Canvas into **MapTools**:
- `MapToolPan`: Handles mouse-drag panning.
- `MapToolZoom`: Handles rectangle-zoom or wheel-zoom.
- `MapToolIdentify`: (Future) Interrogates features at a coordinate.

## 5. Implementation Strategy (Phase 1: Skeleton)
The first implementation phase will focus on the "Skeleton Framework":
1.  **Refactor MapCanvas** to use the `MapSettings` / `MapRendererJob` pattern.
2.  **Define Abstract Base Classes** for Layers and Providers.
3.  **Implement GDAL and OGR Providers** to support existing Tiff and Shapefile loading.
4.  **Create a basic SingleSymbolRenderer** for vector display.
5.  **Enable Asynchronous Rendering** with a cancellation mechanism for smooth panning.

## 6. Testing & Validation
- **Unit Tests:** Verify that `MapSettings` snapshots correctly capture canvas state.
- **Mock Rendering:** Test `MapRendererJob` with mock providers to ensure multi-threading safety.
- **Visual Validation:** Ensure the final composed image matches current monolithic rendering results for the same datasets.
