# Baseline Audit — master @ 93a7fb0bbd (2026-09-07)

Facts below are read from code at the baseline SHA. Paths are worktree-relative.

## 1. Execution architecture (verified)

- Seam: `UI → TaskCenter → JobEngine → RSOperator → kernel` (PROJECT.md
  "Contracts"; enforced by `tests/test_ui_task_center_contract.cpp`).
- Operator framework: `src/operators/framework/` — `rs_operator.h` (JSON
  params/result, `RSOperatorMemoryPolicy` incl. `Streaming`),
  `rs_operator_registry`, `rs_operator_context`, `rs_operator_error` (typed),
  `rs_schema`, `rs_json_params`, `rs_progress_callback`,
  `rs_operation_logger`, `artifact_digest`.
- Catalog registration: `src/operators/rs/rs_operators_init.cpp` (~90 `rs:`
  ids). Other families: `gdal:` (clip, orthorectification, pansharpen,
  polygonize, reproject), `opencv:` (canny/blurs/edge ops), `otb:`
  (bundle_to_perfect_sensor, compute_images_statistics,
  meanshift_segmentation, svm_classification).
- Streaming infrastructure: `src/processing/gdal/` — `gdal_block_stream.h`
  (256-px tiles + halo), `gdal_multiband_block_stream.h`,
  `gdal_window_read.h`, `gdal_dataset_wrapper.h` (`readBandMasked`),
  `gdal_grid_compat.h` (`gridFromDataset`/`compareGrids` typed blocking
  issues), `gdal_safe_call.h`.
- Model runtime 4.0: `src/operators/runtime/` (single execution seam
  `rs:infer/segment/detect/embedding`), `src/operators/framework/` catalog.

## 2. Shared scientific primitives (as they exist at baseline)

| Primitive | Owner today | Notes |
|---|---|---|
| NoData sentinel/validity | `src/processing/algorithms/nodata_utils.h` | exact float-cast match; NaN always invalid. Policy: `docs/processing/nodata-and-statistics.md` |
| Grid compatibility | `src/processing/gdal/gdal_grid_compat.h` | typed blocking issues; unreferenced-pair fallback documented |
| Variance conventions | `temporal::stats::WelfordAccumulator`, `OnlineRegression` (temporal/), `MathUtils::computeStats*` | population vs sample table in policy doc |
| Histogram | per-file: `image_enhancement*.h`, `change_detection.h`, `threshold_raster`, `sar_change`, `atmospheric_correction`, `feature_normalize`, `band_tools` | **duplicated semantics — Milestone A consolidation target** |
| Percentile/quantile | ad hoc in the same files | no shared interpolation contract |
| Convolution | `image_enhancement` (spatial filter), `sar_speckle` (own windows), `opencv:` family | no shared edge-policy primitive |
| Morphology | `qa_mask.h`, `threshold_raster`, `change_detection` (cleanup bits), OpenCV wrappers | no shared primitive |
| Connected components | `rs_flood_fill` (analysis), OBIA segmentation internals | no shared primitive |
| Distance transform | — | absent |
| Window/halo policy | implicit in `gdal_block_stream` (halo) + per-kernel | no documented edge-policy contract enum |

## 3. Family coverage snapshot at baseline

### Optical / radiometric
- Have: `rs:radiometric_calibration`, `rs:dn_to_radiance`,
  `rs:atmospheric_correction` (+ `dos1/dos2/quac` aliases), `rs:qa_mask`,
  `rs:image_enhancement`, `rs:contrast_stretch`, imports
  (S2/Landsat/MODIS) stamping `SICNU_RADIOMETRIC_STATE`.
- Missing: topographic correction (C/Minnaert/SCS+C), haze-optimal
  estimation variants, spectral derivatives, reflectance domain validation
  surface (partial), cloud/shadow post-processing beyond qa_mask.

### Spectral
- Have: `rs:spectral_index` (+ ndvi/ndwi/mndwi/ndbi/evi/savi +
  aliases), `rs:band_ratio`, `rs:band_math`, `rs:continuum_removal`,
  `rs:pca`, `rs:mnf`, `rs:sam_classify`, `rs:spectral_unmixing`,
  `rs:endmember_extraction` (PPI), `rs:rx_anomaly`,
  `rs:spectral_resample`, `rs:feature_select/normalize/stack`.
- Missing: SID as callable primitive (SID math exists in sam test coverage —
  audit), matched filter, ACE, MNF noise estimation audit, constrained
  unmixing modes audit, index families (MSAVI/ARVI/GNDVI/NDMI/NBR/NDRE/
  NDSI/burn/urban/built-up).

