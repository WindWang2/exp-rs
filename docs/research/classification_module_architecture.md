# Research Report: Architecture & ML Engine of the Classification Module (`/research`)

**Repository:** `exp-rs` (SICNU GEO RS)  
**Primary Source Scope:** `src/analysis/classification/`, `src/operators/rs/`, `src/app/classify/`, `tests/test_classifier_*.cpp`  
**Date:** 2026-08-04  
**Author:** AI Pair Programmer (DeepMind Engineering Agent)  

---

## Executive Summary

The **Classification Module** in `exp-rs` provides a high-performance, tiled raster machine learning engine for remote sensing applications. It seamlessly unifies **unsupervised clustering** (K-Means with Hungarian relabeling), **supervised classification** (SVM, Normal Bayes, Random Forest), and **OBIA (Object-Based Image Analysis)** under a single `RsClassificationPipeline` abstraction.

---

## 1. Primary Source Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Classification Architecture                     │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│   RsSupervisedClassificationOperator / RsKMeansOperator                │
│                           │                                            │
│                           ▼                                            │
│                RsClassificationPipeline                                │
│          ┌────────────────┼────────────────┐                           │
│          ▼                ▼                ▼                           │
│    RsPixelRasterizer   RsFeatureScaler   RsClassifierBackend           │
│    (ROI Polygon ──►    (Z-Score /        ├── RsClassifierKMeans        │
│     Train Sample)       MinMax)          ├── RsClassifierSVM           │
│                                          └── RsClassifierNormalBayes   │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘
```

### Primary Sources
- **Pipeline Coordinator**: [`src/analysis/classification/rs_classification_pipeline.h`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_classification_pipeline.h) & [`.cpp`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_classification_pipeline.cpp)
- **Backend Factory**: [`src/analysis/classification/rs_classifier_backend_factory.h`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_classifier_backend_factory.h) & [`.cpp`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_classifier_backend_factory.cpp)
- **Hungarian Relabeling**: [`src/analysis/classification/rs_hungarian_assignment.h`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_hungarian_assignment.h) & [`.cpp`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_hungarian_assignment.cpp)
- **ROI Rasterizer**: [`src/analysis/classification/rs_pixel_rasterizer.h`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_pixel_rasterizer.h) & [`.cpp`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_pixel_rasterizer.cpp)
- **Accuracy Assessment**: [`src/analysis/classification/rs_accuracy_assessment.h`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_accuracy_assessment.h) & [`.cpp`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_accuracy_assessment.cpp)

---

## 2. Key Components & Implementation Mechanics

### A. Tiled Memory-Bounded Pipeline (`RsClassificationPipeline`)
- **Tiled Execution**: Processes large remote sensing rasters in memory-bounded horizontal/vertical tiles without allocating full multi-gigabyte band matrices in RAM.
- **Ignore Pixel Protocol**: Supports NoData masking, zero-band masking, and optional custom background value exclusions via [`RsPixelIgnoreOptions`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_pixel_ignore_options.h).

### B. Unsupervised K-Means & Hungarian Assignment
- **Automatic Cluster Relabeling**: In unsupervised K-Means, cluster IDs ($0 \dots K-1$) are arbitrary. The [`RsHungarianAssignment`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_hungarian_assignment.h) algorithm solves the maximum weight bipartite matching problem to automatically map unsupervised cluster indices to ground-truth class IDs.
- **Backend Verification**: Verified via Catch2 test suite [`tests/test_classifier_kmeans.cpp`](file:///home/kevin/projects/exp-rs/tests/test_classifier_kmeans.cpp).

### C. Supervised Classifiers & Training Pipeline
- **Supported Classifiers**:
  1. `RsClassifierKMeans`: Unsupervised K-Means clustering.
  2. `RsClassifierSVM`: Support Vector Machine (RBF, Linear, Polynomial kernels).
  3. `RsClassifierNormalBayes`: Normal Bayes classifier.
- **ROI Data Extraction**: `RsPixelRasterizer` overlays polygon ROI samples (`RsRoiCollection`) onto input multi-band rasters to extract feature vectors for training and cross-validation (`RsCrossValidation`).

### D. Accuracy Assessment Metrics (`RsAccuracyAssessment`)
- Computes **Confusion Matrix**, **Overall Accuracy (OA)**, **User's Accuracy (UA)**, **Producer's Accuracy (PA)**, and **Kappa Coefficient ($\kappa$)**.

---

## 3. Operator Integration & Seam Discipline

- **`rs:supervised_classification`**: Exposed via [`RsSupervisedClassificationOperator`](file:///home/kevin/projects/exp-rs/src/operators/rs/rs_supervised_classification_operator.h#L38-L45). Accepts training ROI definitions, fits feature scalers, trains the selected classifier backend, and streams out the classified thematic map.
- **`rs:kmeans_classification`**: Exposed via [`RsKMeansOperator`](file:///home/kevin/projects/exp-rs/src/operators/rs/rs_kmeans_operator.h#L30-L35). Performs unsupervised clustering with customizable cluster counts and max iterations.

---

## 4. Verification & Unit Test Suite Coverage

| Test Target | Primary Test File | Assertions / Coverage Focus |
|---|---|---|
| **K-Means & Hungarian Remap** | [`tests/test_classifier_kmeans.cpp`](file:///home/kevin/projects/exp-rs/tests/test_classifier_kmeans.cpp) | Cluster ID optimal matching & convergence |
| **SVM Backend** | [`tests/test_classifier_svm.cpp`](file:///home/kevin/projects/exp-rs/tests/test_classifier_svm.cpp) | Training, prediction accuracy, model export |
| **Normal Bayes** | [`tests/test_classifier_normalbayes.cpp`](file:///home/kevin/projects/exp-rs/tests/test_classifier_normalbayes.cpp) | Probabilistic class boundary decision |
| **Full Pipeline E2E** | [`tests/test_classification_e2e.cpp`](file:///home/kevin/projects/exp-rs/tests/test_classification_e2e.cpp) | End-to-end raster ROI extraction to thematic GeoTIFF |

---

## 5. Conclusion & Recommendations

1. **Architecture Strength**: The separation of `RsClassifierBackend` from `RsClassificationPipeline` provides exceptional locality—adding a new machine learning algorithm (e.g. XGBoost or Deep Learning ONNX) requires implementing only `RsClassifierBackend` without altering raster I/O or tiling.
2. **Performance**: Memory footprint remains constant regardless of raster size due to tile-by-tile streaming.
