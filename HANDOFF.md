# HANDOFF — Remote Sensing System Deepening (Loops K1–K5)

**Date:** 2026-08-07
**Mode:** FULL_AUTONOMOUS_LOOP
**Scope:** Physics/fusion algorithm completion, UI/UX viewport enhancement, out-of-core memory hardening.

---

## 1. Summary

Five TDD loops were executed against the `exp-rs` codebase (C++20 / Qt6 / QGIS engine). Each loop followed Perceive → Design → Red test → Green implementation → Refactor → Verify. All 30 new tests pass; the full goal-domain suite (48 tests) is 100% green; the project builds with zero errors.

A pre-implementation gap analysis established that most of the original `/goal` was **already delivered** by prior sessions — radiometric calibration + DOS/QUAC atmospheric correction, the Spectral Profile Viewer, the histogram/piecewise-stretch editor, the Swipe tool, the Python worker auto-healing pool, and in-memory `ChunkedProcessor` all existed. This session closed the five genuine gaps:

| # | Goal item | Status before | Status after |
|---|-----------|---------------|--------------|
| K1 | Gram-Schmidt pan-sharpening | Brovey/PCA/IHS/linear existed; GS missing | ✅ Added |
| K2 | Spectral Angle Mapper (SAM) | Not present | ✅ Added |
| K3 | Continuum Removal | Not present | ✅ Added |
| K4 | Dual synced 1×2 viewports | Split layout + secondary view existed; pan/zoom NOT synced | ✅ Pixel-level sync added |
| K5 | Out-of-core GDAL block streaming | In-memory `ChunkedProcessor` only | ✅ Streaming iterator added |

---

## 2. Architecture Changes

### 2.1 New algorithm kernels (`src/processing/algorithms/`)

