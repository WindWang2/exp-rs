# P0–P5 Refactor Sprint — Design Spec

**Date:** 2026-07-03  
**Status:** Approved  
**Scope:** Dialog async lifecycle unification, algorithm extraction, StacClient modularization, `main_window` decomposition  
**Test gate:** 449/449 pass (`QT_QPA_PLATFORM=offscreen ctest`)

---

## 1. Goals

### 1.1 Primary goals

1. **Eliminate duplicated async boilerplate** in raster processing dialogs by centralizing run lifecycle in `RasterProcessingDialogBase`.
2. **Move heavy GDAL I/O into algorithm libraries** so dialogs remain thin UI shells.
3. **Decompose `main_window.cpp`** from a ~2000-line monolith into focused translation units with clear ownership boundaries.
4. **Fix latent bugs** discovered during migration (spectral index temp band lifetime, StacClient test linkage, Qt6 container APIs).

### 1.2 Success criteria

| Criterion | Target |
|-----------|--------|
| Unit tests | 449/449 pass offscreen |
| `main_window.cpp` line count | ≤ 300 lines (achieved: 238) |
| GDAL dialogs | All use `runGdalTask()` via base class |
| Processing dialogs | `spectral_index_dialog` uses `runAlgorithmTask()` |
| Duplicate `m_runner` in dialog headers | Removed |
| Design record | This spec + README/CLAUDE pointers |

### 1.3 Non-goals (YAGNI)

- No new user-facing features or behavior changes.
- No further `setupMapCanvas` split (already small).
- No rewrite of `spectral_index_dialog` to pure GDAL (Processing algorithm stays registered).
- No commit of unrelated vendor tree changes (`otb_ref/`, `boost_ref/`).
- No OBIA / Agent / MCP feature expansion in this sprint.

---

## 2. Retrospective — Phase Summary

### P0: Dialog base lifecycle + algorithm helpers

| Area | Change |
|------|--------|
| `RasterProcessingDialogBase` | `startRun()`, `finishRun()`, `isRunning()`, `runGdalTask()`, base-owned `m_runner` |
| Algorithm `processFile()` | `BandMath`, `AtmosphericCorrection`, `ImageEnhancement::processPcaFile` |
| Dialog migration | `band_math`, `atmospheric`, `pca` → `runGdalTask()` |
| Build | `SICNU_EMBED_PYTHON` CMake option (default OFF) |
| MCP | `StdinReader::requestStop()` replaces `QThread::terminate()` |
| Tests | `tests/test_raster_processing_dialog_base.cpp` |

### P1: Remaining dialog async + Fusion + Stac

| Area | Change |
|------|--------|
| Dialog migration | `contrast_stretch`, `mosaic`, `spatial_filter`, `speckle_filter`, `change_detection`, `extract_band`, `band_ratio`, `image_enhancement_panel`, `fusion` → `runGdalTask()` |
| `ImageFusion` | `NativeFusionParams` + `processNativeFusion()` using `GdalDatasetWrapper` + `writeGdalOutput()` |
| `StacClient` | Extracted to `src/agent/stac_client.{h,cpp}`; `StacBrowserDialog` consumes `searchCompleted` signal |
| `main_window` | `main_window_processing.cpp` already held RS dialog slots; duplicate per-dialog `m_runner` removed from headers |

### P2: Algorithm runner + menus split

| Area | Change |
|------|--------|
| `RasterProcessingDialogBase` | `runAlgorithmTask()`, `cleanupRunResources()` virtual hook |
| `spectral_index_dialog` | Migrated to `runAlgorithmTask()`; temp band layers/files held as members until `cleanupRunResources()` |
| `main_window_menus.cpp` | `setupMenu()`, `setupToolbars()`, `setupStatusBar()` extracted |

### P3: Connections + vector editing split

| Area | Change |
|------|--------|
| `main_window_connections.cpp` | `setupConnections()`, `initLayerTree()`, canvas/project/layer-tree callbacks |
| `main_window_vector.cpp` | `checkUnsavedChanges()`, clipboard edit ops, digitizing tools, `updateEditingUI()` |

### P4: Project + layers split

