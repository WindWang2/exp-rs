# RS Preprocessing Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a foundational suite of remote sensing preprocessing algorithms (DOS1, PCA Pan-sharpening, GCP Rectification).

**Architecture:** Hybrid Python/C++. Python handles orchestration and I/O; C++ handles matrix transforms and resampling.

**Tech Stack:** Python, NumPy, Rasterio, Pybind11, Eigen (C++).

---

### Task 1: Implement DOS1 Atmospheric Correction

**Files:**
- Create: `engine/preprocessing.py`
- Create: `tests/test_preprocessing.py`

- [ ] **Step 1: Write failing test for DOS1**

```python
import numpy as np
import pytest
from engine.preprocessing import calculate_dos1

def test_calculate_dos1_basic():
    # Mock band with a "dark object" (haze) value of 10
    band = np.array([[10, 20], [30, 40]], dtype=np.uint8)
    # Expected: 10 is subtracted from all, 0 is the new dark object
    expected = np.array([[0, 10], [20, 30]], dtype=np.uint8)
    result = calculate_dos1_band(band)
    assert np.array_equal(result, expected)
```

- [ ] **Step 2: Implement DOS1 logic in Python**

```python
import numpy as np
import rasterio
import os

def calculate_dos1_band(band_data):
    dark_value = np.min(band_data)
    corrected = band_data.astype(np.float32) - dark_value
    return np.clip(corrected, 0, 255).astype(np.uint8)

def calculate_dos1(input_path: str, output_path: str) -> str:
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        data = src.read()
        
        corrected_data = np.zeros_like(data)
        for i in range(src.count):
            corrected_data[i] = calculate_dos1_band(data[i])
            
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, 'w', **profile) as dst:
        dst.write(corrected_data)
    return output_path
```

- [ ] **Step 3: Run test to verify passes**

Run: `pytest tests/test_preprocessing.py::test_calculate_dos1_basic -v`

- [ ] **Step 4: Commit**

```bash
git add engine/preprocessing.py tests/test_preprocessing.py
git commit -m "feat: add DOS1 atmospheric correction"
```

---

### Task 2: Implement GCP-based Rectification (Logic & Stub)

**Files:**
- Modify: `engine/preprocessing.py`
- Modify: `tests/test_preprocessing.py`

- [ ] **Step 1: Write failing test for GCP coefficient calculation**

```python
from engine.preprocessing import calculate_polynomial_coeffs

def test_rectify_coeffs():
    # Simple shift: x -> x+10, y -> y+5
    src_pts = np.array([[0, 0], [1, 0], [0, 1]])
    dst_pts = np.array([[10, 5], [11, 5], [10, 6]])
    coeffs_x, coeffs_y = calculate_polynomial_coeffs(src_pts, dst_pts, order=1)
    # Verify coefficients match expected shift
    assert coeffs_x[1] == 1.0 # x scale
    assert coeffs_x[0] == 10.0 # x shift
```

- [ ] **Step 2: Implement coefficient calculation using Least Squares**

```python
def calculate_polynomial_coeffs(src_pts, dst_pts, order=1):
    # Solve A * X = B for polynomial coefficients
    # For order 1: X = a0 + a1*x + a2*y
    num_pts = src_pts.shape[0]
    if order == 1:
        A = np.column_stack([np.ones(num_pts), src_pts[:, 0], src_pts[:, 1]])
    else:
        # 2nd order: a0 + a1*x + a2*y + a3*x^2 + a4*xy + a5*y^2
        A = np.column_stack([
            np.ones(num_pts), src_pts[:, 0], src_pts[:, 1],
            src_pts[:, 0]**2, src_pts[:, 0]*src_pts[:, 1], src_pts[:, 1]**2
        ])
    
    coeffs_x, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 0], rcond=None)
    coeffs_y, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 1], rcond=None)
    return coeffs_x, coeffs_y
```

- [ ] **Step 3: Run test and commit**

Run: `pytest tests/test_preprocessing.py::test_rectify_coeffs -v`

---

### Task 3: Implement C++ Warp Engine (Pybind11)

**Files:**
- Modify: `src/raster_ops.cpp`
- Modify: `CMakeLists.txt` (if needed for Eigen)

- [ ] **Step 1: Add Warp function to C++ bindings**

```cpp
// src/raster_ops.cpp
py::array_t<uint8_t> warp_image(py::array_t<uint8_t> input, int out_w, int out_h, py::array_t<double> coeffs_x, py::array_t<double> coeffs_y) {
    // 1. Get pointers to data
    // 2. Loop through output grid (i, j)
    // 3. Calculate source (x, y) using coefficients
    // 4. Perform Bilinear Interpolation
    // 5. Return result
}
```

- [ ] **Step 2: Implement Bilinear Interpolation kernel**

- [ ] **Step 3: Compile and verify bindings**

Run: `mkdir -p build && cd build && cmake .. && make`

- [ ] **Step 4: Commit**

---

### Task 4: Integrate PCA Pan-sharpening

**Files:**
- Modify: `engine/preprocessing.py`
- Modify: `src/raster_ops.cpp`

- [ ] **Step 1: Implement MS band resampling to Pan resolution (Python)**
- [ ] **Step 2: Add Eigen-based PCA kernel to C++**
- [ ] **Step 3: Coordinate PC1 replacement and Inverse PCA in Python**
- [ ] **Step 4: Verify with multi-band test image**
- [ ] **Step 5: Commit**

---

### Task 5: Register Tools and UI Integration

**Files:**
- Modify: `engine/preprocessing.py` (Registry calls)
- Modify: `gui/toolbox.py` (Ensure categories show up)

- [ ] **Step 1: Register tools in engine/preprocessing.py**
- [ ] **Step 2: Run application and verify tools appear in "Preprocessing" category**
- [ ] **Step 3: Final E2E test with Agent**
Ask Agent: "Run DOS1 atmospheric correction on sample_crops.tif"
- [ ] **Step 4: Commit**
