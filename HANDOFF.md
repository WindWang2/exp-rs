# HANDOFF — Autonomous RS System Perfection (/goal) — Optical Platform Session

**Date:** 2026-08-08
**Mode:** FULL_AUTONOMOUS_LOOP — 37 committed vertical slices + F-hardening (ADR 0065–0098)
**Scope:** Deepen `exp-rs` toward the general-purpose optical/multispectral/
hyperspectral processing platform (mission: product import → calibration →
QA masking → atmospheric correction → geometric/grid → analysis-ready →
analysis → accuracy → provenance, all through the Processing Registry).

---

## 1. Session Summary — 23 Slices, All Tested, All Committed

| # | Slice | Commit | ADR |
|---|-------|--------|-----|
| 1 | Semantic band roles (`BandRole` + Landsat/S2/MODIS discovery roles, `SICNU_BAND_ROLE`/`FWHM` metadata, role-resolved spectral-index defaults, catalog + preview carry) | `0b5491f9b4` | 0065 |
| 2 | Raster-grid compatibility service (`compareGrids`, wired into `rs:change_detection`) | `df75cd4d38` | 0066 |
| 3 | QA / cloud / cloud-shadow / snow masking (`rs:qa_mask`, Landsat QA_PIXEL + S2 SCL + generic bitmask; S2 SCL/MSK discovery; dialog) | `5b09a35336` | 0067 |
| 4 | Unified product import dialog (`ProductImportDialog`; Landsat + Sentinel-2 menu entries) | `cd1f524cf6` | 0068 |
| 5 | Radiometric calibration workflow integration (`autoDetectMetadataFile`, dialog, metadata preview) | `87086b0ea8` | 0069 |
| 6 | Atmospheric correction workflow integration (metadata-resolved gain/bias, auto-fill dialog) | `86d30f27d6` | 0070 |
| 7 | Orthorectification dialog over `gdal:orthorectification` | `9cc7d957c7` | 0071 |
| 8 | Change Detection 2.0 (ratio, CVA, Otsu/percentile thresholds, morphological cleanup, area stats) | `23cd2a2d01` | 0072 |
| 9 | Provenance foundation (lineage queries `derivedFrom`/`derivedOutputsOf`, `makeTaskDerivation`) | `904d81c13f` | — |
| 10 | Large-raster memory policy classification (`RSOperatorMemoryPolicy` per operator) | `84007512a6` | 0073 |
| 11 | Classification model metadata + compatibility check (sidecar `features`/`validation`) | `040523590f` | 0074 |
| 12 | MNF transform (`rs:mnf`) | `2ffa770ccb` | 0075 |
| 13 | Spectral Information Divergence (`rs:sam_classify` metric=sam/sid) | `ecf5606322` | 0076 |
| 14 | Linear spectral unmixing (`rs:spectral_unmixing`) | `0474c45b11` | 0077 |
| 15 | RX anomaly detection (`rs:rx_anomaly`) | `bed6181ed8` | 0078 |
| 16 | Spectral resampling (`rs:spectral_resample`, wavelength-aware) | `e3633031fd` | 0079 |
| 17 | Endmember extraction by PPI (`rs:endmember_extraction`) | `00d4684b8b` | 0080 |
| 18 | Spectral library domain (JSON import/export) | `5490db71b2` | 0081 |
| 19 | Wavelength-aware spectral profile (widget x-axis) | `5cf8adb077` | 0082 |
| 20 | Reusable preprocessing DAG (`lab.preprocess.optical` + `tool.rs.qa_mask`) | `2619d94ad3` | 0083 |
| 21 | ROI mean spectrum (`SpectralRoiProfile`) | `1bf432beba` | 0084 |
| 22 | Fusion/PCA scalability review (fusion grid preflight, PCA/MNF NaN handling) | `7a822b2f40` | 0085 |
| 23 | Provenance/lineage in the Data Manager panel | `68cf81116b` | 0086 |
| 24 | Semantic band roles in the agent workspace snapshot (`bandRoles` in the system prompt) | `8d8025099b` | 0087 |
| 25 | Apply a QA mask to a product (`rs:apply_mask`, block-streaming, mask-grid auto-align; dialog; DAG now chains `atmospheric → apply_mask → NDVI`) | `95604a0113` | 0088 |
| 26 | Post-classification comparison (`rs:post_classification_change`: transition matrix, gains/losses, change-type map; `post_classification` kernel) | `4ea75735c7` | 0089 |
| 27 | Semantic band roles in MCP `describe_dataset` (per-band `role`) | `36ecc88f81` | 0090 |
| 28 | Grid harmonization: `gdal:reproject` `reference` alignment (auto CRS/pixel/extent from a reference raster) + recovered orphan `test_gdal_ortho_operators` target | `f7a34c4965` | 0091 |
| 29 | Spectral library matching workbench (`SpectralLibrary::matchSpectrum` SAM/SID ranking; `SpectralLibraryDialog`; 光谱分析 menu) | `d8ef199bf5` | 0092 |
| 30 | Per-class classification diagnostics (`trainSamplesByClass`, `perClassMetrics`, `imbalanceWarnings`) | `f6d8a9b06a` | 0093 |
| 31 | README synced with the current optical-platform capability surface | `ab105da5ba` | — |
| 32 | Classification probability/confidence outputs (`probabilityOutput` raster + `meanConfidence`; NormalBayes posterior normalization; SVM rejected) | `3a306b9d74` | 0094 |
| 33 | Reusable align-and-compare change detection DAG (`lab.change.align_difference`: reproject-reference → difference) | `c19177769f` | 0095 |
| 34 | Wavelength-aware library matching (`matchSpectrum` resamples onto the entry grid when band counts differ; dialog passes profile wavelengths) | `61926df67a` | 0096 |
| 35 | ROI mean-spectrum tool (`RsRoiSpectrumTool` polygon → dock; `SpectralProfileWidget::setSpectrum`; completes spectral input side) | `19d6d4f594` | 0097 |
| 36 | Test hardening: TaskCenter cancel-timing tests no longer flaky under -j8 load (10s poll budgets) | `82620d02e9` | — |
| 37 | F review: shared `processing::gridFromDataset` — de-duplicated 4 identical grid builders | `553bb3d1fe` | 0098 |
| + | Full-suite regression fix (GDAL driver registration in a provider test) | `bae3a3eae0` | — |

