# Research Report: Feature Scaling & Selection Engine for OBIA Classification

**Ticket ID:** 02 — Feature Scaling & Importance Selection Engine  
**Target Module:** `src/analysis/classification/` (`RsFeatureScaler`, `RsClassifierBackend`)  
**Date:** August 2026  

---

### Executive Summary

In OBIA classification, feature matrices contain heterogenous feature dimensions ($N_{\text{features}} = 8B + 6$), ranging from spectral reflectance ($[0.0, 1.0]$ or $[0, 255]$), GLCM textures ($[0.0, 1.0]$), area ($[1, 100000]$), to shape index ($[1.0, 5.0]$). Large-magnitude features like area severely skew distance-based classifiers (SVM, ANN/MLP) if unscaled.

This report outlines the design and C++ implementation of `RsFeatureScaler` supporting:
1. **Z-score Standardization**: $z = \frac{x - \mu}{\sigma + \epsilon}$
2. **MinMax Scaling**: $x' = \frac{x - x_{\min}}{(x_{\max} - x_{\min}) + \epsilon}$
3. **Sidecar Persistence**: Storing mean/stddev vectors in `.class.json` under `"feature_scaler"` metadata for predict-only execution.

---

### Mathematical Formulas

#### 1. Z-Score Standardization
For column $j$ of feature matrix $X$:
$$\mu_j = \frac{1}{N} \sum_{i=1}^N X_{i, j}$$
$$\sigma_j = \sqrt{\frac{1}{N} \sum_{i=1}^N (X_{i, j} - \mu_j)^2}$$
$$X'_{i, j} = \frac{X_{i, j} - \mu_j}{\sigma_j + 10^{-8}}$$

#### 2. Sidecar `.class.json` Schema
```json
{
  "feature_scaler": {
    "type": "zscore",
    "mean": [0.25, 0.12, 1250.0, 1.8],
    "stddev": [0.05, 0.02, 450.0, 0.4]
  }
}
```
