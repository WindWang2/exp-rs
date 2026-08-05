# 02 — Feature Scaling & Importance Selection Engine

Type: research
Status: resolved
Blocked by:

## Question

Raw OBIA feature matrices ($8B + 6$) contain features with vastly different scales (e.g. area $\in [1, 100000]$ vs. GLCM energy $\in [0.0, 1.0]$), causing scale dominance in distance-based and margin-based classifiers like SVM and ANN.

How should `RsFeatureScaler` (Z-score standardization / MinMax scaling) and feature selection (variance threshold / tree-based feature importance) be integrated into `RsObjectClassify` and `RsSegmentFeatures`, and how should scaling parameters be saved into the `.class.json` sidecar for predict-only execution?
