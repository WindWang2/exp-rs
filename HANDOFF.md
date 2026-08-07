# HANDOFF — Autonomous RS System Perfection (/goal) — Final Session

**Date:** 2026-08-07
**Mode:** FULL_AUTONOMOUS_LOOP (Loops L1–L6 + qt-cpp-review + final gates)
**Scope:** Close every remaining `/goal` gap from the K1–K5 baseline, harden crash
recovery, verify the full exit criteria.

---

## 1. Goal Status — 100% Complete

| # | Goal item | Status | Where |
|---|-----------|--------|-------|
| 1.1 | Radiometric calibration operator (Landsat 8/9, Sentinel-2 DN→TOA), registered in AlgorithmEngine, async via TaskCenter | ✅ (prior + L5) | `RsRadiometricCalibrationOperator` (`rs:radiometric_calibration`) + **new operator-level run() test** |
| 1.2 | DOS1 atmospheric correction with auto histogram-minimum extraction | ✅ (L2) | `AtmosphericCorrection::findDarkObjectByHistogram` + `dos1Histogram`; `rs:atmospheric_correction` DOS1/DOS2 now histogram-based |
| 1.3 | Gram-Schmidt + Brovey pan-sharpening | ✅ (K1 + prior) | `ImageFusion::gramSchmidtFusion` / `brovey` via `rs:image_fusion` |
| 1.4 | Spectral Angle Mapper (SAM) | ✅ (K2) | `rs:sam_classify` + `SpectralClassification::samClassify` |
| 2.1 | Spectral profile dock (click → per-band values → curve chart) | ✅ (prior + L6) | `SpectralProfileWidget` + **real-raster extraction tests + accessors** |
| 2.2 | Piecewise stretch editor with control-point handles → `DisplayStretchPipeline` | ✅ (prior) | `HistogramStretchWidget`/`HistogramWidget` (ADR 0008/0041) |
| 3.1 | Linked 1×2 dual viewports with pixel-level pan/zoom sync | ✅ (K4) | `RsDualViewportSyncController` |
| 3.2 | Swipe compare overlay (drag splitter, clipped layers) | ✅ (prior + **L1**) | `SwipeMapTool` — **test file was never registered in CTest; now registered & green** |
| 4.1 | Out-of-core tile streaming, no OOM on 10 GB+ GeoTIFFs | ✅ (K5 + **L3**) | `GdalBlockStream` + **wired into `rs:radiometric_calibration` and `rs:atmospheric_correction` (O(tile) memory)** |
| 4.2 | POSIX shared-memory zero-copy + worker crash auto-restart **+ state recovery** | ✅ (prior + **L4**) | ADR 0064 channel + `PythonWorkerProcessPool` **in-flight request re-dispatch with retry budget + watchdog** |

Exclusions honored: no SAR/LiDAR computation libraries or coupling introduced.

---

## 2. This Session's Work (Loops L1–L6)

### L1 — Swipe test quality gate
`tests/test_swipe_map_tool.cpp` (5 cases, 19 assertions) existed but was **never
registered** in `tests/CMakeLists.txt` — the swipe tool had zero CTest coverage.
Registered with its own target; builds and passes.

### L2 — Histogram-based dark-object extraction (goal 1.2)
`AtmosphericCorrection::findDarkObjectByHistogram(radiance, count, bins=1024)`:
bins the scene, picks the lowest radiance whose bin holds ≥ max(2, valid/10000)
pixels (0.01 % frequency floor) — isolated sensor-noise spikes below the real
scene floor are rejected. Falls back to the global minimum for tiny scenes;
single-level scenes return their own level; non-finite values are ignored.
`dos1Histogram()` is the kernel variant; the production `processFile` DOS1 path
now uses it. 9 new unit cases (24 assertions).

### L3 — Out-of-core streaming in radiometric calibration (goal 4.1)
- `GdalDatasetWrapper::create()` — write-open a new LZW GeoTIFF with
  geotransform/projection (mirrors `createOutputTiff`; honors `lastError()`).
- `GdalDatasetWrapper::writeBandWindow()` — windowed GF_Write, edge-clamped.
- `RadiometricCalibration::processFile` rewritten: validates all requested
  bands **before** creating output, then streams each band 256×256 tiles via
  `GdalBlockStream` → kernel → `writeBandWindow`. Memory O(tile) instead of
  O(width×height); no per-tile allocations (buffer reused). 3 new streaming
  tests (60k+ assertions) incl. edge-clamped multi-tile and band-subset runs.

