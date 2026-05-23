# RS Processing Algorithms Extraction and Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the modular architecture refactoring by extracting NDVI, NDWI, and KMeans algorithms into their own files and cleaning up the old flat files.

**Architecture:** Moving functions from `engine/_processing.py` to modular subpackages (`engine/processing/indices` and `engine/processing/classification`). Updating registration and wiring them up in `__init__.py` files.

**Tech Stack:** Python, NumPy, Rasterio, Scikit-learn.

---

### Task 1: Extract Algorithms to Modular Files

**Files:**
- Create: `engine/processing/indices/vegetation.py`
- Create: `engine/processing/indices/water.py`
- Create: `engine/processing/classification/kmeans.py`

- [ ] **Step 1: Create `engine/processing/indices/vegetation.py`**

```python
import os
import numpy as np
import rasterio
from engine.core.reader import GeospatialReader
from engine.registry import register_tool

@register_tool(
    name="calculate_ndvi",
    label="Normalized Difference Vegetation Index (NDVI)",
    category="Raster Algebra",
    description="Calculates vegetation vigor (NDVI) from Red and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDVI GeoTIFF will be saved"},
        {"name": "red_band", "label": "Red Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Red wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ]
)
def calculate_ndvi(input_path: str, output_path: str, red_band: int = 1, nir_band: int = 2) -> str:
    """
    Calculates Normalized Difference Vegetation Index (NDVI).
    NDVI = (NIR - Red) / (NIR + Red)
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
    
    red = reader.read_raster_band(red_band).astype(np.float32)
    nir = reader.read_raster_band(nir_band).astype(np.float32)
    
    # Safe vectorized division
    with np.errstate(divide='ignore', invalid='ignore'):
        ndvi = (nir - red) / (nir + red)
        ndvi = np.nan_to_num(ndvi, nan=0.0, posinf=1.0, neginf=-1.0)
    
    # Write output GeoTIFF keeping spatial profiles
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.float32
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(ndvi, 1)
        
    return output_path
```

- [ ] **Step 2: Create `engine/processing/indices/water.py`**

```python
import os
import numpy as np
import rasterio
from engine.core.reader import GeospatialReader
from engine.registry import register_tool

@register_tool(
    name="calculate_ndwi",
    label="Normalized Difference Water Index (NDWI)",
    category="Raster Algebra",
    description="Calculates open water body intensity (NDWI) from Green and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDWI GeoTIFF will be saved"},
        {"name": "green_band", "label": "Green Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Green wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ]
)
def calculate_ndwi(input_path: str, output_path: str, green_band: int = 1, nir_band: int = 2) -> str:
    """
    Calculates Normalized Difference Water Index (NDWI).
    NDWI = (Green - NIR) / (Green + NIR)
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
    
    green = reader.read_raster_band(green_band).astype(np.float32)
    nir = reader.read_raster_band(nir_band).astype(np.float32)
    
    # Safe vectorized division
    with np.errstate(divide='ignore', invalid='ignore'):
        ndwi = (green - nir) / (green + nir)
        ndwi = np.nan_to_num(ndwi, nan=0.0, posinf=1.0, neginf=-1.0)
        
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.float32
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(ndwi, 1)
        
    return output_path
```

- [ ] **Step 3: Create `engine/processing/classification/kmeans.py`**

```python
import os
import numpy as np
import rasterio
from sklearn.cluster import KMeans
from engine.core.reader import GeospatialReader
from engine.registry import register_tool

@register_tool(
    name="kmeans_classify",
    label="K-Means Unsupervised Classification",
    category="Classification",
    description="Classifies multi-spectral remote sensing pixels into arbitrary land cover groups using K-Means clustering.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Classified Raster", "type": "file", "required": True, "help": "Path where the thematic classified GeoTIFF will be saved"},
        {"name": "bands", "label": "Spectral Bands (comma separated)", "type": "string", "default": "1,2,3", "required": True, "help": "Indices of bands to use in classification (e.g. 1,2,3)"},
        {"name": "clusters", "label": "Target Land cover classes", "type": "int", "default": 5, "required": True, "help": "Number of unique land cover clusters (K)"}
    ]
)
def kmeans_classify(input_path: str, output_path: str, bands: str = "1,2,3", clusters: int = 5) -> str:
    """
    Performs K-Means unsupervised land classification on specified bands.
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
        
    band_indices = [int(b.strip()) for b in bands.split(",") if b.strip()]
    band_data = []
    
    for b in band_indices:
        band_data.append(reader.read_raster_band(b).astype(np.float32))
        
    # Stack bands into (height, width, band_count) and flatten
    stacked = np.stack(band_data, axis=-1)
    h, w, c = stacked.shape
    flattened = stacked.reshape(-1, c)
    
    # Handle NaN values
    flattened = np.nan_to_num(flattened, nan=0.0)
    
    # Fit KMeans clustering classification
    kmeans = KMeans(n_clusters=clusters, random_state=42, n_init='auto')
    labels = kmeans.fit_predict(flattened)
    classified = labels.reshape(h, w).astype(np.int16)
    
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.int16
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(classified, 1)
        
    return output_path
```

