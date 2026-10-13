# Reproducers & Oracles — R12-A Science Numerics

## Work Package Reproducers

### A02: Multi-band NoData / NaN / Inf
- **Failure mechanism**: A raster where Band 1 has NoData = -9999, Band 2 has NoData = 0 (where 0 is a valid measurement in Band 1). If only Band 1's sentinel is checked, pixels with Band 2 = 0 are retained as valid 0, producing distorted feature calculations.
- **Oracle**: Synthetic 2-band dataset where Band 1 sentinel is -9999 and Band 2 sentinel is 0. Per-band voiding must mark any pixel with invalid band as NoData (NaN).

### A03: Classification Dtype Truncation
- **Failure mechanism**: Model predicts class IDs > 255 or > 65535 in predict-only mode without full sidecar metadata.
- **Oracle**: Test predicting classes 256 and 65536. GeoTIFF output must write GDT_UInt16 / GDT_Int32 without truncation or clamping.

### A04: CRS Transformation Failures
- **Failure mechanism**: Polygon vertices cannot be transformed into the target raster's CRS (e.g. out of valid projection domain).
- **Oracle**: Call `extractTrainingData` with polygon with invalid coordinates or incompatible CRS. Must fail cleanly with an error message instead of silently falling back to original coordinates.

### A05: Raster Output Atomicity
- **Failure mechanism**: Calling `toGeoTIFF` directly on target file can overwrite or corrupt the file if interrupted or disk full.
- **Oracle**: Mid-operation cancellation or write failure leaves target path uncreated or unmodified.

### A06: Temporal Cube & Radiometric Vocab
- **Failure mechanism**: Sub-scene read exception caught by empty `catch (...)`; uint64 overflow on slice range.
- **Oracle**: Reading invalid slice returns clear error context; slice index overflow returns empty vector instead of bad allocation. Radiometric state parses `"gamma0"`.

### A09: Classifier Error Paths
- **Failure mechanism**: Feature count passed to Mahalanobis / MinDistance classifier does not match trained feature count, leading to out-of-bounds memory access.
- **Oracle**: Pass feature vector with mismatched dimensions -> rejected fail-closed with clear error.