### L4 — Worker crash state recovery (goal 4.2)
- `PythonIpcServer::PendingRequest` + `m_inFlight` tracking: every async
  `sendRequest` (with callback) is recorded; `takeInFlightRequests()` moves out
  request+callback+retry budget on crash. Blocking paths
  (`sendRequestAndAwait`/`sendRequestSync`) stay untracked — their callbacks
  capture stack locals and must not be replayed.
- `PythonWorkerProcessPool::handleWorkerCrash` re-dispatches in-flight requests
  to the restarted worker (deferred until `clientConnected`), each replay
  consuming one retry; exhausted retries answer with an error. New test: an
  in-flight `crash_test` request → double crash/restart cycle → terminal error,
  never a hang.

### L5 — Operator-level test (goal 1.1)
New `RS radiometric calibration operator execution` case: full `run()` with MTL
metadata through the registry; verifies result JSON, band count and exact
radiance pixels.

### L6 — Spectral profile widget real coverage (goal 2.1)
Added `hasData()/values()/bandLabels()` accessors and rewrote the two vacuous
memory-layer tests (the "memory" provider is vector-only → tests early-returned
with zero assertions) to use a real 2-band GeoTIFF. 5 cases / 36 assertions
cover extraction values, band labels, cache reuse, out-of-bounds and
layer-removal.

---

## 3. qt-cpp-review — Findings & Fixes (all applied)

Phase 1 lint + 6 parallel deep-analysis agents ran on the session's diff.
Fixable findings, all fixed and re-verified:

| ID | Finding | Fix |
|----|---------|-----|
| A | One crash emitted `workerCrashed` **twice** (`errorOccurred(Crashed)` + `finished(CrashExit)`) → the pool killed the freshly restarted worker and orphaned replay bookkeeping | Coalesced: emit only from `onProcessFinished` |
| B | Pending callbacks leaked if the restarted worker never connected / `listen` failed | 5 s watchdog timer fails them; `startWorker()` return checked; `failPendingRequests()` on both failure branches |
| C | Replayed callbacks could fire after the caller's frame died (adapter `load_plugin`/`unload_plugin` captured stack locals) | Durable captures: `std::shared_ptr` result + `QPointer<QEventLoop>` |
| D | `findDarkObjectByHistogram` treated ±inf as valid → UB `float→size_t` cast, inf bin width | Non-finite values skipped (`std::isfinite`) in range + bin passes (refactored into incremental `DarkObjectStats` accumulator) |
| E | Dark level = bin center could overshoot `maxVal` → whole scene went negative | Clamped to scene max |
| F | `GdalDatasetWrapper::create()` bypassed the `lastError()` contract | `m_lastError` set on every failure path (and `GDALSetProjection` failure now checked) |
| G | Deterministic band errors left a partial output file (create happened before validation) | Upfront band-range + coefficient validation before `create()` |
| H | `SpectralProfileWidget::drawLine` cast NaN y-coordinates to int (UB) | NaN/inf bands skipped — polyline breaks across the gap; markers/labels iterate the point set |
| I | Two spectral-profile tests were vacuous (invalid memory layer) | Rewritten with real raster + assertions |
| J | `processFile applies DOS1` test asserted nothing about output pixels | Reads output back, asserts {0.0, 0.5, 1.5, 0.3} |
| K | Atmospheric `processFile` still whole-band (~12 B/px peak) | Streamed: DnToRadiance 1 pass; DOS1/DOS2 3 passes (range → bins → apply) via `DarkObjectStats`, O(tile) memory |
| L | `AppInterfaceBridge` held a stale server pointer after restart | Re-bind on `workerRestarted` (adapter is not a QObject → `QPointer`-guarded bridge capture, node-id matched, connection severed in `unload()`) |

Lint cleanups in touched files: parenthesized `(std::min)` in `writeBandWindow`,
`QMenu::addAction` deprecated 5-arg reordered (27 sites, main_window_menus),
`QDomDocument::setContent` → ParseResult overload, `(void)` on ignored
`GDALRasterIO` returns (6 files), `x()/y()` → `position()` in main_window.h,
`Classification/*` doc comment. **Zero warnings remain in any file this session
touched.**

---

## 4. Performance Data

