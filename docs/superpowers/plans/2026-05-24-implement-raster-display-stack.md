# Implement Raster Display Stack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the modular raster display stack, including `GDALDataProvider` and `RasterLayer`, following the QGIS-inspired architecture.

**Architecture:** The stack uses `GDALDataProvider` to wrap `GeospatialReader` for data access, and `RasterLayer` to handle the drawing logic. `RasterLayer` reuses `GDALDataProvider` to read data for specific extents and resolutions requested by `MapSettings`.

**Tech Stack:** Python, GDAL (via rasterio), NumPy, PySide6 (for QPainter and related classes).

---

### Task 1: Implement GDALDataProvider

**Files:**
- Create: `engine/core/display/raster/provider.py`
- Test: `tests/test_raster_provider.py`

- [ ] **Step 1: Write the failing test for GDALDataProvider**

```python
import pytest
import os
from engine.core.display.raster.provider import GDALDataProvider

def test_gdal_data_provider_extent():
    # Use existing sample data
    sample_path = "data/sample_crops.tif"
    if not os.path.exists(sample_path):
        pytest.skip("Sample data not found")
        
    provider = GDALDataProvider(sample_path)
    extent = provider.extent()
    
    assert "left" in extent
    assert "right" in extent
    assert "top" in extent
    assert "bottom" in extent
    assert extent["left"] < extent["right"]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_raster_provider.py`
Expected: FAIL with "ModuleNotFoundError"

- [ ] **Step 3: Write implementation for GDALDataProvider**

```python
from engine.core.display.base.data_provider import DataProvider
from engine.core.reader import GeospatialReader

class GDALDataProvider(DataProvider):
    def __init__(self, uri: str):
        self.reader = GeospatialReader(uri)
    
    def extent(self):
        return self.reader.metadata["bounds"]
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_raster_provider.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add engine/core/display/raster/provider.py tests/test_raster_provider.py
git commit -m "feat: implement GDALDataProvider"
```

### Task 2: Implement RasterLayer

**Files:**
- Create: `engine/core/display/raster/layer.py`
- Test: `tests/test_raster_layer.py`

- [ ] **Step 1: Write the failing test for RasterLayer**

```python
import pytest
from engine.core.display.raster.layer import RasterLayer
from engine.core.display.base.map_settings import MapSettings

def test_raster_layer_instantiation():
    sample_path = "data/sample_crops.tif"
    layer = RasterLayer("raster-1", "Sample Raster", sample_path)
    
    assert layer.id == "raster-1"
    assert layer.name == "Sample Raster"
    assert layer.provider is not None
    assert layer.extent is not None

def test_raster_layer_draw_exists():
    sample_path = "data/sample_crops.tif"
    layer = RasterLayer("raster-1", "Sample Raster", sample_path)
    settings = MapSettings()
    
    # Just check if it can be called without error for now (minimal implementation)
    # Full verification would require a QPainter mock or similar
    layer.draw(None, settings)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_raster_layer.py`
Expected: FAIL with "ModuleNotFoundError"

- [ ] **Step 3: Write implementation for RasterLayer**

```python
from engine.core.display.base.map_layer import MapLayer
from engine.core.display.raster.provider import GDALDataProvider
import numpy as np
from PySide6.QtGui import QImage, QPixmap

class RasterLayer(MapLayer):
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = GDALDataProvider(uri)
        self.extent = self.provider.extent()

    def draw(self, painter, settings):
        # Implementation based on gui/canvas.py logic
        if painter is None:
            return

        # 1. Determine read parameters from settings
        # (For now, use simplified logic from canvas.py)
        reader = self.provider.reader
        metadata = reader.metadata
        
        max_dim = max(metadata["width"], metadata["height"])
        scale_factor = 1
        if max_dim > 2048:
            scale_factor = int(max_dim / 2048)
            
        # 2. Read data
        band_count = metadata["count"]
        if band_count >= 3:
            r_band = reader.read_raster_band(1, scale_factor)
            g_band = reader.read_raster_band(2, scale_factor)
            b_band = reader.read_raster_band(3, scale_factor)
            
            def norm(arr):
                amin, amax = arr.min(), arr.max()
                if amax - amin > 0:
                    return ((arr - amin) / (amax - amin) * 255).astype(np.uint8)
                return np.zeros_like(arr, dtype=np.uint8)
                
            r_norm = norm(r_band)
            g_norm = norm(g_band)
            b_norm = norm(b_band)
            
            h, w = r_norm.shape
            rgb = np.dstack((r_norm, g_norm, b_norm))
            rgb_data = np.ascontiguousarray(rgb)
            q_img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
            image_to_draw = q_img.copy()
        else:
            band = reader.read_raster_band(1, scale_factor)
            amin, amax = band.min(), band.max()
            if amax - amin > 0:
                norm_band = ((band - amin) / (amax - amin) * 255).astype(np.uint8)
            else:
                norm_band = np.zeros_like(band, dtype=np.uint8)
                
            h, w = norm_band.shape
            gray = np.dstack((norm_band, norm_band, norm_band))
            gray_data = np.ascontiguousarray(gray)
            q_img = QImage(gray_data.data, w, h, 3 * w, QImage.Format_RGB888)
            image_to_draw = q_img.copy()

        # 3. Paint using the painter
        # Note: Proper extent-based transformation logic would go here.
        # For Task 2, we just ensure the drawing method is implemented.
        pixmap = QPixmap.fromImage(image_to_draw)
        painter.drawPixmap(0, 0, pixmap)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_raster_layer.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add engine/core/display/raster/layer.py tests/test_raster_layer.py
git commit -m "feat: implement RasterLayer"
```

### Task 3: Final Verification

- [ ] **Step 1: Run all tests**

Run: `pytest tests/test_raster_provider.py tests/test_raster_layer.py`
Expected: ALL PASS

- [ ] **Step 2: Verify git status**

Run: `git status`
Expected: Clean status with only new files added.
