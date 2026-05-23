# Modular Remote Sensing Architecture Design

## 1. Overview
The current remote sensing engine organizes algorithms broadly by processing stage (e.g., `preprocessing.py`, `processing.py`). As the system grows to support multiple algorithms per stage (e.g., DOS1 vs. FLAASH for atmospheric correction), this flat structure becomes difficult to maintain.

This design introduces a highly modular, hierarchical directory structure. Algorithms will be grouped first by **stage** (preprocessing, processing) and then by **functional domain** (atmospheric, geometric, indices, classification, etc.).

## 2. Directory Structure

The `engine/` directory will be refactored into the following structure:

```text
engine/
├── core/                  # Core shared utilities
│   ├── reader.py          # I/O and Rasterio wrappers
│   ├── projection.py      # CRS and spatial transforms
│   └── base.py            # (Optional) Base classes/interfaces for algorithms
├── preprocessing/         # Stage: Data Preparation
│   ├── __init__.py
│   ├── atmospheric/
│   │   └── dos1.py        # DOS1 atmospheric correction
│   ├── geometric/
│   │   └── rectify.py     # GCP Polynomial rectification
│   └── radiometric/       # Placeholder for future calibration
├── processing/            # Stage: Feature Extraction & Analysis
│   ├── __init__.py
│   ├── indices/
│   │   └── vegetation.py  # NDVI, etc.
│   │   └── water.py       # NDWI, etc.
│   ├── classification/
│   │   └── kmeans.py      # K-Means clustering
│   └── pansharpening/
│   │   └── pca.py         # PCA pan-sharpening
└── registry.py            # Central ToolRegistry and @register_tool decorator
```

## 3. Component Details

### 3.1 `engine/core/`
This module will house foundational logic that does not belong to any specific remote sensing algorithm. Existing files like `reader.py` and `projection.py` will be moved here.

### 3.2 `engine/preprocessing/`
Submodules will categorize preprocessing algorithms.
*   **`atmospheric/dos1.py`**: Will contain `calculate_dos1` and its helper functions.
*   **`geometric/rectify.py`**: Will contain `calculate_polynomial_coeffs` and related warping wrappers.

### 3.3 `engine/processing/`
Submodules will categorize analytical algorithms.
*   **`pansharpening/pca.py`**: Will contain `pca_pansharpen_arrays` and related logic.
*   **`indices/` and `classification/`**: Placeholders for extracting existing index/classification logic into their own dedicated files.

### 3.4 Tool Registration
The `ToolRegistry` in `engine/registry.py` will remain the central hub. Each algorithm file (e.g., `dos1.py`) will import the `@register_tool` decorator and apply it to its public execution function. The `__init__.py` files within `preprocessing/` and `processing/` will need to import these specific modules to ensure the decorators are executed at runtime.

## 4. Refactoring Strategy (Implementation Plan)
1.  **Create Directories**: Create the new hierarchical directory structure within `engine/`.
2.  **Move Core Files**: Move `reader.py` and `projection.py` to `engine/core/`.
3.  **Refactor Preprocessing**: Break apart `engine/preprocessing.py` into `engine/preprocessing/atmospheric/dos1.py`, `engine/preprocessing/geometric/rectify.py`, and `engine/processing/pansharpening/pca.py`. Note that PCA pansharpening conceptually belongs in processing.
4.  **Update Imports**: Update all internal engine imports to reflect the new structure.
5.  **Update Tests**: Refactor the `tests/` directory to mirror the new engine structure (e.g., `tests/engine/preprocessing/atmospheric/test_dos1.py`), update all import paths in the tests, and ensure the test suite passes.
6.  **Update Agent**: Ensure `agent/executor.py` imports tools from the correct paths or relies entirely on the dynamic `ToolRegistry`.
7.  **Cleanup**: Remove the old, flat `preprocessing.py` and `processing.py` files.