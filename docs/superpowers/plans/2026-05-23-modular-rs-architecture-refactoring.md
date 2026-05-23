# Modular Remote Sensing Architecture Refactoring Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the existing flat remote sensing engine into a highly modular, hierarchical directory structure separated by stage (preprocessing, processing) and functional domain (atmospheric, geometric, pansharpening).

**Architecture:** Moving files from `engine/` into `engine/core/`, `engine/preprocessing/`, and `engine/processing/`. Updating internal imports and tests. Removing old flat files.

**Tech Stack:** Python, pytest.

---

### Task 1: Create New Directory Structure and Move Core Files

**Files:**
- Create: `engine/core/__init__.py`
- Create: `engine/preprocessing/__init__.py`
- Create: `engine/preprocessing/atmospheric/__init__.py`
- Create: `engine/preprocessing/geometric/__init__.py`
- Create: `engine/processing/__init__.py`
- Create: `engine/processing/pansharpening/__init__.py`
- Create: `engine/processing/indices/__init__.py`
- Create: `engine/processing/classification/__init__.py`
- Modify: Move `engine/reader.py` to `engine/core/reader.py`
- Modify: Move `engine/projection.py` to `engine/core/projection.py`
- Test: Update tests to point to new core paths (if they explicitly test core).

- [ ] **Step 1: Create the directory tree**

Run: `mkdir -p engine/core engine/preprocessing/atmospheric engine/preprocessing/geometric engine/processing/pansharpening engine/processing/indices engine/processing/classification`

- [ ] **Step 2: Create empty `__init__.py` files for core and submodules**

Run: `touch engine/core/__init__.py engine/preprocessing/atmospheric/__init__.py engine/preprocessing/geometric/__init__.py engine/processing/pansharpening/__init__.py engine/processing/indices/__init__.py engine/processing/classification/__init__.py`

- [ ] **Step 3: Move core files**

Run: `mv engine/reader.py engine/core/reader.py`
Run: `mv engine/projection.py engine/core/projection.py`

- [ ] **Step 4: Update imports in core files**
If `reader.py` or `projection.py` import each other, update them. Currently, they don't seem to depend on each other heavily, but we need to ensure they work.

- [ ] **Step 5: Update GUI and Test imports for core files**

Change `gui/canvas.py` and `gui/layer_tree.py` (if applicable) and `tests/test_reader.py`:
Modify `gui/canvas.py`:
```python
# Change: from engine.reader import GeospatialReader
# Change: from engine.projection import CRSTransformer
from engine.core.reader import GeospatialReader
from engine.core.projection import CRSTransformer
```
Modify `main.py`:
```python
# Change: from engine.reader import GeospatialReader
from engine.core.reader import GeospatialReader
```
Modify `tests/test_reader.py`:
```python
# Change: from engine.reader import GeospatialReader
from engine.core.reader import GeospatialReader
```

- [ ] **Step 6: Verify Core tests pass**

Run: `PYTHONPATH=. pytest tests/test_reader.py -v`

- [ ] **Step 7: Commit**

```bash
git add engine/ gui/ main.py tests/test_reader.py
git commit -m "refactor: move core utilities to engine/core"
```

---

### Task 2: Extract Atmospheric Correction (DOS1)

**Files:**
- Create: `engine/preprocessing/atmospheric/dos1.py`
- Modify: `engine/preprocessing/__init__.py`

- [ ] **Step 1: Create `dos1.py` and move code from `engine/preprocessing.py`**