## 2. Verification

- **Full clean rebuild: 0 errors** (all targets, incl. the whole test suite).
- **ctest: 1393 tests, 100% passed** after hardening the two TaskCenter
  cancel-timing tests that flaked under `-j8` load (they now poll with 10s
  budgets and pass consistently). 6 pre-existing vector fixtures remain
  SKIPPED (removed from VCS). Suite is green.
- Every new slice shipped with its own Catch2 tests (kernel + operator /
  UI-seam level); the operator registry tests pin the expanded surface.
- The orphaned `test_gdal_ortho_operators` file (registered in no target) was
  recovered as `test_gdal_ortho_operators` — 12 GDAL operator cases now run.

## 3. Product-Aware Pipeline (now reachable end-to-end in the desktop UI)

```
Import Sentinel-2 / Landsat (ProductImportDialog)
  → sensor + semantic band recognition (roles, WAVELENGTH/FWHM metadata)
  → radiometric calibration (auto MTL/MTD detection)
  → QA / cloud / shadow / snow mask (rs:qa_mask)
  → atmospheric correction (metadata-resolved DOS1/DOS2, QUAC)
  → orthorectification / grid harmonization (gdal:reproject reference alignment)
  → apply mask → analysis-ready (lab.preprocess.optical DAG, 5 steps)
  → NDVI / spectral indices (role-resolved) · change detection 2.0
  → post-classification comparison (transition matrix, gains/losses)
  → classification (model metadata + compatibility) · fusion · PCA/MNF
  → hyperspectral: profile → library match (SAM/SID) → resample →
    endmembers → unmix/SAM/SID → RX
  → provenance (model + lineage queries + Data Manager UI)
  → Agent/MCP: workspace snapshot + describe_dataset carry band roles
```

## 4. Known Follow-ups (roadmap `.planning/2026-08-07-rs-platform-goal.md`)

- C5: task-centric UI consistency (shared band/role/CRS/resolution widgets);
  Change Detection dual-view workbench (synchronized viewports + Swipe).
- C1 follow-ups: MAD wrapper over `otb:multivariate_alteration_detector`.
- D10: extend the spectral workbench (ROI mean spectrum → library grid
  resampling → continuum removal display); F-phase: cross-platform build
  verification (Linux is green), final architecture/code review.

## 5. Exclusions Honored

No LiDAR point-cloud subsystem; no SAR processing subsystem introduced.
DL inference remains the optional modular `rs:infer` seam only.
