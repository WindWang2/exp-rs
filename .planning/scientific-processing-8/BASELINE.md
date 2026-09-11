# BASELINE — execution-time audit of origin/master `322dfd3876`

Audited 2026-09-10. `origin/master` was `322dfd3876c34ed62b42846598cacd711c8c91d6`
(identical to the planning snapshot; no drift). No open PRs, no open issues.

## 1. Recent merge history (overlap map)

Latest 30 merged PRs inspected. Track ownership of the 2026-09-10 wave
(the "7.0 platform wave", all merged within hours):

| PR | Branch | Track | Relevance here |
|---|---|---|---|
| #835 | fix/ci-external-process-ns | SDK CI fix | none |
| #834 | fix/ci-range-cache-gdal | Geo CI fix (GDAL 3.8–3.13 VSI bridge) | read for GDAL compat patterns |
| #833 | fix/ci-master-post-832 | CI fix | none |
| #832 | cartography-platform-7 | Cartography | none (avoid shared files) |
| #831 | verification-observability-7 | Fault injection/tests | evidence conventions |
| #830 | plugin-isolation-runtime-5 | SDK | none |
| #829 | **scientific-algorithms-7** | **Scientific algorithms** | **primary predecessor of this track** |
| #828 | execution-plane-runtime-7 | Execution plane | none |
| #827 | pi-spatial-scientist-harness-7 | Agent harness | preflight rule packs may reference new ops |
| #826 | professional-workbench-7 | App | none |
| #825 | model-runtime-multimodal-7 | Models | none |
| #824 | dataset-experiment-7 | Data | none |
| #823 | cloud-geospatial-io-7 | Geo I/O | VectorReader/RasterReader seams consumed here |
| #822 | fix/resolve-open-issues-773-817 | Cross-cutting fixes | SAR look-azimuth, Minnaert, flow NoData fixes |

Older 4.0/5.0/6.0 waves (PRs #761–#821) are fully merged; their remote
branches are historical residue, none diverged (verified by `git branch -r`
+ merge commits). The `itk-upstream/*` branches are the vendored ITK mirror,
not feature work.

## 2. What Scientific Algorithms 7.0 (PR #829) already delivered

- `src/processing/algorithms/sar/sar_orbit.{h,cpp}` — WGS84/ECEF, orbit
  state-vector contract (`SICNU_SAR_ORBIT_STATES`, `SICNU_SAR_AZIMUTH_START_UTC`,
  `SICNU_SAR_PRF`, `SICNU_SAR_RANGE_WINDOW`, `SICNU_SAR_RANGE_RATE`),
  parser+validation, cubic Hermite interpolation, zero-Doppler geolocation
  (image→ground), forward range-Doppler (ground→image, sign-scan+bracketed
  bisection), per-point incidence. Known-answer tested vs a synthetic circular
  orbit (`tests/test_sar_orbit.cpp`).
- `rs:sar_terrain_masks product=local_incidence_orbit` — BACKWARD geolocation
  per input-SAR pixel (row→azimuth time, col→slant range, DEM height) and
  real-LOS incidence angle. DEM sampled through `GdalDatasetWrapper`.
- `rs:sar_terrain_flatten` — gamma0 RTC via plane-fit/projected-area model
  (honestly named, not per-pixel orbit geometry).
- SAR calibration/speckle/texture/dualpol/ratio/change operators; SAR domain
  (linear power vs dB) contract in `sar_metadata.h` + docs/processing/sar-domain.md.

**Explicitly deferred by 7.0 (sar-domain.md §3):** "full range-Doppler terrain
correction still requires DEM resampling into range time and output-grid
geocoding with radiometric rescaling — no operator claims it."

## 3. Capability matrix (track-relevant items)

| Track item | State | Evidence |
|---|---|---|
| A. Orbit interpolation + zero-Doppler machinery | **Implemented** (7.0) | `sar_orbit.h`, `test_sar_orbit.cpp` |
| A. Backward geolocation per SAR pixel (real LOS incidence) | **Implemented** (7.0) | `rs_sar_terrain_masks_operator.cpp` |
| A. **Forward geocoding: DEM map grid → (azimuth,range) → resampled radiometry** | **Missing** (explicitly refused by 7.0) | sar-domain.md §3 "no operator claims it" |
| A. **Terrain/area factor from real per-pixel LOS (sigma0→gamma0 RTC)** | **Missing** (flatten uses plane-fit proxy) | sar-domain.md §2.2 |
| A. Layover/shadow from real per-pixel orbit geometry on an output grid | Missing (constant-geometry + backward-image-grid variants exist) | `rs_sar_terrain_masks` products |
| B. 2-scene SAR ratio/log-ratio change | Implemented | `rs:sar_change`, `rs:sar_ratio` |
| B. Multi-date SAR stack statistics (linear-domain robust summaries, dB reporting, dispersion) | **Missing** | no operator; `rs:temporal_summary` exists but has no SAR-domain contract |
| C. Temporal family (trend, MK/Sen + tie-corrected, seasonal MK, harmonic, phenology, breakpoints, decompose, anomaly, gap-fill, composite, index series) | **Implemented** with one documented time-axis contract | docs/processing/temporal.md, `temporal/` kernels |
| D. `rs:zonal_stats` (vector polygon zones) | **Missing** (only label-raster `rs:segment_stats` exists) | grep + operator list |
| D. `rs:rasterize` (vector → raster burn) | **Missing as operator** (provider-CLI `gdal_rasterize` and qgis_analysis-internal `RsPixelRasterizer` exist but are not operator-reachable, and RsPixelRasterizer is GUI-adjacent) | grep + headers |
| E. Spectral indices/detection/unmixing/derivatives | Implemented (broad) | `spectral_*.cpp`, operators |
| E. Mechanical formula/catalog drift guard | **Missing** — formulas live as free functions + doc comments; help/catalog text can drift | no test binds formula text ↔ kernel numbers |
| F. Terrain/hydrology (fill/D8/accumulation, ties, NoData) | Implemented with documented contracts | `terrain_flow.h`, `terrain_analysis` |
| G. Classification (ISODATA, kmeans, SVM/RF/MLP/kNN/minDist/Mahalanobis, deterministic seeds) | Implemented | `rs_classification_pipeline`, `test_classifier_isodata` |
| H. Scientific contract layer (numeric domain once-per-raster) | Implemented (6.0) | `processing/contracts/scientific_contracts.h` |
| I. Evidence corpus | 447 test files, capability/meta drift tests exist | `tests/` |

## 4. Refusals / non-goals for this track

- No interferometric/InSAR coherence (no SLC/complex inputs in the contract).
- No quad-pol RVI (dual-pol approximation only — documented honesty rule).
- No new scheduler/agent loop/model runtime/renderer/store.
- No GUI dependencies in new kernels.

## 5. Cross-track seams to keep clean

- `src/operators/rs/rs_operators_init.cpp` — registration table (shared; keep
  edits to isolated `add(...)` lines).
- `tests/CMakeLists.txt` — `sicnu_add_test(...)` lines only.
- `src/processing/CMakeLists.txt` / operator CMake — additive source lines.
- `docs/processing/*.md` — additive sections.
- Harness preflight rule packs (`src/agent/harness/`) are another track's
  surface; only *additive* metadata from new operators flows there via the
  registry — no harness file edits planned.