```python
# engine/preprocessing/atmospheric/dos1.py
import numpy as np
import rasterio
import os
from engine.registry import register_tool

def calculate_dos1_band(band_data, nodata=None, dark_value=None):
    if dark_value is None:
        if nodata is not None:
            masked_data = np.ma.masked_equal(band_data, nodata)
            if masked_data.count() == 0:
                return band_data
            dark_value = masked_data.min()
        else:
            dark_value = np.min(band_data)
        
    if np.issubdtype(band_data.dtype, np.floating):
        dtype_max = np.finfo(band_data.dtype).max
    else:
        dtype_max = np.iinfo(band_data.dtype).max
        
    corrected = band_data.astype(np.float32) - dark_value
    result = np.clip(corrected, 0, dtype_max).astype(band_data.dtype)
    if nodata is not None:
        result[band_data == nodata] = nodata
    return result

@register_tool(
    name="dos1_correction",
    label="DOS1 Atmospheric Correction",
    category="Preprocessing",
    description="Performs Dark Object Subtraction (DOS1) atmospheric correction on a raster image.",
    params=[
        {"name": "input_path", "label": "Input Raster", "type": "file"},
        {"name": "output_path", "label": "Output Raster", "type": "file"}
    ]
)
def calculate_dos1(input_path: str, output_path: str) -> str:
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        nodata = src.nodata
        
        dark_values = []
        for i in range(1, src.count + 1):
            band_min = None
            for _, window in src.block_windows():
                band_data = src.read(i, window=window)
                if nodata is not None:
                    masked = np.ma.masked_equal(band_data, nodata)
                    if masked.count() > 0:
                        curr_min = masked.min()
                        if band_min is None or curr_min < band_min:
                            band_min = curr_min
                else:
                    curr_min = np.min(band_data)
                    if band_min is None or curr_min < band_min:
                        band_min = curr_min
            dark_values.append(band_min if band_min is not None else 0)

        with rasterio.open(output_path, 'w', **profile) as dst:
            for _, window in src.block_windows():
                data = src.read(window=window)
                corrected_window = np.zeros_like(data)
                for i in range(src.count):
                    corrected_window[i] = calculate_dos1_band(data[i], nodata=nodata, dark_value=dark_values[i])
                
                dst.write(corrected_window, window=window)
                
    return output_path
```

- [ ] **Step 2: Update tests to point to new dos1 path**

Modify `tests/test_preprocessing.py`, `tests/test_dos1_advanced.py`, `tests/test_dos1_multiband.py`, `tests/test_dos1_uint16.py`, `agent/executor.py` (and any other files importing `calculate_dos1`) to import from `engine.preprocessing.atmospheric.dos1`.

Modify `tests/test_preprocessing.py` (Lines 1-6):
```python
import numpy as np
import pytest
import rasterio
import os
from engine.preprocessing.atmospheric.dos1 import calculate_dos1_band, calculate_dos1
```
*(Apply similar targeted edits to the other 3 test files and the agent executor script)*

- [ ] **Step 3: Run specific DOS1 tests**

Run: `PYTHONPATH=. pytest tests/test_preprocessing.py tests/test_dos1_advanced.py tests/test_dos1_multiband.py tests/test_dos1_uint16.py -v`
Note: The test file `tests/test_preprocessing.py` will fail on later tests (rectify, pca) because those haven't been moved yet. Ignore those specific failures for this step, or split the test file in this step. (Better to split the test file in the next step).

- [ ] **Step 4: Commit**

```bash
git add engine/preprocessing/ tests/ agent/executor.py
git commit -m "refactor: extract DOS1 atmospheric correction module"
```

---

### Task 3: Extract Geometric Correction (Rectify)

**Files:**
- Create: `engine/preprocessing/geometric/rectify.py`

- [ ] **Step 1: Create `rectify.py` and move code from `engine/preprocessing.py`**

```python
# engine/preprocessing/geometric/rectify.py
import numpy as np

def calculate_polynomial_coeffs(src_pts, dst_pts, order=1):
    num_pts = src_pts.shape[0]
    if order == 1:
        A = np.column_stack([np.ones(num_pts), src_pts[:, 0], src_pts[:, 1]])
    else:
        A = np.column_stack([
            np.ones(num_pts), src_pts[:, 0], src_pts[:, 1],
            src_pts[:, 0]**2, src_pts[:, 0]*src_pts[:, 1], src_pts[:, 1]**2
        ])
    
    coeffs_x, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 0], rcond=None)
    coeffs_y, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 1], rcond=None)
    return coeffs_x, coeffs_y
```

- [ ] **Step 2: Update test imports**

Modify `tests/test_preprocessing.py` around line 125:
```python
def test_rectify_coeffs():
    from engine.preprocessing.geometric.rectify import calculate_polynomial_coeffs
    # Simple shift: x -> x+10, y -> y+5
```

- [ ] **Step 3: Run test**

Run: `PYTHONPATH=. pytest tests/test_preprocessing.py::test_rectify_coeffs -v`

- [ ] **Step 4: Commit**

```bash
git add engine/preprocessing/geometric/ tests/test_preprocessing.py
git commit -m "refactor: extract geometric rectification module"
```

---

### Task 4: Extract PCA Pan-sharpening