**`spectral_classification.{h,cpp}`** — pure-float, dependency-free hyperspectral kernels:
- `SpectralClassification::spectralAngle(t, r, bands, nodata)` → radians in [0, π/2], NaN on zero-norm or nodata.
- `SpectralClassification::samClassify(pixels, count, bands, refs, refCount, labels, angles, nodata)` — pixel-major spectra, labels = argmin-angle class, -1 for nodata.
- `SpectralClassification::continuumRemoval(spectrum, out, bands, nodata)` — convex upper hull (Andrew's monotone chain), divides each sample by the continuum value; output in (0, 1].

Deliberately OpenCV-free so they register **unconditionally** (not behind `SICNU_HAS_OPENCV`), matching `spectral_indices`/`band_math` conventions.

**`image_fusion.{h,cpp}`** — added `ImageFusion::gramSchmidtFusion(...)`:
- Simulated-pan GS variant (Laurin et al. / ENVI). Builds synthetic low-res pan = mean of MS bands, modified-Gram-Schmidt orthogonalizes `[synPan, MS₁…MSₙ]`, GS component 0 is replaced by histogram-matched high-res pan, inverse transform recovers sharpened bands. Stores coefficient matrix for exact inverse. Reentrancy/nodata handling consistent with the existing PCA/Brovey kernels.

### 2.2 New operators (`src/operators/rs/`)

Two thin JSON adapters over the kernels, following the `RsTerrainAnalysisOperator` template (single multi-band raster in → raster out, `processFile`-style via `GdalDatasetWrapper` + `writeGdalOutput`):

- **`rs_sam_classify_operator.{h,cpp}`** → `rs:sam_classify`. Reads `refs` (array of equal-length float arrays), classifies each pixel by smallest spectral angle, writes a single-band float raster of class IDs (nodata = -9999). Optional `angleOut` writes the per-pixel best-angle raster.
- **`rs_continuum_removal_operator.{h,cpp}`** → `rs:continuum_removal`. Per-pixel continuum removal; preserves band count.

Both registered unconditionally in `rs_operators_init.cpp` (`REGISTER_RS_OPERATOR`) and in `src/operators/CMakeLists.txt`. Auto-bridged into the `AtomicAlgorithmRegistry` for agent/MCP tool-call access by the existing provider registration block.

### 2.3 Dual-viewport sync (`src/app/shell/`)

**`rs_dual_viewport_sync_controller.{h,cpp}`** — `RsDualViewportSyncController`. Extends the georeferencer's `RsTwinCanvasSyncController` pattern: that controller copies **extent only** (the georef canvases have different CRS). The dual-viewport controller copies **extent + scale + rotation** so the two peer canvases (shared CRS, cloned layers) stay pixel-aligned. Same 16ms throttle + `mApplying` reentrancy guard. Adds `setScaleSync(bool)` (decouple zoom while sharing pan center) and `snapSecondaryToPrimary()` (immediate apply on view open).

Main-window wiring (`src/app/main_window_view.cpp`, `main_window_menus.cpp`, `main_window.h`):
- Lazy-create the controller in `openSecondaryMapView()`, snap on open.
- Tear down in `closeSecondaryMapView()`.
- New View-menu checkable action `双视口联动` (Ctrl+Shift+L) → `toggleDualViewportSync(bool)`.
- New members on `QgisDesktopWindow`: `m_dualViewportSyncAction`, `m_dualViewportSync`.

**Design note:** the dual 1×2 layout itself (`m_mapSplitter` + `SecondaryMapViewWidget` + `m_secondaryViewAction`) was already delivered by the Wave D multi-view work; this loop added only the missing pixel-level synchronization.

### 2.4 Out-of-core block streaming (`src/processing/gdal/`)

**`gdal_block_stream.{h,cpp}`** — `GdalBlockStream`. Tile-by-tile iterator over a GDAL band: row-major visit order, edge-clamped tiles, `forEach(callback)` reads each tile into a reused float buffer (memory O(tileW×tileH) not O(rasterW×rasterH)). Complements the in-memory `ChunkedProcessor` (which parallel-processes an already-loaded buffer). Single-threaded by design — GDAL reads are sequential; the callback is free to dispatch compute onto a thread pool.

**`GdalDatasetWrapper::readBandWindow(band, xOff, yOff, srcW, srcH, buf)`** — new public windowed `GDALRasterIO` read, used by the iterator and reusable for any sub-region read. Edge-clamped internally.

---

## 3. Test Coverage

**30 new tests, all green** (test count 1220 → 1250):

| File | Tag | Cases | Notes |
|------|-----|-------|-------|
| `test_image_fusion.cpp` | `[fusion]` | +5 (16 total) | GS kernel + file-level GS round-trip |
| `test_spectral_classification.cpp` | `[sam]`, `[continuum]` | 15 | kernel + operator file I/O for both |
| `test_dual_viewport_sync.cpp` | `[app][dual_viewport]` | 5 | extent propagation both directions, disable, snap, defaults |
| `test_gdal_block_stream.cpp` | `[block_stream]` | 5 | tile count, edge clamp, exact single-coverage of every pixel with correct values, abort, oversized tile |
| `test_rs_operators.cpp` | `[operators][rs]` | +2 asserts | `rs:sam_classify` + `rs:continuum_removal` registration smoke |

Test conventions matched existing seams: kernel-level flat-float `Catch::Matchers::WithinAbs` (parallel to `test_atmospheric.cpp` / `test_band_math.cpp`), file-level `createOutputTiff` + `GdalDatasetWrapper` round-trip, and the `FastExitListener` trick (bypass the QgsProjContext atexit crash) for real-`QgsMapCanvas` tests.

---

## 4. Verification

- **Build:** full project (`sicnu_processing`, `sicnu_operators`, `sicnu_geo_rs`) builds with **0 errors, 0 new warnings**. (Pre-existing QGIS-core `QVariant::Type` deprecation warnings are unchanged.)
- **Goal-domain suite:** 48/48 ctest tests pass (fusion, spectral, sam, continuum, swipe, sync, dual_viewport, block_stream, radiometric, atmospheric, operators).
- **Regression check:** mosaic, change_detection, terrain, pca, spectral_indices, twincanvas_sync, gdal_wrapper — all pass. The shared `GdalDatasetWrapper` (touched by the new `readBandWindow`) caused no downstream regressions.
- **Out-of-scope check:** no SAR or LiDAR code paths were touched or introduced.

---

## 5. Code Review (qt-cpp-review)

A full `qt-cpp-review` pass ran on all new code: deterministic lint (Phase 1) + six parallel deep-analysis agents (Phase 2). The lint flagged 4× HDR-3 (bare `std::min`), which is the **established project convention** (18 bare vs 1 parenthesized across `src/processing` + `src/operators`; the file this code extends already uses bare form) and intentionally not changed for consistency.

The deep-analysis agents surfaced 5 actionable findings, **all fixed** before the final QA pass:

| ID | Finding | Severity | Resolution |
|----|---------|----------|------------|
| D1 | `samClassify` did `break` on any NaN angle — a single degenerate reference (zero-norm/nodata) silently disabled classification for the entire raster | **Correctness (silent wrong output)** | ✅ Fixed: classify pixel validity up front; in the ref loop, `continue` on NaN (skip bad ref) rather than `break`. Pixel-side nodata still → label -1. 2 new tests cover both paths. |
| D2 | Operators hardcoded `-9999` nodata; rasters with nodata=0/NaN/-32768 were mis-classified | **Correctness (silent wrong output)** | ✅ Fixed: read band nodata via `GdalDatasetWrapper::bandNoDataValue`, fall back to -9999 when none declared. Output label raster keeps a fixed `-9999` sentinel (class ids never collide). |
| D4 | `GdalBlockStream::forEach` doc invited unsafe async thread-pool dispatch; buffer stride contract undocumented | **Robustness** | ✅ Fixed: documented the borrow contract (buffer valid only during callback; copy before async dispatch) and row stride (= `tile.width`, not nominal) on `TileCallback`. |
| D5 | `gramSchmidtFusion` recomputed the per-pixel validity expression `2k+1` times per `k` inside the j-loop (redundant `isnan`/comparisons, ~2·10⁹ wasted ops on a 4-band 4000² raster) | **Performance** | ✅ Fixed: hoisted a reusable per-`k` validity mask (`std::vector<char>`) computed once. |
| D6 | `RsDualViewportSyncController::Pending` was an unscoped enum with no underlying type / trailing comma | **Style** | ✅ Fixed: `enum class Pending : int { ... };` with trailing comma; call sites qualified. |

**Deferred (documented, not fixed — out of scope or shared-infra):**

- `writeGdalOutput` does not call `GDALSetRasterNoDataValue` on output bands (Agent 5, conf 92). This is a **shared infra** gap affecting every `writeGdalOutput` caller in `src/operators/rs/` (continuum, spectral-index, change-detection, terrain, mosaic), not just the new operators. Fixing it properly means an overload + migrating all callers — a separate change. Nodata pixels are still distinguishable by value (-9999) for the new operators.
- `int n = width*height` overflow risk for rasters >46340 px per side (Agent 6, conf 68). This is a **pre-existing pattern across all fusion methods** (`linearWeighted`, `brovey`, `pcaFusion`, `ihsFusion`, `gramSchmidtFusion`); changing one in isolation would be inconsistent. Recommend a sweep across all fusion kernels as a follow-up.
- Raw `QgsMapCanvas*` in the dual-viewport controller could be `QPointer` (Agent 2, conf 65). Theoretical only — Qt auto-disconnects `extentsChanged` on canvas destruction and child-destruction order keeps the controller alive ≤ canvases. Noted for future hardening.
- Dual-viewport controller refreshes both canvases on each apply (Agent 6, conf 74). Matches the existing georeferencer `RsTwinCanvasSyncController` pattern; left consistent.

---

## 6. Files Changed

**New (10):**
- `src/processing/algorithms/spectral_classification.{h,cpp}`
- `src/operators/rs/rs_sam_classify_operator.{h,cpp}`
- `src/operators/rs/rs_continuum_removal_operator.{h,cpp}`
- `src/app/shell/rs_dual_viewport_sync_controller.{h,cpp}`
- `src/processing/gdal/gdal_block_stream.{h,cpp}`
- `tests/test_spectral_classification.cpp`, `tests/test_dual_viewport_sync.cpp`, `tests/test_gdal_block_stream.cpp`

**Modified (9):**
- `src/processing/algorithms/image_fusion.{h,cpp}` (GS kernel + dispatcher)
- `src/operators/rs/rs_image_fusion_operator.cpp` (enum + hint)
- `src/operators/rs/rs_operators_init.cpp` (registration)
- `src/processing/gdal/gdal_dataset_wrapper.{h,cpp}` (`readBandWindow`)
- `src/app/main_window.{h}`, `main_window_view.cpp`, `main_window_menus.cpp` (dual-viewport wiring)
- `src/processing/CMakeLists.txt`, `src/operators/CMakeLists.txt`, `src/app/CMakeLists.txt` (new sources)
- `tests/CMakeLists.txt`, `tests/test_image_fusion.cpp`, `tests/test_rs_operators.cpp`

---

## 7. Commits

1. `43595e139c` feat(rs): add Gram-Schmidt fusion, SAM classifier, continuum removal (Loops K1–K3)
2. `196b478ac2` feat(shell): wire pixel-level dual-viewport pan/zoom sync (Loop K4)
3. `0c69390301` feat(gdal): out-of-core block-streaming iterator for huge GeoTIFF (Loop K5)

---

## 8. Items deliberately left out of scope

- **6S / FLAASH physical atmospheric correction** — requires external radiative-transfer binaries; reserved for future Python IPC integration (per the existing radiometric spec's Out-of-Scope).
- **Calibration-coefficient persistence into GeoTIFF metadata at import time** — the calibration operator reads MTL/MTD directly; future enhancement.
- **Scale-aware Swipe across two independent viewports** — the existing `SwipeMapTool` remains a single-canvas curtain; integrating it with dual synced viewports is a follow-on UX decision (each viewport can already run its own base layer).
- **Continuum-removal band-depth / SAM pairing operator** — both kernels are composable via the existing workflow/tool-call seam; a dedicated combined operator wasn't requested.