### SAR
- Have: `rs:sar_calibrate`, `rs:sar_backscatter`, `rs:sar_speckle`
  (lee/enhanced_lee/frost/kuan/gamma_map/refined_lee/multitemporal —
  `sar_speckle.h`), `rs:sar_texture` (GLCM: contrast, dissimilarity,
  homogeneity, energy, asm, entropy, mean, stddev, correlation),
  `rs:sar_ratio`, `rs:sar_change`, `rs:sar_terrain_correction`,
  `rs:sar_terrain_flatten`; `sar/sar_metadata.h`.
- Missing: dual-pol feature set (VV/VH ratio exists via ratio? audit),
  SAR domain contract hardening (sigma0/gamma0/beta0 × linear/dB),
  range-Doppler executable subset + honest refusal, layover/shadow mask.

### Temporal
- Have: composite (QA best-pixel), smooth (SG/Whittaker), gap_fill,
  harmonic_fit, phenology, breakpoints, decompose, anomaly, sen_trend,
  trend (incl. Mann-Kendall in fit kernels), index_series,
  extract_series, summary; `temporal_time` (day offsets),
  `temporal_preflight`, STAC adapter, collection input contracts.
- Missing: CUSUM, EWMA, seasonal MK, multi-season phenology, double
  cropping, dynamic thresholds, BFAST-like/CCDC-like honest approximations,
  duplicate-timestamp semantics audit.

### Terrain
- Have: `rs:terrain_analysis` single operator, products = slope, aspect,
  hillshade, roughness, tri, tpi (`rs_terrain_analysis_operator.cpp:31`).
- Missing: curvature (profile/plan), multidirectional hillshade, local
  relief, viewshed, depression fill, flow direction/accumulation,
  watershed primitives, geomorphometric classification; geographic-CRS
  distance semantics audit.

### Raster spatial
- Have: `rs:mosaic`, `rs:apply_mask`, `rs:majority_filter`, `rs:recode`,
  `rs:threshold_raster`, `gdal:clip/reproject/polygonize/
  orthorectification/pansharpen`, `rs:image_fusion`
  (brovey/ihs/gs/pca/linear), OBIA family (segment/features/hierarchy/
  label/classify/stats).
- Missing as operators: resample, align/snap-grid, rasterize, sieve,
  region grow, proximity/distance transform, fill holes, clump, zonal
  statistics, local maxima/minima, small-object removal, threshold
  families (Otsu audit in threshold_raster).

### Classification
- Have: OpenCV backends SVM/RF/MLP/NormalBayes
  (`src/analysis/classification/rs_classifier_*.cpp`), KMeans
  (`rs:kmeans_classification`, `rs_classifier_kmeans.cpp`),
  pipeline (scaling, stratified split, tiled predict, accuracy),
  cross-validation, accuracy assessment, JM separability, Hungarian
  label assignment. **No ISODATA anywhere** (grep verified).
- Missing: kNN, minimum distance, Mahalanobis, maximum likelihood,
  logistic regression, LDA/QDA, ISODATA; per-class metrics audit
  (IoU?); spatial CV audit.

### Change detection
- Have (strong): facade `rs:change_detection` + streaming atoms
  `rs:change_difference/normalized_difference/ratio/cva/cva_angle/
  log_ratio/sam/mad/irmad` (`rs_change_primitives.h`,
  `rs_change_streaming.h`), `rs:threshold_raster`,
  `rs:post_classification_change`, SAR change.
- Missing: transition matrix audit, uncertainty/confidence outputs,
  threshold-family sharing with Milestone A histogram primitive.

### Feature engineering
- Have: `rs:feature_stack`, `rs:feature_normalize`, `rs:feature_select`,
  OBIA features (incl. GLCM texture), spectral indices as features.
- Missing: audit of normalization save/apply for inference parity.

## 4. Test & benchmark baseline

- 340 Catch2 test files in `tests/`; family fixtures per
  `docs/processing/validation-policy.md` §2 taxonomy (temporal/change/SAR/
  spectral/classification already covered; see §3 snapshot there).
- Catalog drift guards: `test_algorithm_meta_drift.cpp`,
  `test_catalog_size.cpp`, `test_algorithm_schema.cpp`,
  `test_algorithm_organization.cpp`.
- Benchmarks (JSON baselines): `benchmarks/` — qa_mask, recode,
  majority_filter, spectral_index_streaming, temporal_composite,
  model_tile_inference, data_manager ops. **No terrain/SAR-neighborhood/
  classification/large-tile benchmarks yet.**

## 5. Baseline risks / watch items

1. Histogram/percentile duplication across ≥7 files — unify in Milestone A
   before adding index/threshold families (B/G/H depend on it).
2. `gdal:reproject` exists but "resample as explicit operator" does not;
   Milestone G must decide wrap-vs-new against ADR 0091/0098 seams.
3. Terrain kernels: verify geographic-CRS behavior before extending
   (degree/meter confusion is the classic failure).
4. `test_layout_tools` anonymous-namespace fix landed on master
   (b87b0fb77f); local stale stash in the main checkout is superseded.
5. ISODATA absent — new implementation, not a wrap.