- [ ] **Step 4: Commit extracted files**

```bash
git add engine/processing/indices/vegetation.py engine/processing/indices/water.py engine/processing/classification/kmeans.py
git commit -m "refactor: extract NDVI, NDWI, and KMeans to modular files"
```

### Task 2: Wire up Registration in `__init__.py` files

**Files:**
- Modify: `engine/preprocessing/__init__.py`
- Modify: `engine/processing/__init__.py`
- Modify: `engine/__init__.py`

- [ ] **Step 1: Update `engine/preprocessing/__init__.py`**

```python
from .atmospheric.dos1 import calculate_dos1
from .geometric.rectify import calculate_polynomial_coeffs

__all__ = ["calculate_dos1", "calculate_polynomial_coeffs"]
```

- [ ] **Step 2: Update `engine/processing/__init__.py`**

```python
from .pansharpening.pca import pca_pansharpen_arrays
from .indices.vegetation import calculate_ndvi
from .indices.water import calculate_ndwi
from .classification.kmeans import kmeans_classify

__all__ = ["pca_pansharpen_arrays", "calculate_ndvi", "calculate_ndwi", "kmeans_classify"]
```

- [ ] **Step 3: Update `engine/__init__.py`**

```python
from .core.reader import GeospatialReader
from .core.projection import CRSTransformer
from .registry import ToolRegistry, register_tool
from .preprocessing import calculate_dos1
from .processing import calculate_ndvi, calculate_ndwi, kmeans_classify
```

- [ ] **Step 4: Commit wiring**

```bash
git add engine/preprocessing/__init__.py engine/processing/__init__.py engine/__init__.py
git commit -m "refactor: wire up modular registrations in init files"
```

### Task 3: Update remaining tests and Agent Executor

**Files:**
- Modify: `tests/test_processing.py`
- Modify: `agent/executor.py`

- [ ] **Step 1: Update `tests/test_processing.py`**

```python
from engine.processing import calculate_ndvi, kmeans_classify
```

- [ ] **Step 2: Update `agent/executor.py`**

```python
        elif "ndvi" in p_lower or "vegetation" in p_lower or "crops" in p_lower:
            # ...
            res["code"] = (
                "# Educational Script: Normalized Difference Vegetation Index\n"
                "from engine.processing.indices.vegetation import calculate_ndvi\n\n"
                f"input_raster = '{sample_path}'\n"
                "output_ndvi = 'data/output_ndvi.tif'\n\n"
                "# Execute band algebra in background thread\n"
                "calculate_ndvi(input_raster, output_ndvi, red_band=1, nir_band=3)"
            )
        elif "ndwi" in p_lower or "water" in p_lower or "river" in p_lower:
            # ...
            res["code"] = (
                "# Educational Script: Normalized Difference Water Index\n"
                "from engine.processing.indices.water import calculate_ndwi\n\n"
                f"input_raster = '{sample_path}'\n"
                "output_ndwi = 'data/output_ndwi.tif'\n\n"
                "calculate_ndwi(input_raster, output_ndwi, green_band=2, nir_band=3)"
            )
        elif "kmeans" in p_lower or "classify" in p_lower or "unsupervised" in p_lower:
            # ...
            res["code"] = (
                "# Educational Script: K-Means Unsupervised Classification\n"
                "from engine.processing.classification.kmeans import kmeans_classify\n\n"
                "kmeans_classify(\n"
                f"    input_path='{sample_path}',\n"
                "    output_path='data/output_classification.tif',\n"
                "    bands='1,2,3',\n"
                "    clusters=5\n"
                ")"
            )
```

- [ ] **Step 3: Commit updates**

```bash
git add tests/test_processing.py agent/executor.py
git commit -m "refactor: update tests and agent executor to use new modular paths"
```

### Task 4: Delete old flat files and Run Tests

**Files:**
- Delete: `engine/_preprocessing.py`
- Delete: `engine/_processing.py`

- [ ] **Step 1: Delete old files**

Run: `rm engine/_preprocessing.py engine/_processing.py`

- [ ] **Step 2: Run all tests**

Run: `PYTHONPATH=. pytest -v`

- [ ] **Step 3: Commit final cleanup**

```bash
git rm engine/_preprocessing.py engine/_processing.py
git commit -m "refactor: complete modular architecture split and remove old flat files"
```
