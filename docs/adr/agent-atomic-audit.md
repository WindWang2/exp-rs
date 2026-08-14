# ADR 0121 / Agent Atomic Audit: Algorithm Atomicity Audit & Primitive Decomposition

- Status: Accepted (2026-08-14)
- Scope: `src/operators`, `src/processing/framework`, `src/agent`

## 1. Context & Motivation

In the autonomous Agent and workflow ecosystem (`exp-rs`), algorithms are exposed to LLMs, DAG pipelines, CLI, and GUI through the canonical catalog `AtomicAlgorithmRegistry`.

A core architectural principle of agent-ready systems is:
> **One Operator = One Meaningful Processing Step**

When operators act as monolithic selectors (e.g., combining multiple disparate methods behind a single `method` or `index` string enum parameter), several critical problems arise:
1. **Agent Hallucination & Planning Ambiguity**: LLMs cannot easily reason about the required inputs/outputs or resource footprints of specific algorithms when they are hidden behind a multi-mode switch. For example, QUAC operates on all bands jointly and is full-raster in memory, whereas DOS1/DOS2 operate on a single band and stream in tiles.
2. **Schema Inaccuracy**: Different methods within a selector often require different parameters (e.g., `airmass` is only valid for DOS2; `blue` is required for EVI but unused for NDVI; `swir` is required for NDBI/MNDWI). A monolithic schema must mark these as optional or union them, losing strict parameter validation.
3. **Pipeline Inflexibility**: Agents and workflow DAGs cannot compose or inspect intermediate steps when operations are bundled together.
4. **Tool Discovery Overhead**: An agent searching for "NDVI" or "Dark Object Subtraction" has to inspect generic tools like `rs:spectral_index` or `rs:atmospheric_correction` and figure out internal selector values instead of finding direct, high-leverage atomic tools.

---

## 2. Comprehensive Audit of `src/operators`

Every operator across `src/operators/rs`, `src/operators/gdal`, `src/operators/opencv`, and `src/operators/otb` was inspected for selector parameters (`method`, `mode`, `strategy`, `algorithm`, `type`, `operation`, `index`, `product`, `metric`, `filter`, `resampling`).

### 2.1 Audit Classification Matrix

