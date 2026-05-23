# Refactor MapCanvas to use Unified Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the UI to use the new modular display framework with asynchronous rendering and unified layer management.

**Architecture:** Transition from direct QGraphicsScene manipulation to a modular system where MapCanvas uses MapSettings to configure a MapRendererJob. The job renders layers into a QImage asynchronously, which is then displayed in the canvas. Coordinate transformations are moved into MapSettings.

**Tech Stack:** Python, PySide6, NumPy, GDAL, Shapely.

---

### Task 1: Enhance MapSettings with Coordinate Transformations

**Files:**
- Modify: `engine/core/display/base/map_settings.py`

- [ ] **Step 1: Add transformation methods to MapSettings**

```python
from PySide6.QtGui import QTransform
from PySide6.QtCore import QRectF, QSize

class MapSettings:
    def __init__(self):
        self.layers = []
        self.extent = None # QRectF (World coordinates)
        self.output_size = None # QSize (Device coordinates)
        self.destination_crs = "EPSG:3857"

    def worldToDevice(self) -> QTransform:
        if not self.extent or not self.output_size or self.extent.isEmpty():
            return QTransform()
            
        world_width = self.extent.width()
        world_height = self.extent.height()
        device_width = self.output_size.width()
        device_height = self.output_size.height()
        
        s_x = device_width / world_width
        s_y = device_height / world_height
        
        # Map (left, top) world to (0, 0) device
        # Note: GIS Y increases UP, Device Y increases DOWN
        transform = QTransform()
        transform.scale(s_x, -s_y)
        transform.translate(-self.extent.left(), -self.extent.top())
        return transform

    def deviceToWorld(self) -> QTransform:
        return self.worldToDevice().inverted()[0]
```

- [ ] **Step 2: Commit**

```bash
git add engine/core/display/base/map_settings.py
git commit -m "feat: add coordinate transformation helpers to MapSettings"
```

---

### Task 2: Update Layer Rendering to use MapSettings Transformations

**Files:**
- Modify: `engine/core/display/raster/layer.py`
- Modify: `engine/core/display/renderers/vector/single_symbol.py`

- [ ] **Step 1: Update RasterLayer.draw to use worldToDevice**

```python
    def draw(self, painter, settings):
        # ... (loading logic same as before) ...
        if image_to_draw:
            painter.save()
            # Calculate transform from layer extent to device
            # For simplicity, if we already have the image downsampled to the layer's extent:
            # We need to find where the layer's extent is in device coordinates.
            world_to_device = settings.worldToDevice()
            
            # Reproject layer extent if CRS differs (assume same for now as per current logic)
            layer_rect_device = world_to_device.mapRect(self.extent)
            
            if self.opacity < 1.0:
                painter.setOpacity(self.opacity)
            
            painter.drawImage(layer_rect_device, image_to_draw)
            painter.restore()
```

- [ ] **Step 2: Update SingleSymbolRenderer to use worldToDevice**

```python
    def _to_qpainter_path(self, shape, settings):
        path = QPainterPath()
        transform = settings.worldToDevice()
        
        def transform_point(x, y):
            p = transform.map(QPointF(x, y))
            return p.x(), p.y()

        if shape.geom_type == 'Point':
            x, y = transform_point(shape.x, shape.y)
            path.addEllipse(x - 3, y - 3, 6, 6)
        # ... and so on for LineString and Polygon, applying transform_point to all coords ...
```

- [ ] **Step 3: Commit**

```bash
git add engine/core/display/raster/layer.py engine/core/display/renderers/vector/single_symbol.py
git commit -m "feat: use MapSettings transformations in layer rendering"
```

---

### Task 3: Refactor MapCanvas for Async Rendering

**Files:**
- Modify: `gui/canvas.py`

- [ ] **Step 1: Overhaul MapCanvas to use MapRendererJob**
  - Change `__init__` to initialize `layers` list and `viewport_extent`.
  - Add `add_layer(layer)` and `remove_layer(layer_id)`.
  - Implement `refresh()`:
    1. Cancel previous job if running.
    2. Create `MapSettings` snapshot.
    3. Start `MapRendererJob` in `QThreadPool`.
  - Implement `on_render_finished(image)`:
    1. Update a `QGraphicsPixmapItem` in the scene with the new image.
  - Implement `wheelEvent` and `mouseMoveEvent` to update `viewport_extent` and call `refresh()`.

- [ ] **Step 2: Commit**

```bash
git add gui/canvas.py
git commit -m "refactor: MapCanvas now uses async MapRendererJob and Unified Framework"
```

---

### Task 4: Update main.py and Final Integration

**Files:**
- Modify: `main.py`

- [ ] **Step 1: Update main.py to use new MapCanvas API**
  - Instantiate `RasterLayer` and `VectorLayer` instead of calling `add_raster_layer` / `add_vector_layer`.
  - Call `canvas.add_layer(layer)`.
  - Update visibility and reordering handlers to work with `MapLayer` objects.

- [ ] **Step 2: Final verification**
  - Run the application.
  - Load a raster and a vector.
  - Verify zoom/pan works and rendering is smooth.

- [ ] **Step 3: Commit**

```bash
git add main.py
git commit -m "feat: complete integration of modular display framework in main app"
```
