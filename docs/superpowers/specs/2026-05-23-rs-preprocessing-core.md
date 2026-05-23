# Design Spec: RS Preprocessing Core (Antigravity RS)

**Status:** APPROVED
**Date:** 2026-05-23
**Topic:** Remote Sensing Preprocessing & Calibration Suite

## 1. Overview
Implement a foundational suite of remote sensing preprocessing algorithms required for advanced data analysis. This module focuses on correction, enhancement, and rectification using a high-performance Hybrid (Python/C++) architecture.

## 2. Algorithms & Methods

### 2.1. Atmospheric Correction: DOS1 (Dark Object Subtraction)
- **Purpose:** Remove path radiance (haze) from multispectral imagery.
- **Method:** 
    1. Scan each band for its minimum pixel value (the "Dark Object").
    2. Subtract this value from every pixel in the band.
    3. Clamp resulting values to $[0, \text{max\_dtype}]$.
- **Implementation:** Pure Python (NumPy).

### 2.2. PCA-based Pan-sharpening
- **Purpose:** Combine low-resolution multispectral data with high-resolution panchromatic data.
- **Method:**
    1. Resample MS bands to Pan resolution.
    2. Transform MS bands into Principal Components (PCs) using Covariance/Eigen-decomposition.
    3. Histogram match the Pan image to the PC1 (first component).
    4. Replace PC1 with the matched Pan image.
    5. Reverse transform to reconstruct high-resolution MS bands.
- **Implementation:** Hybrid. C++ for Eigen-decomposition and matrix multiplication.

### 2.3. GCP-based Polynomial Rectification
- **Purpose:** Geometrically correct images using ground control points (GCPs).
- **Method:**
    1. Input: Set of points $(x, y) \to (X, Y)$ (image $\to$ map coordinates).
    2. Calculate polynomial coefficients (1st or 2nd order) via Least Squares.
    3. Perform backward warping (Inverse Mapping) for each pixel in the target grid.
    4. Resample pixel values using Bilinear Interpolation.
- **Implementation:** Hybrid. Python for coefficient calculation; C++ for the warping/interpolation engine.

## 3. Architecture

### 3.1. File Structure
- `engine/preprocessing.py`: Orchestrator and registry.
- `src/raster_ops.cpp`: C++ kernels for PCA and Warping.
- `tests/test_preprocessing.py`: Verification suite.

### 3.2. Registry Integration
Each tool will be registered in `ToolRegistry` with the following categories:
- `calculate_dos1`: Preprocessing -> Atmospheric Correction
- `pan_sharpen_pca`: Preprocessing -> Enhancement
- `rectify_polynomial`: Preprocessing -> Geometric Correction

## 4. Success Criteria
1. **DOS1 Accuracy:** Pixel values are shifted correctly without artifacts.
2. **Pan-sharpening Visuals:** High resolution is maintained while preserving spectral ratios.
3. **Rectification Performance:** A $512 \times 512$ image is warped in $< 200$ms using the C++ core.
4. **Agent Compatibility:** The AI Agent can invoke `rectify_polynomial` via a JSON command containing GCP lists.
