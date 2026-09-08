# Algorithm Capability Matrix (baseline 93a7fb0bbd)

Legend: ✔ = present, ◐ = partial (audit/extend), ✘ = absent.
"Owner" = kernel source of truth. "Ref" = hand-derived/synthetic numeric
references exist. "Scale" = bounded-memory/streaming path exists.

## Optical / Radiometric

| Capability | State | Kernel owner | Operator | Tests/Ref | Scale | Gap action |
|---|---|---|---|---|---|---|
| DN→radiance/reflectance calibration | ✔ | `radiometric_calibration.cpp` | `rs:radiometric_calibration`, `rs:dn_to_radiance` | ✔ | streaming?audit | audit metadata-driven gains |
| DOS1/DOS2/QUAC atmospheric | ✔ | `atmospheric_correction.cpp` | `rs:atmospheric_correction` (+aliases) | ✔ | ◐ | audit haze estimation; keep |
| Topographic correction (C/Minnaert/SCS+C) | ✘ | — | — | — | — | **Milestone B new kernel** |
| Spectral derivatives (1st/2nd) | ✘ | — | — | — | — | Milestone B |
| Continuum removal | ✔ | `spectral_indices?/continuum` | `rs:continuum_removal` | ✔ | audit | audit |
| Index families (NDVI/NDWI/MNDWI/NDBI/EVI/SAVI) | ✔ | `spectral_indices.cpp` | `rs:spectral_index` + single-index aliases | ✔ | streaming ✔ | extend families (B) |
| MSAVI/ARVI/GNDVI/NDMI/NBR/NDRE/NDSI/burn/urban | ✘ | — | — | — | — | **Milestone B** |
| QA/cloud mask composition | ✔ | `qa_mask.cpp` | `rs:qa_mask` | ✔ | ✔ | shadow/snow post-proc audit |
| Matched filter / ACE | ✘ | — | — | — | — | Milestone B/C |
| Radiometric domain validation | ◐ | `SICNU_RADIOMETRIC_STATE` seam | imports | ◐ | — | harden refusals (B) |

## Spectral / Hyperspectral

| Capability | State | Kernel owner | Operator | Tests/Ref | Scale |
|---|---|---|---|---|---|
| PCA | ✔ | `rs_pca_operator` | `rs:pca` | ✔ | ◐ streaming covariance audit |
| MNF | ✔ | `rs_mnf_operator` | `rs:mnf` | ✔ | noise estimation audit |
| PPI endmember | ✔ | `endmember_extraction.cpp` | `rs:endmember_extraction` | streaming-vs-kernel ✔ | ✔ |
| RX anomaly | ✔ | `spectral_anomaly.cpp` | `rs:rx_anomaly` | ✔ | sample covariance streaming |
| SAM classify | ✔ | `spectral_classification.cpp` | `rs:sam_classify` | ✔ | audit |
| SID | ◐ (in change/sam tests) | ? | — as callable | ◐ | **Milestone C** |
| Unmixing (UCLS) | ✔ | `spectral_unmixing.cpp` | `rs:spectral_unmixing` | ✔ | NNSLO/sum-to-1 audit (C) |
| Matched filter | ✘ | — | — | — | Milestone C |
| Band selection | ◐ | `rs:feature_select` | ✔ | audit | — |

## SAR

| Capability | State | Kernel owner | Operator | Tests/Ref | Scale |
|---|---|---|---|---|---|
| Calibration/backscatter | ✔ | `sar/sar_calibration.cpp` | `rs:sar_calibrate`, `rs:sar_backscatter` | ✔ | domain contract audit (D) |
| Speckle: Lee/EnhLee/Frost/Kuan/ΓMAP/RefinedLee/multitemporal | ✔ | `sar/sar_speckle.cpp` | `rs:sar_speckle` | refined-Lee published formula | tile+halo ✔ |
| GLCM texture (8 props) | ✔ | `sar/sar_texture.cpp` | `rs:sar_texture` | ✔ | ✔ |
| Ratio/log-ratio dual-pol | ✔ | `sar/sar_ratio.cpp` | `rs:sar_ratio` | ✔ | extend dual-pol features (D) |
| SAR change | ✔ | `rs_sar_change_operator` | `rs:sar_change` | ✔ | ✔ |
| Terrain flattening | ✔ | `sar/sar_terrain.cpp` | `rs:sar_terrain_flatten`, `rs:sar_terrain_correction` | ✔ | audit DEM/geometory contract |
| Range-Doppler subset + layover/shadow | ✘ | — | — | — | **Milestone D (research-first)** |
| SAR metadata domain contract | ◐ | `sar/sar_metadata.h` | — | ◐ | harden (D) |

## Temporal