| Area | Change |
|------|--------|
| `main_window_project.cpp` | `newProject`, `openProject`, `saveProject*`, `importLayer`, `browseStacCatalog`, `newLayout` |
| `main_window_layers.cpp` | `onIdentifyResults`, layer add/remove/properties, CRS preset, LayerManager delegates |

### P5: View + misc split

| Area | Change |
|------|--------|
| `main_window_view.cpp` | Zoom/pan/identify, measure tools, Georeferencer, classification/OBIA windows |
| `main_window_misc.cpp` | Theme/preferences, processing history, help/about, sample data, panel layout persistence, `closeEvent` |
| `main_window.cpp` | Trimmed to constructor + `setupUi` + `setupMapCanvas` + plugin bootstrap (238 lines) |

---

## 3. Architecture

### 3.1 Dialog async pattern

```
User clicks Run
    → RasterProcessingDialogBase::validateInputs()
    → subclass onRun()
        → runGdalTask(lambda)          [GDAL path]
        → runAlgorithmTask(alg, ...)   [Processing path]
    → startRun() disables Run button
    → AsyncGdalRunner / AsyncAlgorithmRunner on background thread
    → onCompleted(outputPath) / onFailed(error)
    → cleanupRunResources() [subclass hook]
    → finishRun() + handleCompleted / handleFailed
```

**Key types:**

- `AsyncGdalRunner` — `std::function<QString()>` task, emits `completed(QString)` / `failed(QString)`
- `AsyncAlgorithmRunner` — Qgs Processing task, emits `completed(QVariantMap)` / `failed(QString)`
- Base maps algorithm completion to `onCompleted(outputPath())` using dialog's output path field

### 3.2 Dialog inventory

| Dialog | Async API | Notes |
|--------|-----------|-------|
| band_math | `runGdalTask` | `BandMath::processFile()` |
| atmospheric | `runGdalTask` | `AtmosphericCorrection::processFile()` |
| pca | `runGdalTask` | `ImageEnhancement::processPcaFile()` |
| contrast_stretch | `runGdalTask` | `writeGdalOutput()` in lambda |
| spatial_filter | `runGdalTask` | |
| speckle_filter | `runGdalTask` | |
| change_detection | `runGdalTask` | dual-input |
| extract_band | `runGdalTask` | |
| band_ratio | `runGdalTask` | |
| image_enhancement_panel | `runGdalTask` | multi-method panel |
| fusion | `runGdalTask` | `ImageFusion::processNativeFusion()` or OTB path |
| mosaic | `runGdalTask` | |
| spectral_index | `runAlgorithmTask` | `qgis_algorithms:rs_spectral_index`; temp bands in `m_tempLayers` |

### 3.3 `main_window` module map

| File | ~Lines | Owns |
|------|--------|------|
| `main_window.cpp` | 238 | Constructor, `setupUi`, `setupMapCanvas`, plugin load, theme restore |
| `main_window_menus.cpp` | 362 | `setupMenu`, `setupToolbars`, `setupStatusBar` |
| `main_window_docks.cpp` | 250 | `setupDockWidgets`, window-menu dock toggles |
| `main_window_connections.cpp` | 223 | `setupConnections`, `initLayerTree`, CRS/coord/scale callbacks |
| `main_window_view.cpp` | 112 | Map navigation, measure, Georeferencer, classify/OBIA |
| `main_window_vector.cpp` | 339 | Vector editing, clipboard, `checkUnsavedChanges` |
| `main_window_project.cpp` | 107 | Project I/O, import, STAC browser, print layout |
| `main_window_layers.cpp` | 272 | Identify HTML, layer CRUD, CRS dialog, LayerManager passthrough |
| `main_window_misc.cpp` | 234 | Preferences, processing history, help, panel state, `closeEvent` |
| `main_window_processing.cpp` | 367 | RS processing dialog open/load-result slots |

**CMake:** All files listed in `src/app/CMakeLists.txt` under `sicnu_geo_rs` target.

### 3.4 StacClient boundary

