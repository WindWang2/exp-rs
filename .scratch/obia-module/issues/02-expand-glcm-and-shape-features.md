# 02 — Expand GLCM Texture & Geometric Shape Descriptors

Type: research
Status: resolved
Blocked by:

## Question

Currently, `RsSegmentFeatures` only extracts 4 basic spectral statistics (`mean`, `stddev`, `min`, `max`) and 2 basic shape attributes (`area`, `shapeIndex`), lacking texture descriptors (Haralick GLCM: contrast, correlation, energy, homogeneity) and geometric shape features (compactness, convexity, rectangularity, bounding-box ratio, main-axis orientation).

How should Haralick GLCM texture calculations and extended geometric shape descriptors be designed within `RsSegmentFeatures` and `RsHierarchyFeatures`, how should gray-level quantization be configured for GLCM performance, and how will these new features expand the OpenCV `cv::Mat` feature matrix for object classification?

## Answer

1. Expanded `RsSegmentFeatures::SegmentStat` with GLCM texture metrics (`glcmContrast`, `glcmCorrelation`, `glcmEnergy`, `glcmHomogeneity`) and extended shape descriptors (`compactness`, `rectangularity`, `aspectRatio`).
2. Implemented 16-level gray-level quantized GLCM co-occurrence matrix calculation across horizontal and vertical neighbor pairs per segment per band.
3. Updated `toFeatureMatrix` to produce an expanded OpenCV `cv::Mat` feature matrix of dimension $N_{\text{features}} = N_{\text{bands}} \times 8 + 6$.

