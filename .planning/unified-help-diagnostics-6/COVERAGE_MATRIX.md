# COVERAGE_MATRIX — Unified Help 6.0

Measured coverage; updated as milestones land. Mechanical tests enforce the ✅ rows
(test names in TEST_MATRIX.md).

## Measured after content authoring (M-E close)

- Shipped descriptors in embedded content: 264 authored entries
  (49 commands + 6 workbenches + 1 alias + 12 concepts + 92 diagnostics + 105
  operator entries) + 105 flagship parameter knowledge entries nested inside
  operator entries.
- After composition with the live registry the registry also gains ~700
  auto-derived parameter descriptors (base tier from operator schemas), so
  total topic count is roughly 1,050; exact number asserted by
  `test_help_coverage` (>200 authored, >500 parameter descriptors,
  >=105 rs:* operators, >=45 commands via source scan).

| Domain | Total | Help descriptor | Deep knowledge | Tested |
| --- | --- | --- | --- | --- |
| CommandRegistry commands | 34 | 34 (M-B) | purpose+related for all; prerequisites for context-gated | drift test |
| Workbenches | 4 (classify, georef I2I, georef I2M, OBIA) | 4 (M-G) | empty-state guidance ≥3 | descriptor test |
| RSOperator `rs:*` | 105 | 105 (M-E) | flagship params deep (see below) | drift test |
| Operator families beyond rs:* (opencv/gdal/io/otb/python) | registry-listed | schema-derived base tier auto | no manual deep pages (out of scope tier) | derivation test |
| HarnessError codes | 24 | 24 (M-F) | remediation all | coverage test |
| RSOperatorError codes | 24 | 24 (M-F) | remediation all | coverage test |
| GeoError codes | 22 | 22 (M-F) | remediation all | coverage test |
| Dataset finding kinds | ~10 | 10 (M-F) | remediation all | coverage test |
| Preflight/MapSpec issue codes | catalogued at M-F | all catalogued | remediation all | coverage test |
| Concepts (RS/GIS) | ≥12 (spatial leakage, speckle, CVA, NDVI saturation, DEM hydro conditioning, RPC, GCP, dos1/quac, CgA/obia segmentation, determinism grade, memory policy, training/test split) | 12+ (M-E) | — | index test |
| Shortcuts | registry-owned | generated reference | — | generated docs test |
| Templates/recipes | recipe_catalog entries | generated summaries | — | generated docs test |

Flagship deep-parameter targets (unit + recommended + trade-off + scientific effect):
- SAR: rs:sar_speckle (kernelSize, looks, noiseVariance, dampingFactor, deviationK,
  companionScenes), rs:sar_calibrate, rs:sar_backscatter, rs:sar_terrain_flatten,
  rs:sar_terrain_correction, rs:sar_texture, rs:sar_change, rs:sar_dualpol.
- Optical: rs:spectral_index (+ aliases), rs:band_math, rs:atmospheric_* (dos1/dos2/
  quac/6s), rs:dn_to_radiance, rs:radiometric_calibration, rs:continuum_removal,
  rs:spectral_resample, rs:spectral_unmixing, rs:rx_anomaly, rs:endmember_extraction.
- Change: rs:change_detection (+ difference/ratio/cva/normalized_difference),
  rs:post_classification_change.
- Temporal: composite, gap_fill, harmonic_fit, sen_trend, breakpoints, phenology,
  monitor, anomaly, smooth, decompose, index, extract_series, summary, trend.
- Terrain: rs:terrain_analysis, rs:terrain_flow, rs:topographic_correction.
- Classification: rs:kmeans, rs:sam_classify, rs:supervised_classification,
  rs:obia_* (segment/hierarchy/features/label/classify/stats), rs:recode,
  rs:threshold_raster, rs:qa_mask, rs:apply_mask.
- Data/I/O: mosaic, fusion, image_enhancement, image_fusion, majority_filter,
  feature_* (normalize/select/stack), landsat/sentinel2/modis import, modis_georeference,
  inference, model_task, spatial ops, spectral_derivative, spectral_detection.

Coverage % targets: commands 100%, operators (rs:*) descriptor 100%, deep parameter
knowledge ≥60 flagship operators at M-E close; diagnostics 100% of enumerated codes.