```
StacBrowserDialog
    → StacClient::search(endpoint, collection, datetime, bbox)
    → QNetworkAccessManager async GET
    → searchCompleted(features, error)
    → StacBrowserDialog::onSearchCompleted → populate table
```

`StacClient::buildSearchUrl()` is static and unit-tested in `tests/test_stac_client.cpp` (links `stac_client.cpp`).

### 3.5 Bug fixes bundled in sprint

| Bug | Fix |
|-----|-----|
| `spectral_index` temp files deleted when `onRun()` returned while async task still running | `m_tempFiles` / `m_tempLayers` members + `cleanupRunResources()` |
| `test_stac_client` link error | Add `stac_client.cpp` to test target sources |
| `image_fusion.cpp` `toStdVector()` on Qt6 `QList` | Iterator-range `emplace_back` into `std::vector` |
| `stac_browser_dialog` `QVariantMap::toMap()` on already-map type | Direct index access |

---

## 4. Maintainer Guide

### 4.1 Adding a new GDAL processing dialog

1. Subclass `RasterProcessingDialogBase`.
2. Implement `toolName()`, `dialogTitle()`, `onRun()`.
3. In `onRun()`, capture paths/params, then:

```cpp
runGdalTask([sourcePath, outPath, ...]() -> QString {
    QString error;
    if (!MyAlgorithm::processFile(sourcePath, outPath, ..., &error))
        throw std::runtime_error(error.toStdString());
    return outPath;
});
```

4. Put GDAL block I/O in `src/processing/algorithms/` using `GdalDatasetWrapper` + `writeGdalOutput()`.
5. Do **not** add `AsyncGdalRunner *m_runner` to the dialog header.

### 4.2 Adding a new Processing-based dialog

1. Same base class; use `runAlgorithmTask(alg, params, context)`.
2. If the algorithm needs resources that must outlive `onRun()`, store them as dialog members and release in `cleanupRunResources()` override.
3. Link `async_algorithm_runner.cpp` in any test target that pulls in `raster_processing_dialog_base.cpp`.

### 4.3 Adding a new `QgisDesktopWindow` slot

Route by concern (see §3.3 table). If a new category emerges (e.g. layout/print), create `main_window_<category>.cpp` rather than growing `main_window.cpp`.

**Invariant:** `main_window.cpp` stays limited to window bootstrap and map canvas setup.

### 4.4 Testing expectations

- Dialog base lifecycle: `tests/test_raster_processing_dialog_base.cpp`
- Algorithm helpers: extend existing `test_band_math`, `test_atmospheric`, `test_pca`, `test_image_fusion`
- Stac URL building: `tests/test_stac_client.cpp`
- Full gate: `QT_QPA_PLATFORM=offscreen ctest` → 449 tests

---

## 5. Wrap-up Checklist

Executed as a single atomic commit after spec approval:

- [ ] `docs/superpowers/specs/2026-07-03-refactor-sprint-design.md` (this file)
- [ ] `README.md` — test count 443 → 449
- [ ] `CLAUDE.md` — `src/app/` module table + link to this spec
- [ ] All refactor source changes (already in working tree)
- [ ] Verify: `cd build && make -j$(nproc) && QT_QPA_PLATFORM=offscreen ctest`

**Proposed commit message:**

```
refactor: unify dialog async lifecycle and modularize main_window

- RasterProcessingDialogBase: runGdalTask, runAlgorithmTask, run lifecycle
- Migrate all RS dialogs to base-owned async runners
- Extract StacClient; add ImageFusion::processNativeFusion
- Split main_window into 10 focused translation units (~238-line core)
- Fix spectral_index temp band lifetime bug
- 449/449 tests pass (offscreen)
```

---

## 6. References

- Dialog base: `src/app/dialogs/raster_processing_dialog_base.{h,cpp}`
- Async runners: `src/app/dialogs/async_gdal_runner.{h,cpp}`, `async_algorithm_runner.{h,cpp}`
- GDAL helpers: `src/processing/gdal/gdal_dataset_wrapper.{h,cpp}` (`writeGdalOutput`, `extractGeoInfo`)
- Prior code-reuse sprint: commits `995410e`–`534a4123` (MathUtils, dialog_utils)