| Capability | State | Kernel owner | Operator |
|---|---|---|---|
| Composite (best-pixel QA) | ✔ | `rs_temporal_composite_operator` | `rs:temporal_composite` |
| Smoothing (SG/Whittaker) | ✔ | `temporal/temporal_fit.cpp` | `rs:temporal_smooth` |
| Gap fill | ✔ | ditto | `rs:temporal_gap_fill` |
| Harmonic fit | ✔ | ditto | `rs:temporal_harmonic_fit` |
| Phenology (single season) | ✔ | `rs_temporal_phenology_operator` | `rs:temporal_phenology` |
| Breakpoints | ✔ | fit kernels | `rs:temporal_breakpoints` |
| Decompose | ✔ | `rs_temporal_decompose_operator` | `rs:temporal_decompose` |
| Anomaly (z-score) | ✔ | `rs_temporal_anomaly_operator` | `rs:temporal_anomaly` |
| Sen/MK trend | ✔ | `temporal_fit.cpp` | `rs:temporal_sen_trend`, `rs:temporal_trend` |
| CUSUM / EWMA | ✘ | — | **Milestone E** |
| Seasonal MK | ✘ | — | Milestone E |
| Multi-season phenology / double cropping | ✘ | — | Milestone E |
| BFAST-like / CCDC-like (honest subset) | ✘ | — | Milestone E |
| Change-point confidence | ◐ | breakpoints | audit (E) |

## Terrain

| Capability | State | Owner | Operator |
|---|---|---|---|
| Slope/aspect/hillshade/roughness/TRI/TPI | ✔ | `terrain_analysis.cpp` | `rs:terrain_analysis` (6 products) |
| Curvature (profile/plan) | ✘ | — | **Milestone F** |
| Multidirectional hillshade | ✘ | — | Milestone F |
| Local relief | ✘ | — | Milestone F |
| Depression fill / flow dir / accumulation | ✘ | — | Milestone F (foundation) |
| Viewshed | ✘ | — | F decision gate (dep fit) |
| Geomorphometric classification | ✘ | — | Milestone F (TPI-based) |

## Raster spatial (general)

| Capability | State | Owner | Operator |
|---|---|---|---|
| Mosaic | ✔ | `mosaic.cpp` | `rs:mosaic` |
| Apply mask / recode | ✔ | operators | `rs:apply_mask`, `rs:recode` |
| Clip/reproject/polygonize/ortho | ✔ | gdal wrappers | `gdal:*` |
| Resample (explicit operator) | ✘ | — | **Milestone G** (wrap GDAL warp) |
| Align/snap grid | ✘ | — | Milestone G |
| Rasterize | ✘ | — | Milestone G |
| Sieve / small-object removal | ◐ (post_classification sieve) | `post_classification.cpp` | expose standalone (G) |
| Proximity/distance transform | ✘ | — | Milestone G |
| Fill holes / clump | ◐ | post_classification | expose standalone (G) |
| Zonal statistics | ✘ | — | Milestone G |
| Local maxima/minima | ✘ | — | Milestone G |
| Majority filter | ✔ | `rs_majority_filter_operator` | ✔ |
| Focal statistics | ✘ | — | Milestone G (on A window primitive) |
| Morphology suite | ◐ scattered | qa_mask/threshold/opencv | consolidate (A→G) |

## Classification / Segmentation

| Capability | State | Owner | Operator |
|---|---|---|---|
| SVM/RF/MLP/NormalBayes | ✔ | `src/analysis/classification` | `rs:supervised_classification` |
| KMeans | ✔ | `rs_classifier_kmeans` | `rs:kmeans_classification` |
| ISODATA | ✘ | — | **Milestone H (new)** |
| kNN / min-distance / Mahalanobis / max-likelihood / logistic | ✘ | — | Milestone H |
| OBIA segment/features/classify | ✔ | `rs_obia_*` | ✔ |
| Post-classification (sieve/majority/clump/recode) | ✔ | `post_classification.cpp` | `rs:post_classification_*`? audit ids |
| Accuracy (confusion/Kappa) | ✔ | `rs_accuracy_assessment` | ✔ IoU audit (H) |
| Cross-validation | ✔ | `rs_cross_validation` | spatial-CV audit (H) |

## Change detection

| Capability | State | Notes |
|---|---|---|
| Atoms (diff/nd/ratio/cva/log-ratio/sam/mad/irmad) | ✔ streaming | reuse, don't duplicate (I) |
| Thresholding | ✔ `rs:threshold_raster` | share histogram primitive (A) |
| Post-classification change + transitions | ✔ | transition matrix audit (I) |
| Uncertainty/confidence | ✘ | Milestone I decision |

## Feature engineering

`rs:feature_stack/normalize/select` ✔; scaler save/apply parity audit in H.
