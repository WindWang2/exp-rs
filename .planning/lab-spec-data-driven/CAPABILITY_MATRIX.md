# CAPABILITY MATRIX — Phase 0 baseline audit (verified against worktree @ 27b9aa0a63)

## A. Hardcoded workflows → canonical labs → operators

`action` = UI verb (public slot on QgisDesktopWindow, invokeMethod). `operator` = `rs:*` id in
`RSOperatorRegistry` (110 ids enumerated in EVIDENCE.md §B). Params confirmed against registry
during P3 (drift-guarded).

| # | Lab id | 中文标题 | Docs? | Widget wf (id) | Step (old actionId) | New binding |
|---|--------|---------|-------|----------------|--------------------|-------------|
| 1 | image_enhancement | 影像增强与空间滤波 | lab1 | `image_enhancement` (createImageEnhancementWorkflow) | s1 load — `addRasterLayer` | action `addRasterLayer` |
|   |        |        |       |                | s2 contrast — `openContrastStretchDialog` | operator `rs:contrast_stretch` |
|   |        |        |       |                | s3 spatial filter — `openSpatialFilterDialog` | operator `rs:focal_stats`(confirm P3; candidate `rs:image_enhancement`) + action fallback `openSpatialFilterDialog` |
| 2 | spectral_analysis | 光谱指数与波段运算 | lab2 | `spectral_analysis` | s1 load — `addRasterLayer` | action |
|   |        |        |       |                | s2 profiles — `identifyFeatures` | action |
|   |        |        |       |                | s3 NDVI — `openSpectralIndexDialog` | operator `rs:spectral_index` (params: index=ndvi, red=4, nir=5) |
|   |        |        |       |                | s4 band ratio — `openBandMathDialog` | operator `rs:band_math` (expr b5/b4) or `rs:band_ratio` |
|   |        |        |       |                | s5 compare — `openComparisonDialog` | action |
| 3 | classification | 遥感影像分类 | lab3 | `classification` | s1 load — `addRasterLayer` | action |
|   |        |        |       |                | s2 supervised — `openClassificationWindow` | operator `rs:supervised_classification` |
|   |        |        |       |                | s3 accuracy — (none) | no binding (manual step) |
| 4 | change_detection | 变化检测 | lab4 | `change_detection` | s1 load ×2 — `addRasterLayer` | action |
|   |        |        |       |                | s2 visual — `openComparisonDialog` | action |
|   |        |        |       |                | s3 run — `openChangeDetectionDialog` | operator `rs:change_detection` |
| 5 | terrain_analysis | 地形分析 | lab5 | `terrain_analysis` | s1 load DEM — `addRasterLayer` | action |
|   |        |        |       |                | s2 hillshade — `openTerrainDialog` | operator `rs:terrain_analysis` (product=hillshade, az 315, elev 45) |
|   |        |        |       |                | s3 slope — `openTerrainDialog` | operator `rs:terrain_analysis` (product=slope) |
| 6 | georeferencing | 影像配准（几何校正） | lab6 | — (docs only) | docs menu "Raster > Georeferencer" | action `openGeoreferencer`(verify slot exists; else manual) + candidate `rs:align` |
| 7 | image_fusion | 影像融合 | lab7 | `image_fusion` | s1 concept — (none) | manual |
|   |        |        |       |                | s2 load — `addRasterLayer` | action |
|   |        |        |       |                | s3 Brovey — `openFusionDialog` | operator `rs:fusion_brovey` |
|   |        |        |       |                | s4 IHS — `openFusionDialog` | operator `rs:fusion_ihs` |
| 8 | atmospheric_correction | 大气校正 | — | `atmospheric_correction` | s1 load — `addRasterLayer` | action |
|   |        |        |       |                | s2 DOS1 — `openAtmosphericCorrectionDialog` | operator `rs:atmospheric_dos1` (fallback `rs:atmospheric_correction`) |
|   |        |        |       |                | s3 compare — `openComparisonDialog` | action |
| 9 | pca_analysis | 主成分分析 | — (orphan, promoted) | `pca_analysis` | s1 load — `addRasterLayer` | action |
|   |        |        |       |                | s2 run — `openPcaDialog` | operator `rs:pca` (components=3) |
|   |        |        |       |                | s3 analyze — (none) | manual |
| 10 | mosaic | 影像镶嵌 | — (orphan, promoted) | `mosaic` | s1 concept — (none) | manual |
|    |        |        |       |                 | s2 open tool — `openMosaicDialog` | operator `rs:mosaic` |
| 11 | obia_classification | 面向对象分类 (OBIA) | — (orphan, promoted) | `obia_classification` | s1 load samples — `loadSampleData` | action |
|    |        |        |       |                  | s2 open OBIA — `openObiaWindow` | action |
|    |        |        |       |                  | s3 segment+classify — (none) | operator `rs:obia_segment` + `rs:obia_classify` (split into separate steps) |

**Counts reconciled**: docs 7 ∪ widget 10 = 11 canonical labs (georeferencing docs-only;
atmospheric/pca/mosaic/obia widget-only). Goal: ≥10 ✓. All three inventories report 11 after P3.

## B. Operator inventory (registry truth for drift guards)

110 ids registered via `src/operators/rs/rs_operators_init.cpp`; wrapped by
`AtomicAlgorithmRegistry` (sicnu::processing). Relevant to labs:
spectral_index, band_math, band_ratio, ndvi/evi/savi/ndwi/mndwi/ndbi, contrast_stretch,
image_enhancement, focal_stats, morphology, supervised_classification, kmeans_classification,
sam_classify, ace, matched_filter, change_detection, change_difference, change_cva,
change_normalized_difference, terrain_analysis, terrain_flow, atmospheric_correction,
atmospheric_dos1/dos2/quac, dn_to_radiance, radiometric_calibration, fusion_brovey/ihs/pca/
gram_schmidt/linear, image_fusion, pca, mnf, mosaic, align, resample, obia_segment,
obia_features, obia_label, obia_classify, obia_hierarchy, segment, zonal_stats, proximity,
threshold_raster, apply_mask, qa_mask, sieve, fill_holes, majority_filter, recode, rasterize.

## C. Doc-vs-reality drift (why docs must become output)

- `docs/labs/README.md:23,35,41,47,53,59` cite menus `Raster > Enhancement` / `Raster >
  Classification` / … that no longer exist post-ADR 0099 (task-centric RS menu).
- Widget hardcodes 10 workflows incl. 4 labs absent from docs.
- `docs/labs` referenced 0× in `src/`, `tests/`, `data/` → docs are dead prose today.
- Lab data refs (`data/samples/landsat_sample.tif` etc.) exist as local sample rasters;
  `data/samples/` is gitignored (large rasters) — LabSpec references them by relative path only.
