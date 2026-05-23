# Unified Display Framework (QGIS Architecture Port) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the existing display system into a modular, asynchronous framework that mirrors the QGIS source code architecture (`QgsMapCanvas`, `QgsMapLayer`, `QgsDataProvider`, `QgsMapSettings`).

**Architecture:** Python-based port of QGIS C++ patterns. Decoupled Data Providers handle I/O; MapLayers manage state; MapSettings provides immutable view snapshots; MapRendererJob executes background rendering.

**Tech Stack:** PySide6, GDAL/OGR, NumPy.

---

### Task 1: Initialize Core Base Classes (QGIS Port)

**Files:**
- Create: `engine/core/display/base/map_layer.py` (QgsMapLayer)
- Create: `engine/core/display/base/data_provider.py` (QgsDataProvider)
- Create: `engine/core/display/base/map_settings.py` (QgsMapSettings)

- [ ] **Step 1: Create `MapLayer` (QgsMapLayer equivalent)**
Implement base properties: `id`, `name`, `opacity`, `visible`, `crs`, `extent`. Add abstract `draw()` method.

```python
# engine/core/display/base/map_layer.py
from abc import ABC, abstractmethod

class MapLayer(ABC):
    def __init__(self, layer_id: str, name: str):
        self.id = layer_id
        self.name = name
        self.visible = True
        self.opacity = 1.0
        self.crs = None
        self.extent = None

    @abstractmethod
    def draw(self, painter, settings):
        pass
```

- [ ] **Step 2: Create `DataProvider` (QgsDataProvider equivalent)**
Abstract base for data access.

```python
# engine/core/display/base/data_provider.py
from abc import ABC, abstractmethod

class DataProvider(ABC):
    @abstractmethod
    def extent(self):
        pass
```

- [ ] **Step 3: Create `MapSettings` (QgsMapSettings equivalent)**
Thread-safe snapshot of the view state.

```python
# engine/core/display/base/map_settings.py
class MapSettings:
    def __init__(self):
        self.layers = []
        self.extent = None
        self.output_size = None # QSize
        self.destination_crs = "EPSG:3857"
```

- [ ] **Step 4: Commit**
```bash
git add engine/core/display/base/
git commit -m "feat: add base display classes (QgsMapLayer, QgsDataProvider, QgsMapSettings)"
```

---

### Task 2: Implement Raster Display Stack

**Files:**
- Create: `engine/core/display/raster/provider.py` (GDALProvider)
- Create: `engine/core/display/raster/layer.py` (RasterLayer)

- [ ] **Step 1: Implement `GDALDataProvider`**
Wrapper for GDAL to read data for a specific extent and resolution.

```python
# engine/core/display/raster/provider.py
from engine.core.display.base.data_provider import DataProvider
from engine.core.reader import GeospatialReader

class GDALDataProvider(DataProvider):
    def __init__(self, uri: str):
        self.reader = GeospatialReader(uri)
    
    def extent(self):
        return self.reader.metadata["bounds"]
```

- [ ] **Step 2: Implement `RasterLayer`**
Specialized layer for pixel data.

```python
# engine/core/display/raster/layer.py
from engine.core.display.base.map_layer import MapLayer
from engine.core.display.raster.provider import GDALDataProvider

class RasterLayer(MapLayer):
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = GDALDataProvider(uri)
        self.extent = self.provider.extent()

    def draw(self, painter, settings):
        # Implementation: read via provider, convert to QImage, paint
        pass
```

- [ ] **Step 3: Verify and Commit**
```bash
git add engine/core/display/raster/
git commit -m "feat: implement modular raster display components"
```

---

### Task 3: Implement Vector Display Stack (OGR & Renderer)

**Files:**
- Create: `engine/core/display/vector/provider.py` (OGRProvider)
- Create: `engine/core/display/vector/layer.py` (VectorLayer)
- Create: `engine/core/display/renderers/base.py` (FeatureRenderer)
- Create: `engine/core/display/renderers/vector/single_symbol.py` (SingleSymbolRenderer)

- [ ] **Step 1: Implement `OGRDataProvider`**
Reads vector features for a given extent.

- [ ] **Step 2: Implement `FeatureRenderer` base and `SingleSymbolRenderer`**
Define the strategy for how vector features look.

```python
# engine/core/display/renderers/base.py
class FeatureRenderer(ABC):
    @abstractmethod
    def render_feature(self, feature, painter):
        pass
```

- [ ] **Step 3: Implement `VectorLayer`**
Wires together the Provider and Renderer.

- [ ] **Step 4: Verify and Commit**
```bash
git add engine/core/display/vector/ engine/core/display/renderers/
git commit -m "feat: implement modular vector display components and renderer strategy"
```

---

### Task 4: Implement Asynchronous Rendering Job

**Files:**
- Create: `engine/core/display/pipeline/renderer_job.py` (QgsMapRendererParallelJob)

- [ ] **Step 1: Implement `MapRendererJob` using QRunnable**
Iterates through layers from `MapSettings` and draws them to a shared `QImage`.

```python
# engine/core/display/pipeline/renderer_job.py
from PySide6.QtCore import QRunnable, Signal, QObject
from PySide6.QtGui import QImage, QPainter

class JobSignals(QObject):
    finished = Signal(QImage)

class MapRendererJob(QRunnable):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.signals = JobSignals()
        
    def run(self):
        # Create output buffer
        image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        image.fill(0) # Transparent
        painter = QPainter(image)
        
        for layer in self.settings.layers:
            if layer.visible:
                layer.draw(painter, self.settings)
        
        painter.end()
        self.signals.finished.emit(image)
```

- [ ] **Step 2: Implement Cancellation Mechanism**
Ensure we can abort jobs if a new pan/zoom occurs (matching `QgsMapRendererJob::cancel`).

- [ ] **Step 3: Verify and Commit**
```bash
git add engine/core/display/pipeline/
git commit -m "feat: implement asynchronous map renderer job"
```

---

### Task 5: Refactor MapCanvas to use Unified Framework

**Files:**
- Modify: `gui/canvas.py`
- Modify: `main.py`

- [ ] **Step 1: Refactor `MapCanvas` to manage `MapSettings`**
Remove monolithic `add_raster_layer` / `add_vector_layer` logic. Replace with `refresh()` which spawns a `MapRendererJob`.

- [ ] **Step 2: Migrate existing layers to new classes**
Update `main.py` loading logic to use `RasterLayer` and `VectorLayer`.

- [ ] **Step 3: Implement MapTools (Pan/Zoom)**
Move interaction logic out of the Canvas into specialized Tool classes (matching `QgsMapTool`).

- [ ] **Step 4: Final verification and old code cleanup**
Run full suite, ensure responsiveness, and remove deprecated logic from `MapCanvas`.

- [ ] **Step 5: Commit**
```bash
git add gui/canvas.py main.py
git commit -m "refactor: transition MapCanvas to modular QGIS-style display framework"
```
