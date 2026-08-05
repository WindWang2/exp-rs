# Research Report: Expanding GLCM Texture & Geometric Shape Descriptors in `exp-rs` OBIA Module

**Ticket ID:** 02 — Expand GLCM Texture & Geometric Shape Descriptors  
**Target Module:** `src/analysis/segmentation/` (`RsSegmentFeatures`, `RsHierarchyFeatures`)  
**Date:** August 2026  

---

### Executive Summary

Currently, `RsSegmentFeatures` in `exp-rs` extracts basic spectral statistics (`mean`, `stddev`, `min`, `max` across $B$ bands) and basic shape attributes (`area`, `perimeter`, `shapeIndex`), yielding $4B + 3$ feature columns in the OpenCV `cv::Mat` feature matrix.

This report presents a complete mathematical, algorithmic, and architectural specification for expanding `RsSegmentFeatures` with:
1. **4 Haralick Gray-Level Co-occurrence Matrix (GLCM) Texture Descriptors** (Contrast, Correlation, Energy, Homogeneity) per spectral band, computed over horizontal and vertical neighbor pairs on 16-level quantized segment bounding boxes.
2. **3 Extended Geometric Shape Descriptors** (Compactness $P^2 / 4\pi A$, Rectangularity $A / A_{\text{bbox}}$, Bounding Box Aspect Ratio).
3. **API and Feature Matrix Expansion**, bringing total feature dimensions to $N_{\text{features}} = 8B + 6$.

---

### Key Formulas & Definitions

#### 1. Haralick GLCM Texture Features (Haralick et al., 1973):
- **Contrast**: $\sum_{i, j} (i - j)^2 p(i, j)$
- **Correlation**: $\sum_{i, j} \frac{(i - \mu_i)(j - \mu_j) p(i, j)}{\sigma_i \sigma_j}$
- **Energy (ASM)**: $\sum_{i, j} p(i, j)^2$
- **Homogeneity (IDM)**: $\sum_{i, j} \frac{p(i, j)}{1 + |i - j|}$

#### 2. Extended Geometric Shape Features:
- **Compactness**: $\frac{P^2}{4 \pi A}$
- **Rectangularity**: $\frac{A}{W_{\text{bbox}} \times H_{\text{bbox}}}$
- **Aspect Ratio**: $\frac{\max(W_{\text{bbox}}, H_{\text{bbox}})}{\min(W_{\text{bbox}}, H_{\text{bbox}})}$

---

### Matrix Dimensions ($N_{\text{features}}$)
For $B$ spectral bands:
$$N_{\text{features}} = 8B + 6$$
For 1 band: 14 columns. For 4 bands: 38 columns.