| Metric | Before | After |
|--------|--------|-------|
| Radiometric calibration peak memory (per band) | O(width×height) floats (e.g. 10k×10k band = 400 MB) | O(256×256) ≈ 256 KB + 1 reuse buffer |
| Atmospheric DOS1/DOS2 peak memory | ~12 B/px (DN+output+radiance) | O(tile) + O(bins)=8 KB histogram |
| 10 GB+ GeoTIFF processing | OOM risk | streamable, no full-band allocation |
| Per-tile allocations in streaming path | — | zero (buffer capacity reused) |
| Worker crash recovery | job silently lost | in-flight request re-dispatched (≤1 retry) or errored ≤5 s |
| Shared-memory zero-copy round-trip (int32 2048²) | — | 1115.6 MiB/s (unchanged, regression-checked) |

---

## 5. Verification

- **Full clean rebuild** (2488 sources): **0 errors**; 0 warnings in any
  session-touched file. Remaining ~10.8k warnings are the documented
  pre-existing Qt-deprecation baseline in vendored QGIS core
  (`qgsvariantutils.h`, `qgsfield.h`, `qgsvectordataprovider.h`,
  `qgsrasterattributetable.h` — `QVariant::Type` family) plus the same
  deprecation family spread through vendored `src/core|gui|app`.
- **ctest: 1274/1274 = 100 % pass** (final gate, rc=0).
- **Known pre-existing flaky tests** (both failed in the previous session's
  Aug 6 run too — see `build/Testing/Temporary/LastTestsFailed.log`, and pass
  in isolation): `#977 QgisDisplayManager active view and auto-display
  tracking` (missing FastExitListener atexit guard), `#22 TaskCenter - Retry
  preserves a submitted job's auto-load preference` (JobEngine singleton
  teardown race under -j8 load). Not caused by this session; re-run green.
- **No QML** exists in the project (Qt Widgets app), so the `qt-qml-test-run`
  gate is N/A; ctest is the authoritative gate.
- Out-of-scope check: no SAR/LiDAR code paths touched.

---

## 6. Files Changed (this session)

**Modified (23):**
- `src/processing/algorithms/atmospheric_correction.{h,cpp}` — histogram dark
  object, `DarkObjectStats`, streaming processFile
- `src/processing/algorithms/radiometric_calibration.cpp` — streaming
  processFile, upfront validation, ParseResult
- `src/processing/gdal/gdal_dataset_wrapper.{h,cpp}` — `create`,
  `writeBandWindow`, `lastError` contract
- `src/python/isolated/python_ipc_server.{h,cpp}` — in-flight tracking,
  `takeInFlightRequests`
- `src/python/isolated/python_worker_process.{h,cpp}` — single crash signal
- `src/python/isolated/python_worker_process_pool.{h,cpp}` — replay + watchdog
  + `failPendingRequests`
- `src/python/isolated/python_plugin_adapter.{h,cpp}` — durable callbacks,
  bridge re-bind
- `src/app/widgets/spectral_profile_widget.{h,cpp}` — accessors, NaN-safe chart
- `src/app/main_window.h`, `src/app/main_window_menus.cpp` — deprecation fixes
- `src/app/classification/rs_classify_session_state.h`,
  `src/app/classification/qgsclassificationmainwindow.cpp` — warning fixes
- `tests/CMakeLists.txt` — swipe target registered, wrapper source added
- `tests/test_atmospheric.cpp`, `tests/test_radiometric_calibration.cpp`,
  `tests/test_python_plugin_manager.cpp`, `tests/test_spectral_profile_widget.cpp`,
  `tests/test_rs_operators.cpp`, `tests/test_gdal_wrapper.cpp`,
  `tests/test_virtual_raster_preflight.cpp`,
  `tests/test_collection_import_service.cpp`, `tests/test_python_plugin_host.cpp`

**New tests:** +25 test cases / +19 900 assertions across the suites above
(histogram DOS1: 9, streaming: 3 + operator run, recovery: 1, spectral
profile: 3 real-raster cases).

## 7. Deferred (documented, out of scope)

- `sendRequestSync` main-thread/worker-thread socket race (Agent 6 D-009):
  pre-existing, needs a targeted stress test before touching the sync path.
- `create()` vs `createOutputTiff()` ~25-line duplication: cosmetic; note for
  a future `TILED=YES`/`PREDICTOR` sweep (must land in both).
- Whole-band QUAC remains in-memory by design (full-scene percentile stats).
- At-least-once replay semantics: re-dispatched requests may re-execute if the
  first response was lost mid-wire; callers needing exactly-once should carry
  idempotency keys.
- Two pre-existing flaky tests (#22, #977) — see §5.
