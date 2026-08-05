# 05 — Hierarchical Feature Tree Selection Panel & Selective Feature Matrix Builder

Type: task
Status: resolved
Blocked by: 01, 02

## Question

OBIA segment feature extraction produces $8B + 6$ features (spectral, GLCM texture, shape descriptors). Including unneeded or redundant features increases dimensionality and noise.

How should `RsSegmentFeatures::toFeatureMatrix` accept a feature selection mask/struct (`RsFeatureSelection`), and how should `RsObiaMainWindow` expose a hierarchical Feature Selection Tree Panel (`QTreeWidget` dock) allowing users to check/uncheck feature groups (Spectral, Texture, Shape) and individual descriptors before model training & prediction?