**Files:**
- Create: `engine/processing/pansharpening/pca.py`

- [ ] **Step 1: Create `pca.py` and move code from `engine/preprocessing.py`**

```python
# engine/processing/pansharpening/pca.py
import numpy as np
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../build'))

def pca_pansharpen_arrays(ms_bands, pan_band):
    """
    ms_bands: shape (bands, height, width)
    pan_band: shape (height, width)
    """
    import raster_ops
    bands, h, w = ms_bands.shape
    
    # Flatten to (pixels, bands)
    ms_flat = ms_bands.reshape(bands, -1).T.astype(np.float32)
    
    # Forward PCA
    projected, evecs, mean = raster_ops.compute_pca(ms_flat)
    
    pan_flat = pan_band.reshape(-1).astype(np.float32)
    
    # Histogram matching PAN to PC1
    pan_mean = pan_flat.mean()
    pan_std = pan_flat.std()
    pc1_mean = projected[:, 0].mean()
    pc1_std = projected[:, 0].std()
    
    if pan_std != 0:
        pan_matched = (pan_flat - pan_mean) * (pc1_std / pan_std) + pc1_mean
    else:
        pan_matched = pan_flat
        
    projected[:, 0] = pan_matched
    
    # Inverse PCA: data = projected * evecs.T + mean
    inversed = np.dot(projected, evecs.T) + mean
    
    # Reshape back
    sharpened = inversed.T.reshape(bands, h, w)
    return sharpened.astype(ms_bands.dtype)
```

- [ ] **Step 2: Update test imports**

Modify `tests/test_preprocessing.py` around line 135:
```python
def test_pca_pansharpen():
    from engine.processing.pansharpening.pca import pca_pansharpen_arrays
    import rasterio
```

- [ ] **Step 3: Run test**

Run: `PYTHONPATH=. pytest tests/test_preprocessing.py::test_pca_pansharpen -v`

- [ ] **Step 4: Commit**

```bash
git add engine/processing/ tests/test_preprocessing.py
git commit -m "refactor: extract PCA pansharpening module"
```

---

### Task 5: Extract Processing Algorithms (NDVI, NDWI, KMeans)

**Files:**
- Create: `engine/processing/indices/vegetation.py`
- Create: `engine/processing/indices/water.py`
- Create: `engine/processing/classification/kmeans.py`
- Delete: `engine/preprocessing.py` (old)
- Delete: `engine/processing.py` (old)

- [ ] **Step 1: Check if old `processing.py` exists, if so move NDVI, NDWI, KMeans to new homes.**
If `engine/processing.py` contains `calculate_ndvi`, `calculate_ndwi`, and `kmeans_classify`, break them into:
- `engine/processing/indices/vegetation.py` (`calculate_ndvi`)
- `engine/processing/indices/water.py` (`calculate_ndwi`)
- `engine/processing/classification/kmeans.py` (`kmeans_classify`)
Make sure to keep `@register_tool` if present.

- [ ] **Step 2: Wire up Registration in `__init__.py` files**
For the toolbox registry to discover these, the package needs to import them.
Modify `engine/preprocessing/__init__.py`:
```python
from .atmospheric.dos1 import calculate_dos1
from .geometric.rectify import calculate_polynomial_coeffs
```
Modify `engine/processing/__init__.py`:
```python
from .pansharpening.pca import pca_pansharpen_arrays
# Optional: import indices and classification here if they exist
```

- [ ] **Step 3: Update remaining tests and Agent Executor**
Update `tests/test_processing.py` (if it exists) and `agent/executor.py` to point to the new paths:
```python
# agent/executor.py (Lines 83-93)
# Change: from engine import calculate_ndvi
# To: from engine.processing.indices.vegetation import calculate_ndvi
# Change: from engine import calculate_ndwi
# To: from engine.processing.indices.water import calculate_ndwi
# Change: from engine import kmeans_classify
# To: from engine.processing.classification.kmeans import kmeans_classify
```

- [ ] **Step 4: Delete the old, flat files**
Run: `rm -f engine/preprocessing.py engine/processing.py`

- [ ] **Step 5: Run full test suite**
Run: `PYTHONPATH=. pytest -v`

- [ ] **Step 6: Commit**
```bash
git add engine/ tests/ agent/
git commit -m "refactor: complete modular architecture split and remove old flat files"
```