| Operator ID | Selector Parameter(s) | Category | Current State & Recommendation |
| :--- | :--- | :--- | :--- |
| `rs:atmospheric_correction` | `method`: `dn_to_radiance`, `dos1`, `dos2`, `quac` | **Monolithic Selector (Priority 1)** | **Decompose**: Split into `rs:dn_to_radiance`, `rs:atmospheric_dos1`, `rs:atmospheric_dos2`, `rs:atmospheric_quac`. Retain `rs:atmospheric_correction` as legacy facade. |
| `rs:spectral_index` | `index`: `NDVI`, `EVI`, `SAVI`, `NDWI`, `NDBI`, `MNDWI` | **Monolithic Selector (Priority 2)** | **Decompose**: Split into dedicated atomic primitives `rs:ndvi`, `rs:evi`, `rs:ndwi`, `rs:savi`, `rs:ndbi`, `rs:mndwi`. Retain `rs:spectral_index` as legacy facade. |
| `rs:change_detection` | `method`: `difference`, `normalized_difference`, `ratio`, `cva`, `mad` | **Decomposed (ADR 0120)** | **Complete**: Split into `rs:change_difference`, `rs:change_normalized_difference`, `rs:change_ratio`, `rs:change_cva`, `rs:change_mad` + `rs:threshold_raster`. `rs:change_detection` retained as facade (`facadeOf`). |
| `rs:image_fusion` | `method`: `linear`, `brovey`, `pca`, `ihs`, `gram_schmidt` | **Decomposed (ADR 0120)** | **Complete**: Split into `rs:fusion_linear`, `rs:fusion_brovey`, `rs:fusion_pca`, `rs:fusion_ihs`, `rs:fusion_gram_schmidt`. `rs:image_fusion` retained as facade (`facadeOf`). |
| `rs:terrain_analysis` | `product`: `slope`, `aspect`, `hillshade`, `roughness`, `tri`, `tpi` | **Monolithic Selector (Candidate)** | **Candidate**: Slope, Aspect, Hillshade, etc., compute distinct topographic derivatives. Recommended for future decomposition (`rs:terrain_slope`, `rs:terrain_aspect`, etc.). |
| `rs:sam_classify` | `metric`: `sam`, `sid` | **Dual Metric (Candidate)** | **Candidate**: Spectral Angle Mapper vs Spectral Information Divergence. Can remain unified or split to `rs:sam_classify` / `rs:sid_classify`. |
| `rs:supervised_classification` | `method`: `svm`, `random_forest`, `mlp`, `knn`, `normal_bayes` | **Multi-Classifier (Candidate)** | **Candidate**: Different ML classifiers. Can be decomposed in future phases. |
| `rs:obia_classify` | `segmentMethod`, `method` | **Compound Pipeline (Candidate)** | **Candidate**: Combines segmentation + classification. Reusable primitives `rs:obia_segment` + `rs:supervised_classification` preferred. |
| `rs:radiometric_calibration` | `unit`: `radiance`, `toa_reflectance`, `brightness_temperature` | **Unit Normalization** | **Atomic**: Single sensor calibration process targeting physical units. |
| `rs:band_math` | Expression string | **True Atomic** | General-purpose pixel math expression evaluator. |
| `rs:spectral_unmixing` | Method: LSU | **True Atomic** | Linear spectral unmixing. |
| `rs:rx_anomaly` | N/A | **True Atomic** | Reed-Xiaoli anomaly detector. |
| `rs:continuum_removal` | N/A | **True Atomic** | Continuum removal curve normalization. |
| `rs:spectral_resample` | N/A | **True Atomic** | Spectral resampling to target sensor response functions. |
| `rs:endmember_extraction` | N/A | **True Atomic** | Pixel Purity Index (PPI) endmember extraction. |
| `rs:threshold_raster` | `thresholdMethod`: `manual`, `otsu`, `percentile`, `statistical` | **True Atomic** | Single-step thresholding & binarization step; method is algorithm configuration. |
| `rs:post_classification_change` | N/A | **True Atomic** | Discrete cross-tabulation transition matrix. |
| `rs:qa_mask` / `rs:apply_mask` | `source`, `mask` | **True Atomic** | Quality flag extraction & mask application. |
| `rs:pca` / `rs:mnf` | N/A | **True Atomic** | Dimensionality reduction / orthogonalization. |
| `rs:mosaic` | N/A | **True Atomic** | Spatial stitching and blending. |
| `rs:landsat_import` / `rs:sentinel2_import` / `rs:modis_import` / `rs:modis_georeference` | N/A | **True Atomic** | Ingestion & georeferencing primitives. |
| `rs:kmeans_classification` / `rs:majority_filter` / `rs:recode` / `rs:infer` | N/A | **True Atomic** | Standalone image processing operations. |
| `gdal:clip` / `gdal:reproject` / `gdal:orthorectification` / `gdal:polygonize` | `resampling` (nearest, bilinear, cubic) | **True Atomic** | Core spatial operations. Resampling is an interpolation parameter, not a separate pipeline step. |
| `opencv:*` (`gaussian_blur`, `mean_blur`, `median_blur`, `sobel`, `canny`, `laplacian`) | N/A | **True Atomic** | Distinct filtering primitives. |
| `otb:segmentation` / `otb:svm_classification` / `otb:compute_images_statistics` | `filter` (meanshift, watershed, mProfiles) | **Tool Wrapper** | External OTB CLI wrapper. |

---

## 3. Detailed Split Architecture for Atmospheric Correction & Spectral Indices

### 3.1 Atmospheric Correction (`rs:atmospheric_correction`)

#### Current Problem:
`rs:atmospheric_correction` mixes 4 fundamentally different algorithms:
1. `dn_to_radiance`: Linear calibration $L = \text{gain} \cdot \text{DN} + \text{bias}$ (radiometric state transition `raw` $\to$ `radiance`).
2. `dos1`: Dark object subtraction with zero-reflectance dark pixel assumption (radiometric state transition $\to$ `surface_reflectance`).
3. `dos2`: Atmospheric correction incorporating cosine solar zenith and atmospheric transmittance (requires `airmass`).
4. `quac`: Quick Atmospheric Correction — multi-band, scene-statistics-based hyperspectral/multispectral reflectance retrieval operating on all bands jointly (full-raster memory policy).

#### Atomic Decomposition:
- **`rs:dn_to_radiance`**:
  - Parameters: `input`, `output`, `band` (default 1), `metadata_path`, `gain`, `bias`.
  - Radiometric output state: `radiance`.
  - Memory policy: `Streaming`.
- **`rs:atmospheric_dos1`**:
  - Parameters: `input`, `output`, `band` (default 1), `metadata_path`, `gain`, `bias`.
  - Radiometric output state: `surface_reflectance`.
  - Memory policy: `MultiPassStreaming`.
- **`rs:atmospheric_dos2`**:
  - Parameters: `input`, `output`, `band` (default 1), `metadata_path`, `gain`, `bias`, `airmass` (default 1.0).
  - Radiometric output state: `surface_reflectance`.
  - Memory policy: `MultiPassStreaming`.
- **`rs:atmospheric_quac`**:
  - Parameters: `input`, `output`.
  - Radiometric output state: `surface_reflectance`.
  - Memory policy: `FullRaster`.

All atomic operators share the underlying C++ computational kernels (`AtmosphericCorrection::processFile` and `AtmosphericCorrection::processFileMultiBand`) and metadata loading helpers without duplicating code.

---

### 3.2 Spectral Indices (`rs:spectral_index`)

#### Current Problem:
`rs:spectral_index` accepts `index: NDVI | EVI | SAVI | NDWI | NDBI | MNDWI` with a union parameter schema (`nir`, `red`, `green`, `blue`, `swir`). An LLM invoking NDVI is exposed to irrelevant SWIR/Blue parameters, and cannot immediately see that EVI requires Blue while NDVI only requires NIR and Red.

#### Atomic Decomposition:
- **`rs:ndvi`**:
  - Purpose: Normalized Difference Vegetation Index: $(\text{NIR} - \text{Red}) / (\text{NIR} + \text{Red})$
  - Parameters: `input`, `output`, optional `nir`, optional `red`.
- **`rs:evi`**:
  - Purpose: Enhanced Vegetation Index: $2.5 \cdot (\text{NIR} - \text{Red}) / (\text{NIR} + 6 \cdot \text{Red} - 7.5 \cdot \text{Blue} + 1)$
  - Parameters: `input`, `output`, optional `nir`, optional `red`, optional `blue`.
- **`rs:ndwi`**:
  - Purpose: Normalized Difference Water Index: $(\text{Green} - \text{NIR}) / (\text{Green} + \text{NIR})$
  - Parameters: `input`, `output`, optional `green`, optional `nir`.
- **`rs:savi`**:
  - Purpose: Soil-Adjusted Vegetation Index: $((\text{NIR} - \text{Red}) / (\text{NIR} + \text{Red} + 0.5)) \cdot 1.5$
  - Parameters: `input`, `output`, optional `nir`, optional `red`.
- **`rs:ndbi`**:
  - Purpose: Normalized Difference Built-up Index: $(\text{SWIR} - \text{NIR}) / (\text{SWIR} + \text{NIR})$
  - Parameters: `input`, `output`, optional `swir`, optional `nir`.
- **`rs:mndwi`**:
  - Purpose: Modified Normalized Difference Water Index: $(\text{Green} - \text{SWIR}) / (\text{Green} + \text{SWIR})$
  - Parameters: `input`, `output`, optional `green`, optional `swir`.

All atomic index operators share the unified band role resolver, multi-band reader, `SpectralIndices::*` math kernels, and GeoTIFF writer.

---

## 4. Facade Preservation & `facadeOf` Metadata Contract

Backward compatibility is guaranteed 100%:
1. **Legacy Facade Preservation**: `rs:atmospheric_correction` and `rs:spectral_index` remain fully functional and registered in `RSOperatorRegistry` and `AtomicAlgorithmRegistry`.
2. **Bidirectional `facadeOf` Linking**:
   - Facade operator metadata specifies:
     - `rs:atmospheric_correction` $\to$ `meta["facadeOf"] = "rs:dn_to_radiance,rs:atmospheric_dos1,rs:atmospheric_dos2,rs:atmospheric_quac"`
     - `rs:spectral_index` $\to$ `meta["facadeOf"] = "rs:ndvi,rs:evi,rs:ndwi,rs:savi,rs:ndbi,rs:mndwi"`
   - Atomic primitive metadata specifies:
     - Atmospheric primitives $\to$ `meta["facadeOf"] = "atmospheric_correction"`
     - Spectral index primitives $\to$ `meta["facadeOf"] = "spectral_index"`
3. **No Numerical Drift**: The atomic operators and legacy facades call the identical mathematical implementation.

---

## 5. Verification Matrix

| Verification Target | Test Suite | Pass Criteria |
| :--- | :--- | :--- |
| Operator Registration | `test_atomic_registry_contract` | All new atomic IDs and legacy facades are registered in `AtomicAlgorithmRegistry` |
| `facadeOf` Contract | `test_atomic_registry_contract` | Metadata contains valid bidirectional `facadeOf` references |
| Parameter Validation | `test_schema_validator` / `test_atomic_registry_contract` | Precise schemas for each atomic operator accept valid parameters and reject invalid types |
| Numerical Equivalence | `test_rs_operators` | Atomic execution produces bitwise / floating-point identical output to legacy facade |
| Pipeline Composition | `test_atomic_registry_contract` | Composition DAGs (e.g. `dn_to_radiance` $\to$ `atmospheric_dos1` $\to$ `ndvi` $\to$ `threshold_raster`) validate cleanly |
