# #291: [P1][correctness] RPC warp can never start: transformFromSnapshot never configures QgsRpcGcpTransformer (empty source RPC)

### Area
Core Remote Sensing / Georeferencing / Classification

### Type
correctness

### Severity
P1

### Affected code
- `src/app/georeferencer/rs_georeferencing_session.cpp:309-337` (RsGeoreferencingSession::transformFromSnapshot)

### User impact
- RPC Physical georeferencing - a first-class method in the I2M params panel - can never produce output through the GUI. User loses work with only a generic submission-failure message.

### Reproduction
- **Trigger (GEOREF-1)**: Image-to-Map window (the only profile exposing RPC per rs_georef_params_panel.cpp:133,599-606): open RPC-bearing raster, collect GCPs (fit succeeds via QgsGeorefTransform::fit which DOES configure RPC options through makeConfiguredTransform), press 运行/Apply.

### Expected behavior
- **Expected (GEOREF-1)**: The warp snapshot builds an RPC transformer configured with the snapshot's source raster path, DEM path, z-offset, refinement flag and destination CRS (mirroring makeConfiguredTransform), then warps.

### Actual behavior
- **Actual (GEOREF-1)**: The fresh QgsRpcGcpTransformer has empty mSrc, logs 'RPC transformer: source raster path is empty' and returns false; every RPC warp submission fails with a generic Task Center submit error. DEM/zOffset/GCP-refinement are unreachable on the only production warp path, and the #286 setDestinationCrs seam is unwired on the warp side. All session warp tests (test_georeferencing_session.cpp:539-680) use Linear; test_rpc_golden.cpp:72-75 bypasses the session and configures setRpcOptions directly, so no test covers this.

### Root cause
- **GEOREF-1**: CONFIRMED (rs_georeferencing_session.cpp:309-337 transformFromSnapshot creates a fresh QgsGeorefTransform(snap.method) via setMethod->QgsGcpTransformerInterface::create(RpcPhysical) -> QgsRpcGcpTransformer() with empty mSrc (qgsgcptransformer.cpp:91-92); never calls setRpcOptions/setDestinationCrs; QgsRpcGcpTransformer::updateParametersFromGcps returns false when mSrc.isEmpty() (qgsrpcgcptransformer.cpp:127-131) -> transformFromSnapshot returns nullptr -> startWarpTask -1 -> shell applyTransform '无法提交校正任务' (qgsgeoref_shell_window.cpp:1467-1470). Only production setRpcOptions caller is the fit-side makeConfiguredTransform helper (qgsgeoreftransform.cpp:41, used by refit/fit, NOT warp).)
  - *Callchain*: `QgsGeorefShellWindow::applyTransform (qgsgeoref_shell_window.cpp:1433-1467) -> RsGeoreferencingSession::createWarpSnapshot -> startWarpTask (339-414) -> transformFromSnapshot (309) -> QgsGeorefTransform::updateParametersFromGcps (qgsgeoreftransform.cpp:166) -> QgsRpcGcpTransformer::updateParametersFromGcps returns false -> transformFromSnapshot returns nullptr -> startWarpTask returns -1 -> applyTransform shows '无法提交校正任务' / 'Task Center 提交失败'.`
  - *Code context*: `Line 314: 'auto transform = std::make_unique<QgsGeorefTransform>( snap.method );' then only loadRaster + collectEnabledGcps + updateParametersFromGcps. No call to setRpcOptions()/setSourceRasterPath()/setDestinationCrs() anywhere in the function (grep: setRpcOptions production callers = only makeConfiguredTransform in qgsgeoreftransform.cpp:41). QgsGcpTransformerInterface::create(RpcPhysical) builds QgsRpcGcpTransformer() with default mSrc=""; updateParametersFromGcps (qgsrpcgcptransformer.cpp:127-131) returns false when 'mSrc.isEmpty()'.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #286 (closed) is the fit-side refinement/offset-units bug (fixed at HEAD top commit d9c59e5b82); #250 warp try/catch; #249 RPC_HEIGHT/RPC_DEM semantics; #251/#253 error-message minors. None cover the warp transform construction path. Not a dupe; genuinely new root cause.
- Finding GEOREF-1: Not #286 (that fixed refinement bias units in fit-side transformers); not #250 (warp try/catch); no closed issue covers the warp transform construction path. New root cause: setRpcOptions wiring exists only in fit()'s makeConfiguredTransform.


---

# #293: [P1][build/portability] cmake --install produces a broken installation: libsicnu_agent.so has no install rule and no INSTALL_RPATH is set anywhere

### Area
Build System / Packaging / Installation

### Type
build/portability

### Severity
P1

### Affected code
- `src/agent/CMakeLists.txt:2-51` (sicnu_agent)

### User impact
- Complete failure of the installed product (make install, AppImage via `make install DESTDIR=$APPDIR` in packaging/build-appimage.sh:21 — linuxdeploy cannot resolve libsicnu_agent.so from the AppDir either). CI's 'Verify Installation Rule Execution' step misses it because it only runs sicnu_geo_rs_cli (which does not link sicnu_agent) and is downstream of the currently-failing ctest step.

### Reproduction
- **Trigger (BUILD-1)**: Any `cmake --install` / `make install` / packaging that consumes the install tree (README.md 'Install' section documents `cmake --install . --prefix /usr/local`)

### Expected behavior
- **Expected (BUILD-1)**: install(TARGETS sicnu_agent LIBRARY DESTINATION lib) present; installed GUI app starts

### Actual behavior
- **Actual (BUILD-1)**: Installed app is unloadable; every installed deployment ships a binary that cannot run

### Root cause
- **BUILD-1**: CONFIRMED (P1): src/agent/CMakeLists.txt:2 add_library(sicnu_agent SHARED); grep of src/ finds install(TARGETS ...) rules only for sicnu_geo_rs, sicnu_geo_rs_cli, qgis_core/gui/native, sicnu_core, sicnu_processing, sicnu_operators, plugins - none for sicnu_agent. sicnu_data/sicnu_qgis_display/sicnu_task_center are STATIC libs (src/data/CMakeLists.txt:1 qt_add_library STATIC) and are statically linked, so sicnu_agent is the ONLY first-party SHARED lib lacking an install rule. Built sicnu_geo_rs DT_NEEDED libsicnu_agent.so and SONAME=libsicnu_agent.so (readelf-verified); installed prefix trees (skep1-install / install-test) contain no libsicnu_agent.so in lib/ and running installed bin/sicnu_geo_rs fails with 'error while loading shared libraries: libsicnu_agent.so' (install-test/gui.err).
  - *Callchain*: `cmake --install → bin/sicnu_geo_rs DT_NEEDED libsicnu_agent.so (readelf-verified) → ld.so cannot resolve → process exits before main()`
  - *Code context*: `src/agent/CMakeLists.txt:2 `add_library(sicnu_agent SHARED ...)` and src/app/CMakeLists.txt:296 links it into the `sicnu_geo_rs` executable, but `grep 'install(TARGETS' src/ cmake/ CMakeLists.txt` shows install rules for sicnu_geo_rs, sicnu_geo_rs_cli, qgis_core/gui/native, sicnu_core, sicnu_processing, sicnu_operators, plugins — none for sicnu_agent. Empirically reproduced by installing the audit build to a temp prefix: `$P/bin/sicnu_geo_rs` → exit 127 `error while loading shared libraries: libsicnu_agent.so: cannot open shared object file` (lib/ contains libqgis_*, libsicnu_core/processing/operators but no libsicnu_agent.so), while `$P/bin/sicnu_geo_rs_cli --help` exits 0.`

### Suggested fix
- Update CMakeLists.txt install rules, dependency discovery, and RPATH configuration to ensure correct runtime linking and packaging.

### References / Dedupe
- #180 (closed, P1, line 86) = 'sicnu_core shared library has no install() rule' - same defect class for sicnu_core (fixed src/core/CMakeLists.txt:2945); sicnu_agent explicitly NOT part of that fix, so BUILD-1 is a fresh same-class instance, not a dupe. #232 worker_daemon.py; #234 build/portability batch has no rpath/install-target item; #180 procedures are the model to copy.
- Finding BUILD-1: #180 covered the same class for sicnu_core (fixed at src/core/CMakeLists.txt:2945); sicnu_agent was not part of it. #232 was worker_daemon.py only.


---

# #295: [P2][ui/interaction] Launcher/base-class guard bugs make Fusion/ExtractBand/Terrain dialogs permanently unrunnable

### Area
GUI / Application Shell / Dialogs

### Type
ui/interaction

### Severity
P2

### Affected code
- `src/app/dialogs/fusion_dialog.cpp:24-166,168-278` (FusionDialog::onRun / RasterProcessingDialogBase::validateInputs)
- `src/app/dialogs/extract_band_dialog.cpp:94-118` (ExtractBandDialog::onRun / RasterProcessingDialogBase::validateInputs)
- `src/app/dialogs/terrain_dialog.cpp:106-139 (onRun; setRasterLayer at 129, output auto-fill 115-124)` (TerrainDialog::onRun vs RasterProcessingDialogBase::validateInputs (raster_processing_dialog_base.cpp:65-83))

### User impact
- Image Fusion / pan-sharpening feature is completely non-functional on the classic dialog path (workflow-shell tool.rs.image_fusion is the only remaining path); auto-derived output path logic is dead
- Extract Band tool dead whenever invoked without a raster as current layer; user gets a misleading error suggesting a layer problem while the dialog's own combo selection is ignored
- Terrain analysis via the legacy fallback path is 100% broken with a misleading error; auto output naming never works

### Reproduction
- **Trigger (DLGA-1)**: Open 影像融合/全色锐化 from the menu in the classic shell (no workflow session controller), select pan + MS layers and a method, type an output path, click 运行
- **Trigger (DLGA-2)**: Open 提取波段 from the menu while the canvas current layer is a vector or nothing is selected, then choose layer/band and click 运行
- **Trigger (DLGB-5)**: Open the legacy Terrain Analysis dialog (only reachable when m_sessionController is absent; in the shipped app setupRibbonAndTaskPanel creates it unconditionally, so the menu normally routes to the workflow tool instead)

### Expected behavior
- **Expected (DLGA-1)**: Dialog collects pan/MS from its own combos (as onRun does) and submits rs:image_fusion / the CLI pansharpen path; empty output auto-derives '<ms>_fused.tif' per lines 192-198
- **Expected (DLGA-2)**: Dialog validates its own combo selection (like ApplyMaskDialog::validateInputs does) and auto-suggests '<input>_bandN.tif' when the output field is empty
- **Expected (DLGB-5)**: Dialog validates against the combo-selected DEM and auto-fills the output name, then runs rs:terrain_analysis

### Actual behavior
- **Actual (DLGA-1)**: First click warns 请指定输出文件路径。 (output empty); after typing a path every click warns 未选择有效的栅格图层。 and onRun() is unreachable — the dialog can never execute anything
- **Actual (DLGA-2)**: Every Run click is rejected: 请指定输出文件路径。 when output empty, then 未选择有效的栅格图层。 after filling it — the dialog can never run; the auto-suggest code is dead
- **Actual (DLGB-5)**: Run is always rejected with '未选择有效的栅格图层。' (or '请指定输出文件路径。' first) regardless of the selected DEM; the whole dialog is non-functional dead code

### Root cause
- **DLGA-1**: CONFIRMED as code defect but LATENT/UNREACHABLE in the shipped app (corrected P3): FusionDialog has no validateInputs/setRasterLayer use (dialog ctor+onRun verified) and its launcher openRasterDialog<FusionDialog> passes nullptr - BUT openFusionDialog (main_window_processing.cpp:474-482) branches to openWorkflowTool('tool.rs.image_fusion') whenever m_sessionController is set, and m_sessionController is created UNCONDITIONALLY in the window ctor (main_window.cpp:129 -> setupRibbonAndTaskPanel -> main_window_docks.cpp:543 new WorkflowSessionController). The finder's own trigger requires m_sessionController==null which never holds in this app.
  - *Callchain*: `Ribbon/menu 分析→影像融合 → QgisDesktopWindow::openFusionDialog (m_sessionController==null) → openRasterDialog<FusionDialog> (no setRasterLayer) → dlg.exec() → user picks pan/MS in combos, clicks 运行 → RasterProcessingDialogBase lambda → validateInputs() → base impl fails on !m_rasterLayer → onRun() never invoked`
  - *Code context*: `fusion_dialog.cpp has no validateInputs() or setRasterLayer() override (header declares neither). The Run button path is raster_processing_dialog_base.cpp:192-195 `connect( m_runButton, &QPushButton::clicked, this, [this]() { if ( validateInputs() ) onRun(); } );` and base validateInputs (raster_processing_dialog_base.cpp:76-80) does `if ( !m_rasterLayer || !m_rasterLayer->isValid() ) { QMessageBox::warning( this, dialogTitle(), tr( "未选择有效的栅格图层。" ) ); return false; }`. FusionDialog never assigns m_rasterLayer: its launcher main_window_processing.cpp:474-482 is `openRasterDialog<FusionDialog>(this, tr( "Image Fusion" ));` with the third argument defaulted to nullptr (template at main_window_processing.cpp:97-107 only calls dialog.setRasterLayer(rasterLayer) `if (rasterLayer)`), and onRun() (fusion_dialog.cpp:170-171) reads layers from mPanCombo/mMsCombo but never calls setRasterLayer. Consequently lines 192-198 (`if ( outPath.isEmpty() ) { outPath = ... "_fused.tif"; ... }`) are dead code: base validateInputs rejects an empty output path first (line 68-74).`
- **DLGA-2**: CONFIRMED and LIVE (P2): launcher main_window_menus.cpp:406-415 sets m_rasterLayer only if canvas currentLayer is raster; with no raster selected base validateInputs (raster_processing_dialog_base.cpp:76-80) rejects every Run despite the dialog's own working layer combo; auto-suggest output path (extract_band_dialog.cpp:110-118) is permanently dead behind the empty-outputPath early reject (:67-74).
  - *Callchain*: `预处理→提取波段… (no raster current layer) → dlg.exec() → user picks layer+band in combo, clicks 运行 → base Run lambda → RasterProcessingDialogBase::validateInputs → fails (!m_rasterLayer) → onRun() never invoked; the m_outputEdit->setText auto-suggest at line 117 can never execute`
  - *Code context*: `ExtractBandDialog does not override validateInputs() (header lines 17-32 declare none). Launch site main_window_menus.cpp:406-415 only sets the layer conditionally: `ExtractBandDialog dlg( this ); if ( m_mapCanvas && m_mapCanvas->currentLayer() ) { if ( auto *rl = qobject_cast<QgsRasterLayer *>( m_mapCanvas->currentLayer() ) ) dlg.setRasterLayer( rl ); } dlg.exec();`. With no current layer (or a vector selected) m_rasterLayer stays nullptr, so base validateInputs (raster_processing_dialog_base.cpp:76-80) rejects every Run with 未选择有效的栅格图层。 even though the dialog has its own working layer combo. The auto-derive branch extract_band_dialog.cpp:110-118 (`if ( outPath.isEmpty() ) { outPath = ... tr( "_band%1.tif" ).arg( bandIndex ); m_outputEdit->setText( outPath ); }`) is unreachable: base validateInputs rejects an empty outputPath (lines 67-74) before onRun() is ever called.`
- **DLGB-5**: CONFIRMED as code defect but LATENT/UNREACHABLE (corrected P3): TerrainDialog::onRun sets m_rasterLayer at :129 only after validateInputs already required it - BUT openTerrainDialog (main_window_processing.cpp:460-468) routes to openWorkflowTool('tool.rs.terrain_analysis') whenever m_sessionController is non-null (always true in shipped app); legacy branch dead.
  - *Callchain*: `menu 地形分析 -> openTerrainDialog -> (legacy branch) openRasterDialog<TerrainDialog> (no layer set) -> user selects DEM in combo, clicks 运行 -> RasterProcessingDialogBase::validateInputs -> m_rasterLayer == nullptr -> warning box, onRun never invoked`
  - *Code context*: `Base Run button: 'if ( validateInputs() ) onRun();' (raster_processing_dialog_base.cpp:192-195). Base validateInputs rejects empty outputPath ('请指定输出文件路径。') and rejects '!m_rasterLayer || !m_rasterLayer->isValid()' ('未选择有效的栅格图层。'). TerrainDialog does not override validateInputs; m_rasterLayer is nullptr until onRun line 129 'setRasterLayer( rl );' — code that can only execute after validateInputs already required a non-null m_rasterLayer. Likewise the output-name auto-fill (lines 115-124) sits after the empty-path rejection. The only opener passes no layer: openTerrainDialog -> 'openRasterDialog<TerrainDialog>(this, tr("Terrain Analysis"));' with default rasterLayer=nullptr (main_window_processing.cpp:460-468, 96-109). onAnalysisFinished/mWatcher are also dead (never connected).`

### Suggested fix
- Ensure widgets are fully initialized or stubbed safely, signals/slots are correctly connected to backend results, and UI elements do not block the main event loop.

### References / Dedupe
- No closed issue covers fusion/extract_band/terrain launcher guards: #182 = ImageEnhancementPanel/BandRatio band combos (different, fixed); #203 = terrain operator cellSize default (different). Grep issues-closed.txt 'fusion/extract band/terrain' empty.
- Finding DLGA-1: No closed issue matches: #182 (band combos in ImageEnhancementPanel/BandRatioDialog) is a different defect and was fixed in cac5da9469; grep of issues-closed.txt for fusion shows nothing; verified current HEAD d9c59e5b8290 still has no validateInputs override in fusion_dialog.{h,cpp}
- Finding DLGA-2: No closed issue mentions extract_band; #182 covered ImageEnhancementPanel/BandRatioDialog combos only; verified HEAD still lacks validateInputs override in extract_band_dialog.{h,cpp}
- Finding DLGB-5: #203 covered the terrain operator cellSize default — different defect; verified at HEAD


---

# #297: [P2][data/crs-nodata-metadata] Output NoData/metadata contract violations batch 2: writeGdalOutput callers, Byte change/threshold masks (255), spectral_resample, stackToGeoTiff

### Area
Data Model / Raster Grids / NoData & CRS

### Type
data/crs-nodata-metadata

### Severity
P2

### Affected code
- `src/processing/gdal/gdal_dataset_wrapper.cpp:507-549` (writeGdalOutput (free function))
- `src/operators/rs/rs_change_streaming.cpp:240-267 (writeMaskFromMagnitude mask write); src/processing/algorithms/change_detection.cpp:57 (mask[i]=255); src/operators/rs/rs_threshold_raster_operator.cpp:118 (same kernel)` (writeMaskFromMagnitude / ChangeDetection::changeMask)
- `src/operators/rs/rs_spectral_resample_operator.cpp:217-226, 253-270, 290-299` (RsSpectralResampleOperator::run)
- `src/processing/gdal/gdal_dataset_wrapper.cpp:507-547` (writeGdalOutput / createOutputTiff)
- `src/processing/algorithms/satellite_products.cpp:1379-1416, 1446-1497` (SatelliteProducts::stackToGeoTiff)

### User impact
- Same downstream corruption class as #202: means/stddevs poisoned by sentinels, change-detection false positives on NoData regions; silent because the values are visually obvious but statistically included.
- NoData regions (scene edges, clouds) become 'change' in every downstream mask application; area statistics and visual results are wrong for exactly the pixels most likely to be invalid.
- Same #202-family contract violation; library-matching and index workflows on resampled cubes silently ingest NaN as data.
- Same downstream-contract violation class as #202, now via the shared writer used by seven call sites — any fix must add a nodata parameter to writeGdalOutput
- MODIS-vs-Sentinel change detection passes the radiometric-comparability gate yet compares values differing by 10^4; NoData fill pixels enter every downstream statistic. The #202-style contract hole is re-opened at the import seam.

### Reproduction
- **Trigger (RSALGO-4)**: Run QUAC / band math / terrain slope-aspect-hillshade on any raster with NoData pixels, then open the output downstream (statistics, change detection, mosaic)
- **Trigger (OPS-4)**: rs:change_detection with makeMask=true (or method=change_mask, or rs:threshold_raster) on a scene containing NaN/NoData pixels, followed by rs:apply_mask (or any >0-mask consumer) using the mask.
- **Trigger (OPS-8)**: Resampling to a target grid that extends beyond the source wavelength range (e.g. adding a 443 nm target to a 480-900 nm cube) — output band is entirely NaN with no declared NoData.
- **Trigger (PROVG-7)**: Any of these outputs containing NoData/NaN pixels (e.g. IHS fusion of partial-coverage bands writes NaN at image_enhancement_panel.cpp:419)
- **Trigger (RSALGO-7)**: Import any MODIS MOD09 HDF or Sentinel-2 L1C SAFE and stack it; run change detection against a calibrated (0-1) partner

### Expected behavior
- **Expected (RSALGO-4)**: Output band carries a NoData declaration matching the emitted sentinel (NaN or -9999)
- **Expected (OPS-4)**: GDAL band NoData=255 declared on the Byte mask so every consumer can distinguish NoData from 1=changed (and apply_mask could skip 255 cells).
- **Expected (OPS-8)**: Band NoData = NaN declared (as continuum_removal and unmixing do at rs_continuum_removal_operator.cpp:116-121, rs_spectral_unmixing_operator.cpp:179-190).
- **Expected (PROVG-7)**: Output bands declare the sentinel actually written (NaN or the operator's sentinel), per the #202 output contract
- **Expected (RSALGO-7)**: Band NoData (and scale/offset semantics) preserved on output bands, or values physically converted before declaring the state

### Actual behavior
- **Actual (RSALGO-4)**: No band NoData metadata; downstream consumers treat NaN (renders as gaps/0) or -9999 as valid data and include them in statistics
- **Actual (OPS-4)**: NoData cells are indistinguishable from data; they read as 255 and are classified as masked/changed by the apply_mask contract.
- **Actual (OPS-8)**: NaN sentinel written but undeclared; downstream declared-NoData consumers treat the NaN band as valid data.
- **Actual (PROVG-7)**: Float32 bands with NaN content and no declared NoData; non-QGIS consumers (GDAL stats, scripts, agents) treat sentinels as data
- **Actual (RSALGO-7)**: NoData declaration lost on every stacked band; state metadata claims reflectance while pixels are scaled DN

### Root cause
- **RSALGO-4**: CONFIRMED (writeGdalOutput gdal_dataset_wrapper.cpp:507-547 has no NoData API; QUAC atmcorr.cpp:371, band_math.cpp:643, terrain rs_terrain_analysis_operator.cpp:168 all write sentinels via it; contrast paths declare at atmcorr.cpp:407 / radcal.cpp:524)
  - *Callchain*: `rs:atmospheric_correction(method=quac) -> AtmosphericCorrection::processFileMultiBand:371; rs:band_math -> BandMath::processFile:643; rs:terrain_analysis -> RsTerrainAnalysisOperator::run:168 (all -> writeGdalOutput)`
  - *Code context*: `writeGdalOutput creates the output TIFF and writes each band but never calls GDALSetRasterNoDataValue on any band (verified full function body). In-scope callers that emit sentinel-valued pixels: AtmosphericCorrection::processFileMultiBand -> quac writes NaN pixels (atmospheric_correction.cpp:371); BandMath::processFile writes NaN from safeDiv (band_math.cpp:641-644); RsTerrainAnalysisOperator::run writes nodata=-9999 or NaN cells (rs_terrain_analysis_operator.cpp:166-169). Contrast with the fixed single-band paths which do declare: AtmosphericCorrection::processFile:407 and RadiometricCalibration::processFile:524 call outDataset.setBandNoDataValue(NaN) - only the writeGdalOutput-based paths were missed.`
- **OPS-4**: CONFIRMED (change_detection.cpp:57 mask[i]=255 'No data'; rs_change_streaming.cpp writeMaskFromMagnitude sets only SICNU_CHANGE_* meta, no GDALSetRasterNoDataValue; threshold operator schema documents 255=NoData; apply_mask consumer tests maskBuf>0.0f at rs_apply_mask_operator.cpp:363)
  - *Callchain*: `MCP/CLI execute_algorithm -> RsOperatorAdapter::execute -> RsChangeDetectionOperator::run / RsThresholdRasterOperator::run -> runChangeStreaming -> writeMaskFromMagnitude; consumer: rs:apply_mask reads the mask raster band 1 with `> 0 = masked`.`
  - *Code context*: `writeMaskFromMagnitude creates the Byte output via createOutputTiff, writes the mask with GDALRasterIO, and sets only SICNU_CHANGE_* metadata items — there is no GDALSetRasterNoDataValue(maskBand, 255) anywhere in the function (240-267). The kernel sets 255 for non-finite magnitudes (change_detection.cpp:57) and the operator metadata explicitly documents '255 marks NoData pixels in the output mask' (rs_threshold_raster_operator.cpp:73). rs:apply_mask's mask contract is 'value > 0 = masked' (rs_apply_mask_operator.cpp:127, 363: `maskBuf[off] > 0.0f`), so 255 cells are treated as masked; rs:qa_mask outputs use the same 0/1 convention with no 255 escape.`
- **OPS-8**: CONFIRMED (spectral_resample writes quiet_NaN for out-of-range LUT entries, sets only WAVELENGTH meta; continuum_removal:120 and unmixing:179-190 declare NaN as contrast)
  - *Callchain*: `MCP/CLI execute_algorithm -> RsOperatorAdapter::execute -> RsSpectralResampleOperator::run.`
  - *Code context*: `Output created via createOutputTiff (221-223) and bands written with GDALRasterIO (277-285); the only band metadata set afterwards is WAVELENGTH/WAVELENGTH_UNITS (292-298) — no GDALSetRasterNoDataValue despite out-of-range targets being written as quiet_NaN (261-264) and metadata documenting 'target wavelengths outside the source range yield NaN'. Input SICNU_BAND_ROLE/FWHM are also not carried to the mapped bands.`
- **PROVG-7**: CONFIRMED (GUI instances verified: image_enhancement_panel.cpp:419 IHS NaN arg -> :445 writeGdalOutput; contrast_stretch_dialog.cpp:184; extract_band_dialog.cpp:144)
  - *Callchain*: `Ribbon enhancement panel / contrast stretch dialog / rs:atmospheric_correction -> writeGdalOutput -> createOutputTiff (no band NoData set)`
  - *Code context*: `writeGdalOutput creates the GeoTIFF via createOutputTiff (COMPRESS/TILED options only) and writes bands; there is no GDALSetRasterNoDataValue call and no nodata parameter in the API at all. Callers that write NaN sentinels: image_enhancement_panel.cpp:419 (IHS NaN argument) -> :445 writeGdalOutput; contrast_stretch_dialog.cpp:184; atmospheric_correction.cpp:371; extract_band_dialog.cpp:144; plus band_math/spectral-index/terrain (covered by OPS-1).`
- **RSALGO-7**: CONFIRMED (stackToGeoTiff band loop copies pixels/desc/WAVELENGTH/FWHM/ROLE only - no NoData, no SCALE/OFFSET; contrast copyRasterPixels:332-335 copies NoData; MOD09/MYD09 stamped SurfaceReflectance over verbatim 0-10000 DN; change-detection gate rs_change_detection_operator.cpp:247-257 compares labels only, so MOD09-vs-calibrated passes while differing by 1e4)
  - *Callchain*: `product import (Landsat/Sentinel-2/MODIS discovery) -> SatelliteProducts::stackToGeoTiff; downstream rs:change_detection trusts SICNU_RADIOMETRIC_STATE for comparability (ADR 0114)`
  - *Code context*: `Band copy loop reads/writes Float32 and copies description + WAVELENGTH/FWHM/SICNU_BAND_ROLE metadata but never GDALSetRasterNoDataValue(dstBand, ...) nor SCALE/OFFSET from the source band (contrast copyRasterPixels:332-335 which does copy NoData). Radiometric-state block then asserts e.g. MOD09/MYD09 -> kRadiometricStateSurfaceReflectance (line 1470-1473) even though MODIS HDF scale_factor (typically 1e-4) was not applied - the stacked 'surface reflectance' holds 0-10000 raw DN. Sentinel-2 L1C is declared toa_reflectance while holding quantified DN (0-10000) for the same reason.`

### Suggested fix
- Validate grid compatibility rigorously, propagate NoData masks across all processing steps, and preserve CRS/metadata on output datasets.

### References / Dedupe
- Clean vs issues-closed.txt: #202 (line 64) covers mosaic/SAM/RX only; #173 VRT NoData is the provider path; no closed issue covers writeGdalOutput callers, Byte 255 masks, spectral_resample, or stackToGeoTiff.
- Finding RSALGO-4: #202 fixed mosaic + SAM (-9999) + RX (NaN) outputs only; the writeGdalOutput family (QUAC/band-math/terrain + GUI dialogs contrast_stretch_dialog.cpp:184, extract_band_dialog.cpp:144, image_enhancement_panel.cpp:445) was not covered - fix incomplete at current HEAD.
- Finding OPS-4: #202 fixed output-NoData declarations for SAM (-9999), RX (NaN), mosaic, kmeans (commit b3fe107887 touched only those four operator files). The change-mask Byte path was not covered; verified missing at HEAD.
- Finding OPS-8: #202 fixed kmeans/mosaic/rx/sam only; spectral_resample not in that commit (b3fe107887) and still missing at HEAD.
- Finding PROVG-7: #202 fixed mosaic/SAM/RX only; OPS-1 covers the headless rs operator paths and cites this file — this finding adds the GUI dialog + atmospheric_correction instances and the missing-writer-API framing. Verified at HEAD.
- Finding RSALGO-7: #173 fixed VRT NoData for the virtual-raster provider; #202 fixed operator outputs. Stack import path not covered in issues-closed.txt.


---

# #299: [P2][algorithm/numerics] Class-map writers still destructively overwrite outputs before validation and delete them on failure (#285 temp+rename fix not propagated to pipeline/class raster writers)

### Area
Algorithms / Image Processing / Numerics

### Type
algorithm/numerics

### Severity
P2

### Affected code
- `src/analysis/classification/rs_classification_pipeline.cpp:596-621 (Create on target path), 699-712 (failWithPartialOutput removes it)` (RsClassificationPipeline::run)
- `src/analysis/segmentation/rs_class_raster.cpp:paint 87-89 (GDALCreate), 98-115 (reference opened/validated AFTER create), 113/183/218 (removeIncompleteOutput); polygonize 265-266 (GDALCreate of vector), 308-364 (mask build can fail after create), 311/321/332/348/361/376 (removeIncompleteOutput)` (RsClassRaster::paint / RsClassRaster::polygonize)

### User impact
- User loses the prior classification result of a long-running whole-scene job; same data-loss class as closed issue #285 which was only fixed in rs_post_process.cpp.
- Destroys the user's previous class raster/vector on a failed re-run - the exact defect class of #285, still live in the OBIA writeback path.

### Reproduction
- **Trigger (RSCLS-3)**: Re-run classification to the same output path and have any late step fail: read error at a tile, prediction exception, scaler band mismatch, or user cancel mid-predict.
- **Trigger (RSHYP-2)**: Re-run an OBIA classify/export to the same outputPath as a previous good result and hit any post-Create failure: reference grid mismatch (segment map from a different raster), disk full during row writes, or an unreadable class raster row in polygonize.

### Expected behavior
- **Expected (RSCLS-3)**: Write to a sibling temp dataset and rename on success (the exact pattern commit 26f4566474 implemented for RsPostProcess::saveLabelRaster/polygonize).
- **Expected (RSHYP-2)**: Output written atomically (temp file + rename, as implemented for #285 in classification) or at least validated before the existing file is truncated; failure leaves the previous result intact.

### Actual behavior
- **Actual (RSCLS-3)**: Previous output is truncated at Create and then QFile::remove'd by the failure handler; nothing left on disk.
- **Actual (RSHYP-2)**: GDALCreate truncates the previous output first; every late failure path then deletes it, so the user loses both the old and the new result (polygonize additionally leaves orphaned .dbf/.shx sidecars).

### Root cause
- **RSCLS-3**: CONFIRMED (rs_classification_pipeline.cpp:596-614 GTiff Create truncates existing output; failWithPartialOutput :699-712 QFile::remove's output AND probability output; reachability: rs_supervised_classification_operator.cpp:247-268 + qgsclassificationmainwindow + rs_kmeans_operator)
  - *Callchain*: `GUI Apply (module:classify:apply) and rs:supervised_classification operator -> RsClassificationPipeline::run; also the .tmp~ preview path is safe but Apply writes straight to the user-chosen output`
  - *Code context*: `GDALDataset *dstDs = drv->Create( config.outputRaster.toUtf8().constData(), outW, outH, 1, outType, papsz ); ... failWithPartialOutput: QFile::remove( config.outputRaster ); — GTiff Create truncates the existing file in place before any tile is written.`
- **RSHYP-2**: CONFIRMED (rs_class_raster.cpp paint: GDALCreate at :87 before GDALOpen(reference) at :98, grid-mismatch removeIncompleteOutput at :113; polygonize GDALCreate :265 before mask build, 6 removeIncompleteOutput sites; contrast RsSegmentMap::toGeoTIFF validates reference BEFORE create; callers rs_obia_task.cpp:183, rs_obia_main_window.cpp:1362/1642, rs_obia_hierarchy_operator.cpp:229)
  - *Callchain*: `OBIA GUI tasks: src/app/obia/rs_obia_task.cpp:183 and src/app/obia/rs_obia_main_window.cpp:1362; operators: src/operators/rs/rs_obia_hierarchy_operator.cpp:229 (and the obia_classify paint path) -> RsClassRaster::paint(segMap, segmentClasses, referenceRasterPath, outputPath, classColors) -> RsClassRaster::polygonize`
  - *Code context*: `paint: 'GDALDatasetH dstDs = GDALCreate( driver, outputPath.toUtf8().constData(), w, h, 1, outType, papszOptions );' (line 87) truncates any existing file at outputPath; only afterwards 'GDALDatasetH srcDs = GDALOpen( referenceRasterPath... )' (line 98) and on grid mismatch 'removeIncompleteOutput( outputPath ); return result;' (line 113). GTiff Create opens the file for writing immediately, so the previous output is already gone at that point; mid-write RasterIO failures likewise 'removeIncompleteOutput( outputPath )' (183, 218). polygonize creates the shapefile at line 265 before the per-row class-raster mask build (339-365) which fails closed on the first unreadable row and then deletes the .shp - leaving stale .dbf/.shx sidecars beside a deleted .shp. Contrast RsSegmentMap::toGeoTIFF (rs_segment_map.cpp:99-131) which correctly validates the reference BEFORE GDALCreate.`

### Suggested fix
- Correct the mathematical formulas, coordinate system transforms, and NoData boundary conditions to align with standard GIS conventions (Horn 1981, GDAL, IEEE-754).

### References / Dedupe
- #285 (line 4) fix 26f4566474 touched ONLY rs_post_process.cpp + tests (git show --stat verified) - both members are genuine incomplete-fix siblings, not dupes.
- Finding RSCLS-3: #285 closed by 26f4566474 — git show --stat: only rs_post_process.cpp + tests touched; pipeline path still destructive at HEAD.
- Finding RSHYP-2: #285 was fixed in commit 26f4566474 which touched ONLY src/analysis/classification/rs_post_process.cpp (verified via git show --stat); git log shows rs_class_raster.cpp untouched since creation (1444e71885). Genuinely incomplete fix of #285.


---

# #301: [P2][algorithm/numerics] Radiometric operators silently fall back to identity gain/bias when calibration metadata is absent and stamp output as radiance/reflectance

### Area
Algorithms / Image Processing / Numerics

### Type
algorithm/numerics

### Severity
P2

### Affected code
- `src/operators/rs/rs_radiometric_calibration_operator.cpp:114-142; src/processing/algorithms/radiometric_calibration.cpp:88-121,377-382,498-511` (RsRadiometricCalibrationOperator::run / RadiometricCalibration::toRadiance / loadLandsatMtl)
- `src/operators/rs/rs_atmospheric_correction_operator.cpp:148-151, 199-229, 236-269` (atmospheric_detail::runAtmosphericCorrectionCore)

### User impact
- Quantitatively wrong radiance products enter the calibration -> atmospheric-correction -> change-detection chain with a trusted radiometric-state stamp, defeating the ADR-0114 comparability guard itself.
- Users get DN labeled as radiance/surface-reflectance without any signal; downstream radiometric-state checks are poisoned the same way as OPS-2.

### Reproduction
- **Trigger (OPS-2)**: rs:radiometric_calibration with unit='radiance' on a Landsat Collection 2 Level-2 SR stack (no RADIANCE_MULT_BAND_x in MTL) or on any generic product carrying only GDAL SCALE/OFFSET metadata.
- **Trigger (OPS-3)**: rs:dn_to_radiance (or atmospheric_correction method=dn_to_radiance/dos1/dos2) on a stacked raster whose MTL/MTD is not a sibling of the input and where the caller omitted gain/bias.

### Expected behavior
- **Expected (OPS-2)**: Deterministic failure ('no radiance coefficients for band N') mirroring the reflectance-path guard, before any output is created.
- **Expected (OPS-3)**: A typed error or at least a warning ('radiance gain/bias unresolved; pass gain/bias or metadata_path') — the schema advertises 'resolved from product metadata when omitted' as if it always succeeds.

### Actual behavior
- **Actual (OPS-2)**: Output is an identity copy of DN labeled and stamped as radiance; run returns success.
- **Actual (OPS-3)**: Silent identity transform; output stamped as radiance (or used as-is for DOS scaling).

### Root cause
- **OPS-2**: CONFIRMED (BandCoefficients defaults gain=1/bias=0 radiometric_calibration.h:46-53; loadLandsatMtl :113-116 inserts band on ANY coefficient match so L2-SR MTL (REFLECTANCE_MULT only) yields identity radiance; toRadiance :377-382 is an unguarded linearScale; toToaReflectance Landsat branch guards at :393-394 - the codebase's own standard; up-front validation :500-511 checks only band presence; operator stamps state=radiance unconditionally rs_radiometric_calibration_operator.cpp:150-158)
  - *Callchain*: `MCP/CLI execute_algorithm -> RsOperatorAdapter::execute -> RsRadiometricCalibrationOperator::run -> RadiometricCalibration::processFile -> toRadiance; output state then consumed by rs:change_detection's radiometric comparability check (rs_change_detection_operator.cpp:244-256), which now trusts the wrong state.`
  - *Code context*: `BandCoefficients defaults radianceGain=1.0/radianceBias=0.0 (radiometric_calibration.h:66-68). loadLandsatMtl inserts a band when ANY coefficient matched (radiometric_calibration.cpp:113-116), so a Landsat C2 L2 SR MTL (REFLECTANCE_MULT only, no RADIANCE_MULT_BAND_x) yields gain=1/bias=0. toRadiance() (377-382) is MathUtils::linearScale with no 'coefficients missing' guard, unlike toToaReflectance's Landsat branch which does guard (393-394: `if (c.reflMult == 1.0 && c.reflAdd == 0.0) return false;`). loadGdalMetadata (265-312) sets only scale/offset, which toRadiance ignores entirely. The up-front validation (500-511) checks only meta.bands.contains(b), not unit-specific coefficients. The operator then stamps SICNU_RADIOMETRIC_STATE=radiance on the output (operator lines 150-158).`
- **OPS-3**: CONFIRMED (rs_atmospheric_correction_operator.cpp:148-151 defaults; :199-229 resolution chain silently falls through on every failure (no metadata, parse fail, band missing) with no warning; :263-269 stamps radiance/surface-reflectance; schema text at :45-46/:69 literally advertises 'when omitted, resolved from product metadata' - contract violation is user-visible)
  - *Callchain*: `MCP/CLI execute_algorithm -> RsOperatorAdapter::execute -> runAtmosphericCorrectionCore -> AtmosphericCorrection::processFile`
  - *Code context*: `gain/bias default to 1.0/0.0 (150-151). The metadata resolution block (199-229) only overwrites them when a metadata file is found, loads, and contains the band; every failure path (no MTL/MTD sibling, XML parse failure, band not in coefficients) falls through silently with no warning or error. processFile then runs `out[i] = gain * v + bias` (atmospheric_correction.cpp pass 3), i.e. DN passthrough for dn_to_radiance, and the operator stamps SICNU_RADIOMETRIC_STATE=radiance (263-269).`

### Suggested fix
- Correct the mathematical formulas, coordinate system transforms, and NoData boundary conditions to align with standard GIS conventions (Horn 1981, GDAL, IEEE-754).

### References / Dedupe
- #127/#128/#129 (lines 105-107) built the features; no closed issue covers the identity fallback or unresolved gain/bias. Clean.
- Finding OPS-2: #127/#125 built this operator; no closed issue covers the radiance identity-passthrough (grep of issues-closed.txt: nothing on radiance defaults). Verified at HEAD.
- Finding OPS-3: No closed issue covers unresolved atmospheric gain/bias defaults (#129 added QUAC, #127 the operator). Verified at HEAD: no error branch exists when the metadata block fails.


---

# #303: [P2][processing/provider] gdal tool wrappers construct always-failing or precision-truncating CLI invocations (grid -a_nodata, gdalmanage default action, proximity -targetvalue, reproject/dstnodata 6-decimal formatting)

### Area
Processing Framework / GDAL & OTB Providers

### Type
processing/provider

### Severity
P2

### Affected code
- `src/processing/providers/gdal_tools/algorithms/gdal_grid.cpp:67-70, 82-87` (GdalGridAlgorithm::buildArgs)
- `src/processing/providers/gdal_tools/algorithms/gdalmanage.cpp:11-13, 26-30` (GdalManageAlgorithm::initAlgorithm / buildArgs)
- `src/processing/providers/gdal_tools/algorithms/gdal_proximity.cpp:43-45` (GdalProximityAlgorithm::buildArgs)
- `src/operators/gdal/gdal_reproject_operator.cpp:162-171` (GdalReprojectOperator::run)

### User impact
- gdal:grid is unusable in the default dialog flow and in the respecified-pixel-size flow — always-failing tool with a cryptic downstream error
- The default and most common gdal:manage action never runs; user gets a generic failure
- Proximity restricted to a target value can never run; only the whole-raster distance mode works
- Grid-alignment precision defect in the primary reprojection entry point; downstream same-grid operators (change detection, apply_mask) then reject or misplace the result.

### Reproduction
- **Trigger (PROVG-1)**: Run gdal:grid from the toolbox with the default NODATA (-9999), or set PIXEL_SIZE without EXTENT
- **Trigger (PROVG-2)**: Run gdal:manage without changing ACTION (default 'info')
- **Trigger (PROVG-3)**: Run gdal:proximity with TARGET_VALUE set
- **Trigger (OPS-6)**: gdal:reproject with targetResolution in degrees, e.g. 2.69495e-5 (~3 m) or 8.98315e-5 (~10 m): to_string yields '0.000027'/'0.000090', rounding the pixel size by up to ~2%; nodata like -3.4e38 serializes as a 39-digit fixed string.

### Expected behavior
- **Expected (PROVG-1)**: Wrapper emits only flags gdal_grid supports (drop -a_nodata or use a supported mechanism); -tr accompanied by extent or translated to -outsize
- **Expected (PROVG-2)**: args << "identify" maps to a real mode and the tool reports dataset info
- **Expected (PROVG-3)**: -values <n> (possibly comma-separated list) is emitted
- **Expected (OPS-6)**: util::fmtDouble(targetResolution)/fmtDouble(nodata) as used by the reference path and gdal:clip.

### Actual behavior
- **Actual (PROVG-1)**: gdal_grid aborts with 'Unknown argument: -a_nodata' (or ERROR 5 for -tr-only); algorithm throws 'GDAL tool gdal_grid.py failed'
- **Actual (PROVG-2)**: gdalmanage rejects 'info' as an unknown mode; algorithm throws 'GDAL tool gdalmanage failed'
- **Actual (PROVG-3)**: Unknown option -targetvalue -> tool exits non-zero -> 'GDAL tool gdal_proximity failed'
- **Actual (OPS-6)**: Rounded 6-decimal values are passed to GDALWarp; output pixel size deviates from the requested resolution, accumulating to tens of pixels of misalignment across a large scene and breaking exact grid alignment with co-registered products.

### Root cause
- **PROVG-1**: CONFIRMED empirically on system GDAL 3.13.2 (= linked version): 'gdal_grid -a_nodata -9999 ...' -> 'ERROR 1: Unknown argument: -a_nodata' (flag absent from --help); '-tr without -txe/-tye' -> 'ERROR 5: -txe ad -tye arguments must be provided when resolution is provided'. Wrapper code gdal_grid.cpp:67-70,82-87 as cited; NODATA declared with default -9999 (:24-26) so dialog flow always trips
  - *Callchain*: `Toolbox dialog / pipeline JSON -> TaskCenter -> ProviderAlgorithmAdapter::execute -> GdalToolWrapper::processAlgorithm -> GdalGridAlgorithm::buildArgs -> gdal_grid subprocess (exit 1)`
  - *Code context*: `if (parameters.contains("NODATA") && !parameters.value("NODATA").toString().isEmpty()) { double nodata = parameters.value("NODATA").toDouble(); args << "-a_nodata" << QString::number(nodata); } — NODATA is declared with default -9999 (initAlgorithm line 24-26) so any dialog-built parameter map carries it. Verified on the linked GDAL 3.13.2: `gdal_grid -a_nodata -9999 ...` -> 'ERROR 1: Unknown argument: -a_nodata' (gdal_grid --help has no nodata option at all; -a_nodata belongs to gdal_edit). Additionally line 82-87 emits -tr without -txe/-tye whenever PIXEL_SIZE is set but EXTENT is empty -> 'ERROR 5: -txe ad -tye arguments must be provided when resolution is provided'.`
- **PROVG-2**: CONFIRMED empirically: gdalmanage modes are {copy,delete,identify,rename}; 'gdalmanage info <file>' -> 'Error: Zero positional arguments expected'. Wrapper gdalmanage.cpp:11-13,26-30 keeps invalid 'info' as index 0/default
  - *Callchain*: `Toolbox dialog / pipeline -> TaskCenter -> ProviderAlgorithmAdapter::execute -> GdalToolWrapper::processAlgorithm -> GdalManageAlgorithm::buildArgs -> gdalmanage subprocess (argparse error)`
  - *Code context*: `QStringList actions; actions << "info" << "copy" << "rename" << "delete"; ... QString action = actions.value(actionIndex, "info"); args << action; — default enum index 0 selects 'info'. Verified on GDAL 3.13.2: `gdalmanage --help` shows '{copy,delete,identify,rename}'; `gdalmanage info <file>` -> 'Error: Zero positional arguments expected' + usage (exit != 0). The intended mode is 'identify'.`
- **PROVG-3**: CONFIRMED empirically: gdal_proximity --help lists '-values <n>,<n>,<n>'; 'gdal_proximity src dst -targetvalue 6' -> 'Unrecognized option : -targetvalue'. Wrapper gdal_proximity.cpp:43-45 as cited
  - *Callchain*: `Toolbox dialog / pipeline -> TaskCenter -> ProviderAlgorithmAdapter::execute -> GdalToolWrapper::processAlgorithm -> GdalProximityAlgorithm::buildArgs -> gdal_proximity subprocess`
  - *Code context*: `if (parameters.contains("TARGET_VALUE") && !parameters.value("TARGET_VALUE").toString().isEmpty()) { args << "-targetvalue" << parameters.value("TARGET_VALUE").toString(); } — verified on GDAL 3.13.2: gdal_proximity --help lists '-values <n>,<n>,<n>' (no -targetvalue in any GDAL version); `gdal_proximity src dst -targetvalue 6` prints usage and exits non-zero.`
- **OPS-6**: CONFIRMED (gdal_reproject_operator.cpp:162-165 targetResolution branch uses std::to_string (=%f 6 decimals: 2.69495e-5 -> '0.000027'); :167-170 -dstnodata same; reference branch :151-161 correctly uses fmtDouble (%.17g) - the #230 asymmetry is visible in one function)
  - *Callchain*: `MCP/CLI execute_algorithm -> RsOperatorAdapter::execute -> GdalReprojectOperator::run -> runGdalWarp (gdal_operator_utils.cpp:123).`
  - *Code context*: ``} else if (targetResolution > 0.0) { options.emplace_back("-tr"); options.emplace_back(std::to_string(targetResolution)); ...` and `options.emplace_back("-dstnodata"); options.emplace_back(std::to_string(nodata));` — std::to_string is %f with 6 decimals. The reference-alignment branch above (153-161) was fixed to use util::fmtDouble (%.17g) and gdal:clip was fixed the same way, but this branch was left on to_string.`

### Suggested fix
- Align parameter definitions, argument builders, and result harvesting with the QGIS/GDAL/OTB provider execution contract.

### References / Dedupe
- #169 (line 97) fixed -of/-ts/-te/-outsize (8cc594609a touched gdal_grid.cpp 2 lines); #228 (line 38) fixed gdalmanage NEWNAME + others (646d3ea112 +9 lines, kept 'info'); #230 (line 36) fixed clip + reproject reference branch (646d3ea112 touched gdal_reproject_operator.cpp but left the -tr/-dstnodata branch). All three findings are genuine incomplete-fix residuals, not dupes.
- Finding PROVG-1: #169 (commit 8cc594609a) fixed -of/-ts/-te/-outsize in these wrappers but left -a_nodata; #228 batch fixed contour/gdalmanage validation, not gdal_grid. Verified present at HEAD d9c59e5b.
- Finding PROVG-2: #228 (commit 646d3ea112) added the NEWNAME validation in this file but kept the invalid 'info' mode word. Verified at HEAD.
- Finding PROVG-3: #169 fixed -of/-ts/-te/-outsize flags (commit 8cc594609a), #228 fixed zMin/zMax/gdaltransform/stats/contour/gdalmanage — -targetvalue untouched. Verified at HEAD.
- Finding OPS-6: #230 'gdal:clip truncates -te/-dstnodata floats to 6 decimals' — commit 646d3ea112 fixed clip and reproject's reference branch only; the targetResolution/dstnodata branch still uses std::to_string at HEAD. Prior fix incomplete.


---

# #305: [P2][concurrency/lifecycle] #209 RSS retry timer is created on JobEngine worker threads (no event dispatcher) so it never fires - queued-task starvation persists on the worker path

### Area
Concurrency / Job Engine / Lifecycle

### Type
concurrency/lifecycle

### Severity
P2

### Affected code
- `src/processing/framework/task_center.cpp:689-703` (TaskCenter::processNextQueuedTasks)

### User impact
- Silent scheduler deadlock under memory pressure: pipelines stall, waitForTask/waitForPipeline block to their 30/60-min timeouts, MCP/agent tool calls time out. Recovery only via a new main-thread submit.

### Reproduction
- **Trigger (CONC-1)**: 1) One or more tasks run and finish while process RSS is at/above the 75% watermark (plausible right after a big raster task frees memory - VmRSS decays asynchronously). 2) The LAST running task's terminal record is processed by its JobEngine worker (the common path). 3) Queued tasks exist. Thread A (worker) executes the singleShot at task_center.cpp:693; thread B does not exist - nobody ever runs the event loop for A's thread, so the retry never fires and no other terminal transition is pending (totalRunning==0).

### Expected behavior
- **Expected (CONC-1)**: The #209 re-arm retries scheduling after 250ms so queued tasks are launched once RSS drops, regardless of which thread processed the terminal transition.

### Actual behavior
- **Actual (CONC-1)**: On the worker-thread path the retry lambda is never scheduled; queued tasks stay Queued indefinitely (until the user happens to submit/mark another task from the main thread). The exact starvation #209 was filed for still reproduces whenever the terminal transition lands on a worker.

### Root cause
- **CONC-1**: CONFIRMED (task_center.cpp:688-703 QTimer::singleShot(250,...) with NO context object - Qt docs: timer created in calling thread, and QTimer requires a thread with an event loop; JobEngine workers are raw std::thread (job_engine.cpp:442 m_workers.emplace_back) with no dispatcher -> QObject::startTimer fails ('Timers can only be used with threads started with QThread') and the callback never runs; chain verified: workerLoop:474 -> runOperatorJob terminal notify(:546 invokes listener synchronously on worker) -> onJobRecord(task_center.cpp:529) -> markTaskCompleted:893 processNextQueuedTasks under m_mutex on the worker; grep shows the singleShot at :693 is the ONLY auto re-arm in task_center.cpp - no periodic scheduler self-heals; remaining processNextQueuedTasks callers (:462 submit, :1033 cancel, :1269 submitPipeline) are main-thread user actions exactly as the finding's recovery path states; #209 fix 763966246f added exactly this line and works only when the transition lands on the main thread)
  - *Callchain*: `JobEngine workerLoop -> runOperatorJob -> notify(terminal record) -> TaskCenter::onJobRecord -> processJobRecord -> markTaskCompleted -> processNextQueuedTasks (m_mutex held, worker thread) -> memoryPressureHigh()==true && totalRunning==0 -> QTimer::singleShot(250, ...) on std::thread`
  - *Code context*: `processNextQueuedTasks() runs with m_mutex held on whatever thread performed the terminal transition. Terminal transitions for JobEngine-submitted tasks execute on the worker: JobEngine worker -> notify(rec) (job_engine.cpp:546-564, invoked from runOperatorJob finish lambdas on the worker) -> TaskCenter::onJobRecord (task_center.cpp:532) -> processJobRecord -> markTaskCompleted/Failed/Canceled -> processNextQueuedTasks. The #209 re-arm is:
  if ( m_resourceMonitor.memoryPressureHigh() ) {
      if ( totalRunning == 0 && QCoreApplication::instance() ) {
          QTimer::singleShot( 250, [this]() { ... processNextQueuedTasks(); flushPendingLaunches(); flushPendingSignals(); } );
      }
      break;
  }
QTimer::singleShot with no context object starts the timer in the CALLING thread. A JobEngine worker is a raw std::thread with no QEventLoop/event dispatcher; per Qt docs (Threads and QObjects): 'Event driven objects may only be used in a single thread. Specifically, this applies to the timer mechanism' and 'If no event loop is running ... the QTimer will never emit its timeout() signal'. QObject::startTimer fails with 'Timers can only be used with threads started with QThread' and returns -1; the 250ms callback is never invoked.`

### Suggested fix
- Ensure proper synchronization, atomic state transitions, clean thread termination, and avoidance of use-after-free or race conditions on worker/GUI threads.

### References / Dedupe
- #209 (line 57) closed by adding this timer; the fix is incomplete on the worker-transition path - incomplete-fix angle verified by git show 763966246f (+30 lines task_center.cpp, the singleShot). Not a dupe of #208/#110.
- Finding CONC-1: #209 CLOSED via commit 763966246f ('RSS retry timer') which added exactly this QTimer::singleShot. Fix works only for the main-thread path (enqueueTask/submitPipeline callers); the primary terminal-transition path (ADR 0051 listener on JobEngine workers) is unfixed. Not a dupe of #208 (zero-step pipeline) or #110.


---

# #307: [P2][concurrency/lifecycle] Provider cancel-watcher std::thread destroyed while joinable when runPrepared throws non-QgsProcessingException -> std::terminate

### Area
Concurrency / Job Engine / Lifecycle

### Type
concurrency/lifecycle

### Severity
P2

### Affected code
- `src/processing/framework/provider_algorithm_adapter.cpp:274-307` (ProviderAlgorithmAdapter::execute)

### User impact
- Whole-process crash from a single failing provider algorithm; unrecoverable data loss for in-flight work.

### Reproduction
- **Trigger (CONC-3)**: Thread W (JobEngine worker) runs any gdal:/otb:/native:/processing: algorithm whose runPrepared raises a non-QgsProcessingException - e.g. a CRS-transforming algorithm raising QgsCsException on bad input (this app already hit exactly that class of escape in #199), or std::bad_alloc in a full-scene raster calculator pass. No user cancellation is required; the watcher exists unconditionally in production wiring.

### Expected behavior
- **Expected (CONC-3)**: The exception propagates to JobEngine::runOperatorJob's catch(...) (job_engine.cpp:799-819) and the job is marked Failed with the error recorded.

### Actual behavior
- **Actual (CONC-3)**: std::terminate is called inside the adapter: the entire desktop app aborts (SIGABRT), taking down unsaved user work and all other running tasks.

### Root cause
- **CONC-3**: CONFIRMED (provider_algorithm_adapter.cpp:274-307: watcher std::thread armed whenever isCancelledFn set; catch handles ONLY QgsProcessingException (joins + rethrows runtime_error); any other exception escaping runPrepared (QgsCsException - a QgsException not QgsProcessingException; std::bad_alloc; GDAL/QGIS internal throws) unwinds with watcher joinable -> std::terminate per [thread.thread.destr]; production always arms: main.cpp:161-162 fallback executor and processing_job_adapter.cpp:63-65 both pass [&ctx]{ctx.isCancelled()}; JobEngine's catch(... ) at job_engine.cpp:812-819 never reached; #179 (batch-21 8cc594609a) introduced exactly this watcher - diff verified line-for-line)
  - *Callchain*: `JobEngine worker -> runOperatorJob -> fallback executor (main.cpp:148) / 'processing:' prefix executor (processing_job_adapter.cpp:77) -> adapter->execute(params, progressBridge, isCancelledFn) -> watcher thread spawned -> algorithm->runPrepared throws non-QgsProcessingException -> stack unwinds -> ~std::thread(joinable) -> std::terminate`
  - *Code context*: `  std::atomic<bool> runDone{false};
  std::thread watcher;
  if ( isCancelledFn ) {
    watcher = std::thread( [&runDone, isCancelledFn, &feedback]() { ... } );   // captures locals BY REFERENCE
  }
  QVariantMap runResults;
  try {
    runResults = algorithm->runPrepared( parameters, context, &feedback );
  }
  catch ( const QgsProcessingException &e ) {   // ONLY QgsProcessingException is handled
    runDone.store( true ); if ( watcher.joinable() ) watcher.join();
    try { algorithm->postProcess( context, &feedback, false ); } catch (...) {}
    throw std::runtime_error( e.what().toStdString() );
  }
Any other exception escaping runPrepared (QgsCsException - which is a QgsException, NOT a QgsProcessingException; std::bad_alloc on full-scene buffers; exceptions from upstream GDAL/QGIS code) propagates out of execute() while 'watcher' is still joinable. Per [thread.thread.destr], destroying a joinable std::thread calls std::terminate(). Even before that, unwinding destroys runDone/feedback while the watcher loop references them. Production ALWAYS arms the watcher: main.cpp:161-162 and processing_job_adapter.cpp:64-65 pass isCancelledFn = [&ctx](){ return ctx.isCancelled(); } for every provider/fallback execution.`

### Suggested fix
- Ensure proper synchronization, atomic state transitions, clean thread termination, and avoidance of use-after-free or race conditions on worker/GUI threads.

### References / Dedupe
- #179 (line 87) added the watcher; #199 (line 67) same symptom class in a different file. Crash-on-error introduced by the #179 fix - not a dupe.
- Finding CONC-3: #179 CLOSED added the mid-run cancel watcher (this code); the crash-on-non-QgsProcessingException is a defect introduced by that fix, not covered by it. #199 (QgsCsException terminates app) was the same symptom class but fixed in the reprojection operator, not this adapter. Batch-21 #8cc594609a did not touch this file's exception path.


---

# #309: [P2][correctness] SIFT/template-match auto-GCP insertion stores raw SRC pixel coordinates where the session contract requires map coordinates - matched GCPs are garbage

### Area
Core Remote Sensing / Georeferencing / Classification

### Type
correctness

### Severity
P2

### Affected code
- `src/app/georeferencer/qgsgeoreferencermainwindow.cpp:304-312,455-463` (QgsGeoreferencerMainWindow::runSiftMatch acceptance lambda / runTemplateMatch acceptance lambda)

### User impact
- Both auto-match features (SIFT, template NCC) silently produce garbage georeferencing whenever they succeed, on the very data they are designed for (georeferenced source for template match). Worst-case the user accepts 25 'good' matches and warps to a garbage output with no error.

### Reproduction
- **Trigger (GEOREF-3)**: I2I window. Template match ALWAYS (rs_template_matcher.cpp:151-160 requireSrcGeo=true rejects non-georeferenced SRC): grid col/row (e.g. (300,400)) stored as sourcePoint; fit computes toColumnLine((300,400)) = ((300-UL_X)/resX, ...) -> wildly wrong pixels -> garbage transform and warp output, silently (fit 'succeeds'). SIFT with georeferenced SRC: same. SIFT with unreferenced SRC: canvas-picked GCPs store (col,-line) (provider gt {0,1,0,0,-1,0}) while SIFT stores (col,+line) -> mixing manual and SIFT GCPs mirrors the y axis of half the points; SIFT-only sets place markers at positive y, outside the canvas extent y in [-H,0].

### Expected behavior
- **Expected (GEOREF-3)**: Match srcPx converted to the source layer's map coordinates via the source geotransform (toXY) before insertion, or the session to accept pixel coords with an explicit convention conversion; ExistingSeeds mode converting session map-coord GCPs to pixels when feeding the matcher (currently rs_template_matcher treats g.sourcePoint() directly as col/row at line 212-216, so existing-seed mode with a georeferenced source puts huge map values into 'sc = lround(sx)' and every seed falls out of bounds -> '未找到满足阈值的匹配点').

### Actual behavior
- **Actual (GEOREF-3)**: Auto-matched GCPs are stored in a different coordinate space than every other GCP source (canvas picks, .points load, table edits). Fit, residuals, markers, .points export all consume the wrong values; template-match ExistingSeeds mode finds zero matches with a misleading threshold message.

### Root cause
- **GEOREF-3**: CONFIRMED (SIFT acceptance qgsgeoreferencermainwindow.cpp:309 QgsGcpPoint(m.srcPx, m.dstWorld, destCrs, true) with srcPx documented 'Pixel coords in source raster' (rs_sift_matcher.h:40) from rs_sift_matcher.cpp:168 (keypoint/srcScale) - raw pixel col/row; template acceptance :460 identical with rs_template_matcher.cpp:267 m.srcPx=QgsPointXY(sx,sy) grid col/row; session contract requires source-layer MAP coords when georeferenced (qgsgcppoint.h:46 + facade updateParametersFromGcps:184-187 routes through getPixelCoords when hasExistingGeoreference); appendGcps (rs_georeferencing_session.cpp:203-214) does zero conversion then refit()s immediately; template-match primary mode REQUIRES a georeferenced SRC (requireSrcGeo, rs_template_matcher.cpp:151-160) so the mismatch is on the designed-for path; ExistingSeeds mode confirmed broken: qgsgeoreferencermainwindow.cpp:363 feeds g.sourcePoint() (map coords) as col/row, rs_template_matcher.cpp:212-216 lround(sx) -> out-of-bounds -> zero matches with threshold message; SIFT-on-unreferenced mixes (col,-line) picks vs (col,+line) SIFT - provider gt[5]=-1 verified)
  - *Callchain*: `RsSiftTask/RsTemplateMatcher (Task Center worker) -> inliers/matches -> QMessageBox accept -> session.appendGcps(pairs) -> refit -> QgsGeorefTransform::fit -> getPixelCoords misinterprets pixel indices as map coords; markers QgsGeorefDataPoint::updateMarkers setWorldPos(sourcePoint) plot pixels on a map-unit canvas.`
  - *Code context*: `SIFT acceptance (309): 'pairs.append( QgsGcpPoint( m.srcPx, m.dstWorld, destCrs, true ) );' with RsSiftMatcher::Match::srcPx documented as 'Pixel coords in source raster' (rs_sift_matcher.h) produced at rs_sift_matcher.cpp:168 'mm.srcPx = QgsPointXY( srcPts[i].x / srcScale, srcPts[i].y / srcScale );'. Template acceptance (460) identical for rs_template_matcher.cpp:267 'm.srcPx = QgsPointXY( sx, sy );' (grid col/row). No conversion to source-layer map coordinates anywhere. The fit treats sourcePoint as MAP coords when georeferenced: QgsGeorefTransform::updateParametersFromGcps (qgsgeoreftransform.cpp:184-187) 'mRasterChangeCoords.getPixelCoords( sourceCoordinates )' and toColumnLine (qgsrasterchangecoords.cpp:70-78) divides (p-mUL)/res.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #34 (line 200) Task Center migration infra; #72 (line 162) match acceptance UX only. No closed issue on matcher-to-session coordinate semantics. Clean.
- Finding GEOREF-3: #34 only migrated matching to Task Center (infra). No closed issue covers matcher-to-session coordinate semantics. New root cause.


---

# #311: [P2][agent/mcp] MCP stdio server never exits on stdin EOF and performs no TaskCenter/JobEngine shutdown (zombie process)

### Area
Agent Interface / Model Context Protocol (MCP)

### Type
agent/mcp

### Severity
P2

### Affected code
- `src/app/main.cpp:187-198` (main (mcpMode branch) + StdinReader::run (src/agent/mcp_server.cpp:361-372))

### User impact
- Orphaned processes per client session; dirty shutdown crash path (variant of #177/#178 for the MCP surface)

### Reproduction
- **Trigger (AGMCP-2)**: MCP client terminates the connection (normal disconnect, crash, or timeout)

### Expected behavior
- **Expected (AGMCP-2)**: Server process exits on stdin EOF and shuts down TaskCenter/JobEngine cleanly

### Actual behavior
- **Actual (AGMCP-2)**: Process hangs indefinitely as a zombie; if killed, ~StdinReader's wait(3000) can time out mid-getline -> QThread destroyed-while-running qFatal, and running tasks' threads outlive QgsApplication

### Root cause
- **AGMCP-2**: CONFIRMED
  - *Callchain*: `client disconnect -> stdin EOF -> StdinReader::run exits -> (no quit wired) QCoreApplication::exec loops forever holding stdout open`
  - *Code context*: `McpServer server; server.start(app); int result = app->exec(); — no connect(mReader,&QThread::finished,...) anywhere (mcp_server.cpp:398 only connects lineRead), so when the client closes stdin StdinReader::run returns but app->exec() keeps running forever. GUI path (main.cpp:509-511) calls TaskCenter::shutdown()/JobEngine::shutdown(); the MCP branch has neither, so even a forced exit tears down QgsApplication (delete app) while provider-job worker threads still run.`

### Suggested fix
- Comply with MCP protocol specifications, raise proper JSON-RPC errors, and handle lifecycle/path policies securely.

### References / Dedupe
- #177/#178 are GUI-path teardown (QgsApplication torn down while JobEngine workers run; shutdown joins forever) — different surface. #221/#222 lifecycle handshake commit did not add EOF/quit wiring. No closed issue covers MCP stdio-server exit-on-EOF.
- Finding AGMCP-2: #177/#178 are GUI-path teardown; #222/#221 fixes did not add EOF/quit handling — no dup


---

# #313: [P2][security/robustness] Workspace path policies bypassable: CLI validates before ${VAR} expansion; MCP accepts relative '..' paths

### Area
Security / Path Validation / Robustness

### Type
security/robustness

### Severity
P2

### Affected code
- `src/cli/rs_pipeline_runner.cpp:215 vs 339-344,635-644` (RsPipelineRunner::runFromJson/validatePipelineJson/expandEnvironmentPlaceholders + McpServer::absolutePathOutsideWorkspace (src/agent/mcp_server.cpp:105-148))

### User impact
- SICNU_MCP_WORKSPACE/SICNU_PIPELINE_WORKSPACE sandboxes do not actually confine reads/writes

### Reproduction
- **Trigger (AGMCP-5)**: Untrusted pipeline JSON with ${VAR} params, or an MCP agent passing relative parent-escaping paths

### Expected behavior
- **Expected (AGMCP-5)**: Containment enforced on the final resolved paths (post-expansion, CWD-resolved)

### Actual behavior
- **Actual (AGMCP-5)**: Both documented workspace boundaries can be escaped deterministically

### Root cause
- **AGMCP-5**: CONFIRMED
  - *Callchain*: `CLI: pipeline JSON -> validatePipelineJson(raw) OK -> cliJsonToWorkflowDefinition -> expandEnvironmentPlaceholders -> absolute path outside workspace dispatched. MCP: tools/call execute_algorithm {parameters:{input:"../../x"}} -> validateWorkspacePaths -> relative -> allowed`
  - *Code context*: `CLI: runFromJson calls validatePipelineJson (which runs jsonValueOutsideWorkspace on raw params, lines 635-644) BEFORE cliJsonToWorkflowDefinition expands ${VAR} into params (line 215) — a param "${SECRET_OUT}/x.tif" is the literal relative string during validation, then expands to an absolute path outside SICNU_PIPELINE_WORKSPACE. MCP: absolutePathOutsideWorkspace returns false for any non-absolute path (comment line 103: 'Relative paths are allowed without check') — input "../../etc/shadow" or "..\\.." passes validateWorkspacePaths and the operator resolves it against the process CWD, escaping SICNU_MCP_WORKSPACE whenever CWD is at/under the workspace root.`

### Suggested fix
- Enforce path normalization and access control policies after macro/variable expansion to prevent unauthorized filesystem access.

### References / Dedupe
- No closed issue on workspace path containment. #223 (CLOSED) is about the ${VAR} grammar being misread as step references (unresolved DAG) — different defect, same placeholder feature. #221/#222/#240 unrelated.
- Finding AGMCP-5: grep of issues-closed: no prior issue on workspace-path bypass; #221/#222/#240 unrelated


---

# #315: [P2][concurrency/lifecycle] Python IPC lifecycle: sendRequestSync timeout leaves UAF-capturing callback registered; worker-crash reset crosses two plugins onto one IPC stream

### Area
Concurrency / Job Engine / Lifecycle

### Type
concurrency/lifecycle

### Severity
P2

### Affected code
- `src/python/isolated/python_ipc_server.cpp:294-326` (PythonIpcServer::sendRequestSync)
- `src/python/isolated/python_worker_process_pool.cpp:223-268` (PythonWorkerProcessPool::handleWorkerCrash / PythonPluginAdapter::initialize)

### User impact
- Deterministic crash/UB on the main thread in the delayed-main-thread scenario; the same shape of defect was fixed for sendRequestAndAwait (#233) but this remaining path was not.
- Crossed IPC streams after a crash-restart: duplicate plugin menu entries, responses attributed to the wrong plugin, and premature node release allowing a third consumer. This is the residual ownership defect left after #233's bridge-dangling fix (QPointer re-bind, python_plugin_adapter.cpp:118-123).

### Reproduction
- **Trigger (DATAPY-2)**: Worker-thread sendRequestSync whose home thread does not service the event loop within timeoutMs+1000 ms (timeout is 300 s for py: execution).
- **Trigger (DATAPY-3)**: Worker crash while a Python plugin is loaded, followed by loading another Python plugin (pool default size 2 makes node reuse likely).

### Expected behavior
- **Expected (DATAPY-2)**: Timeout returns cleanly and the deferred invocation is invalidated (shared context + erased callback), mirroring the sendRequestAndAwait fix at lines 240-264.
- **Expected (DATAPY-3)**: The crashed node's owner either loses the node (adapter marked dead / plugin reloaded) or the node stays reserved; exactly one bridge per IPC server.

### Actual behavior
- **Actual (DATAPY-2)**: The queued lambda survives with dangling references; late execution is use-after-free on QWaitCondition/QMutex/QJsonObject and the caller's stack (crash or silent memory corruption).
- **Actual (DATAPY-3)**: Two AppInterfaceBridges receive the same PythonIpcServer::messageReceived: daemon requests like ui.add_plugin_menu are dispatched twice (duplicate QActions, racing responses), and either adapter's releaseWorker(m_workerNode) clears isBusy while the other still uses the node — the busy-flag protocol is broken.

### Root cause
- **DATAPY-2**: CONFIRMED
  - *Callchain*: `py: algorithm on a JobEngine worker (src/processing/framework/python_algorithm_adapter execute → AppInterfaceBridge handler, src/python/isolated/app_interface_bridge.cpp:353-354 `ipc->sendRequestSync("processing.execute_algorithm", req, execResult, execIsError, 300000)`) → sendRequestSync worker branch → main thread blocked >301 s (long synchronous GUI/GDAL work; this app has documented classes of multi-minute main-thread stalls, e.g. #184/#218) → worker times out and unwinds → queued lambda eventually executes on the main thread → UAF.`
  - *Code context*: `The worker-thread branch queues a lambda capturing `&result, &isError, ... &status, &done, &mutex, &waitCond` BY REFERENCE (lines 301-313) onto the server's home thread, then blocks: `if ( !waitCond.wait( &mutex, timeoutMs + 1000 ) ) { return AwaitStatus::Timeout; }` (319-322). On timeout the worker returns immediately and destroys those stack objects; nothing cancels or fences the queued lambda. When the main thread later runs it, it executes `status = weakServer->sendRequestAndAwait(...); result/isError writes; waitCond.wakeAll();` on destroyed objects.`
- **DATAPY-3**: CONFIRMED
  - *Callchain*: `Plugin A loaded on node N (acquireWorker → isBusy=true) → daemon segfaults (there is even a `crash_test` RPC, worker_daemon.py:492-495) → handleWorkerCrash(N) → isBusy=false, new server+worker → workerRestarted re-binds A's bridge to the new server → user loads Plugin B → acquireWorker returns the SAME node N → B's bridge also binds to N's server (bindIpcServer connects messageReceived) → both bridges dispatch every daemon→C++ request.`
  - *Code context*: `handleWorkerCrash rebuilds the node and unconditionally does `node->isBusy = false;` (line 261) while the PythonPluginAdapter that owns the node still holds m_workerNode (src/python/isolated/python_plugin_adapter.h:82) and re-binds its bridge on restart (python_plugin_adapter.cpp:118-123). acquireWorker hands the node out again purely on `!node->isBusy && ... node->server->hasClient()` (pool lines 72-78). Nothing re-marks the node busy for the first adapter, and no adapter-side invalidation exists (the adapter listens only to workerRestarted, never to workerCrashed).`

### Suggested fix
- Ensure proper synchronization, atomic state transitions, clean thread termination, and avoidance of use-after-free or race conditions on worker/GUI threads.

### References / Dedupe
- #210 (CLOSED) was the worker-thread socket/UAF and is addressed by the current deferral design; #233 (CLOSED) fixed sendRequestAndAwait callback lifetime and adapter-dangling after unloadAll — distinct from the residual sendRequestSync queue-lifetime and the crash busy-flag reset. Not covered elsewhere (grep isBusy/crash in issues-closed = JobEngine/TaskCenter only).
- Finding DATAPY-2: #210 (fixed) covered sendRequestSync driving the socket from workers; #233 (fixed) covered sendRequestAndAwait's stack-capturing callback timeout. This is the distinct worker-branch queue-lifetime defect — verified still present at HEAD d9c59e5b.
- Finding DATAPY-3: #233 (closed) fixed adapters capturing AppInterfaceBridge* dangling after unloadAll — different defect. Grepped 'isBusy'/'crash' in issues-closed.txt: #177/#178/#207 are JobEngine/TaskCenter. Not covered.


---

# #317: [P2][data/crs-nodata-metadata] CRS preset table maps CGCS2000/Xian1980 labels to wrong EPSG codes (39-deg zone shift; Xian off-by-one)

### Area
Data Model / Raster Grids / NoData & CRS

### Type
data/crs-nodata-metadata

### Severity
P2

### Affected code
- `src/app/crs_presets.cpp:22-30, 46-56` ((anonymous) presets())

### User impact
- Silent gross georeferencing corruption: a layer assigned a 'matching' preset CRS gets misplaced by tens of degrees / wrong datum; project CRS mispairs with UTM data, cascading into the SHELLA-1-style extent errors and reprojection failures. China-region workflows are the app's primary audience and these are 22 of 37 presets

### Reproduction
- **Trigger (SHELLA-3)**: Pick any 'CGCS2000 / 3-degree GK Zone 25..33' or 'Xian 1980 / GK Zone 13..23' preset to set the project or layer CRS

### Expected behavior
- **Expected (SHELLA-3)**: Zone 25 -> EPSG:4513 (CM 75E), Xian Zone 13 -> EPSG:2327; the applied CRS matches the label

### Actual behavior
- **Actual (SHELLA-3)**: Zone 25 -> EPSG:4547 = CM 114E (39 degrees off); Xian Zone 13 -> EPSG:2326 = Hong Kong 1980 Grid System (different datum); the rest of both series are one zone off

### Root cause
- **SHELLA-3**: CONFIRMED
  - *Callchain*: `设置->CRS 预设 / 图层->设置工程 CRS -> CrsPresetDialog -> selectedEpsg -> QgsCoordinateReferenceSystem::fromEpsgId(4547) -> QgsProject::setCrs / layer->setCrs`
  - *Code context*: `crs_presets.cpp:22-30 declares `{ "CGCS2000 / 3-degree GK Zone 25", 4547 }` .. `{ ".. Zone 33", 4555 }`, and :46-56 `{ "Xian 1980 / GK Zone 13", 2326 }` .. `{ ".. Zone 23", 2336 }`. Verified against the local PROJ database: EPSG:4547 = 'CGCS2000 / 3-degree Gauss-Kruger CM 114E' (zone 25's central meridian is 75E — the correct zone-number series is 4513-4521; e.g. projinfo EPSG:4513 = 'CGCS2000 / 3-degree Gauss-Kruger zone 25'); EPSG:2326 = 'Hong Kong 1980 Grid System' (a different datum/ellipsoid), while Xian 1980 GK zone 13 is EPSG:2327 (2336 = zone 22, 2337 = zone 23). So 'Zone 13' applies Hong Kong 1980 and every other Xian entry is shifted one zone; every CGCS2000 preset is shifted ~39 degrees of longitude. Consumers: QgisDesktopWindow::setProjectCrs / setLayerCrsFromPreset (main_window_layers.cpp:259-278, main_window_processing.cpp:547-574) call QgsCoordinateReferenceSystem::fromEpsgId with these codes.`

### Suggested fix
- Validate grid compatibility rigorously, propagate NoData masks across all processing steps, and preserve CRS/metadata on output datasets.

### References / Dedupe
- No closed issue touches crs_presets.cpp (#236/#234 unrelated UI/build batches). Not covered.
- Finding SHELLA-3: No closed issue touches crs_presets.cpp (#236 UI minors and #234 build minors predate/are unrelated). Verified live at HEAD d9c59e5b with projinfo


---

# #319: [P2][concurrency/lifecycle] QgsLayoutDesignerDialog WA_DeleteOnClose + destructor deleteLater on dangling mWindow - heap UAF at exit

### Area
Concurrency / Job Engine / Lifecycle

### Type
concurrency/lifecycle

### Severity
P2

### Affected code
- `src/app/layout/qgslayoutdesignerdialog.cpp:64-73, 100; src/app/main_window_project.cpp:67-70` (QgsLayoutDesignerDialog::~QgsLayoutDesignerDialog / QgisDesktopWindow::newLayout)

### User impact
- Crash on application exit for any session that opened and closed a layout designer; on ASan builds it aborts the process

### Reproduction
- **Trigger (SHELLA-5)**: Open New Layout once, close the layout designer window, then quit the application (or anything else that destroys the main window, e.g. tests)

### Expected behavior
- **Expected (SHELLA-5)**: mWindow tracked via QPointer (or destroyed via deleteLater before the attribute applies); destructor is a no-op after the window was already closed

### Actual behavior
- **Actual (SHELLA-5)**: Deterministic use-after-free on exit: the null-check guard passes on the dangling pointer and deleteLater() is invoked on a destroyed QObject (crash/ASan heap-use-after-free at teardown)

### Root cause
- **SHELLA-5**: CONFIRMED
  - *Callchain*: `工程->新建布局 -> newLayout creates designer (child of main window) + WA_DeleteOnClose mWindow -> user closes layout window -> mWindow destroyed (pointer dangles) -> app quit -> ~QgisDesktopWindow destroys child designer -> ~QgsLayoutDesignerDialog -> mWindow->deleteLater() on freed object`
  - *Code context*: `main_window_project.cpp:67-68 sets `designer->window()->setAttribute( Qt::WA_DeleteOnClose );` where window() returns the parentless `mWindow` (qgslayoutdesignerdialog.h:44 `QWidget *window() override { return mWindow; }`, member declared as raw `QMainWindow *mWindow = nullptr;` at .h:100). When the user closes the layout window, WA_DeleteOnClose destroys mWindow via deleteLater, but the raw member is NOT nulled. The destructor then runs at app teardown (designer is a QObject child of the main window): qgslayoutdesignerdialog.cpp:69-71 `if (mWindow) { mWindow->deleteLater(); ... }` — the comment says 'guard with a null check', but a deleted QWidget leaves the pointer non-null, so the branch executes `deleteLater()` on freed memory (heap-use-after-free in QObject::deleteLater -> d-ptr read). close() at :141-143 has the same dangling pattern.`

### Suggested fix
- Ensure proper synchronization, atomic state transitions, clean thread termination, and avoidance of use-after-free or race conditions on worker/GUI threads.

### References / Dedupe
- #220/#233 cover GuiJobHandle dtor and Python IPC lifetimes; #236's UAF batch is layer/task menus; no closed issue touches QgsLayoutDesignerDialog lifetime. Not covered.
- Finding SHELLA-5: #220/#233 cover other lifecycle bugs (GuiJobHandle dtor, Python IPC); #236's UAF batch was layer/task menus. No closed issue covers the layout designer lifetime; verified WA_DeleteOnClose caller + raw member at HEAD


---

# #321: [P2][performance/cpu] Batch processing still runs every item synchronously on the GUI thread; processEvents re-entrancy lets the file list mutate mid-iteration (UAF)

### Area
Performance / CPU & Main Thread Responsiveness

### Type
performance/cpu

### Severity
P2

### Affected code
- `src/app/dialogs/batch_processing_dialog.cpp:408-454 (sequential loop), 416 (processEvents between items), 760 (adapter->execute), 810 (alg->run)` (BatchProcessingDialog::onRun / runBatchItem)
- `src/app/dialogs/batch_processing_dialog.cpp:408-430` (BatchProcessingDialog::onRun)
- `src/app/dialogs/batch_processing_dialog.cpp:363-371 (dir emptiness check only), 424-426 (output naming), 408-454 (loop)` (BatchProcessingDialog::onRun (#217 fix verification))

### User impact
- Multi-minute UI freezes (the original #184 P1 symptom) persist whenever any single item is slow; unsafe shared-instance run can interleave with toolbox runs of the same algorithm
- Crash or corrupted batch run (wrong input processed under wrong output name) triggered by ordinary UI interaction the #184 fix deliberately re-enabled; regression introduced by the fix commit
- Failed batches with confusing per-item errors; silent loss of earlier batch outputs on name collision

### Reproduction
- **Trigger (DLGB-12)**: Batch any real rasters; each item runs seconds-to-minutes with the UI frozen
- **Trigger (DLGA-3)**: During a batch run, click 添加文件… or 移除选中 in the still-enabled file-list buttons while an item is between the processEvents window and runBatchItem
- **Trigger (DLGB-11)**: Type a new directory path into 输出目录, or batch two files with identical basenames

### Expected behavior
- **Expected (DLGB-12)**: Per-item submission through the Task Center/JobEngine seam (as the single-shot dialogs do), enabling true mid-item cancel
- **Expected (DLGA-3)**: Either user-input events excluded (as before #184) or the file list guarded/disabled during the run; inputFile should be a copy (QString, not const QString&)
- **Expected (DLGB-11)**: mkpath (or upfront existence validation) and unique per-file output naming / collision warning

### Actual behavior
- **Actual (DLGB-12)**: GUI frozen for the duration of every item; cancel takes effect only at the next item boundary; shared registry instance mutated per run
- **Actual (DLGA-3)**: List mutation during the live reference yields use-after-free reads of freed QString data (or at minimum wrong file→output mapping from index shifts); Add can also silently extend a running batch
- **Actual (DLGB-11)**: All items fail with generic algorithm write errors when the dir is missing; basename collisions silently destroy earlier results

### Root cause
- **DLGB-12**: CONFIRMED
  - *Callchain*: `运行批量 -> onRun loop -> runBatchItem -> adapter->execute / alg->run (blocking, GUI thread) -> QApplication::processEvents() only between items`
  - *Code context*: `The loop calls runBatchItem directly on the GUI thread: RS path 'const Json::Value result = adapter->execute(params);' (line 760) and QGIS path 'const QVariantMap results = alg->run(params, context, &feedback);' (line 810). 'QApplication::processEvents();' appears only at line 416 (before each item) so cancel/progress are processed BETWEEN items only; during an item the GUI is fully blocked. Additionally the QGIS path runs the shared REGISTRY algorithm instance ('QgsApplication::processingRegistry()->algorithmById(algorithmId)') rather than the create()/clone() copy that qgsprocessingalgorithm.h:425-430 requires ('not safe to call on algorithms directly retrieved from QgsProcessingRegistry').`
- **DLGA-3**: CONFIRMED
  - *Callchain*: `运行批量 click → onRun loop iteration i → const QString &inputFile = m_inputFiles[i] → processEvents() → nested dispatch of 添加文件…/移除选中 click → m_inputFiles.append/removeAt (buffer realloc or element destruction) → processEvents returns → QFileInfo(inputFile) / runBatchItem(..., inputFile, ...) read the destroyed QString`
  - *Code context*: `Lines 414-424: `const QString &inputFile = m_inputFiles[i]; m_statusLabel->setText(...); QApplication::processEvents(); if ( m_canceled ) {...} ... QString baseName = QFileInfo( inputFile ).completeBaseName(); ... runBatchItem( algorithmId, inputFile, outputPath, &itemError, overrides );`. The reference into the QStringList is held ACROSS the processEvents() call. Commit cac5da9469 (#184) replaced the previous `QApplication::processEvents(QEventLoop::ExcludeUserInputEvents)` (whose comment said 'Keep UI responsive without re-entering user-input handlers') with plain processEvents(), so during each item the user can click 添加文件… (onAddFiles appends → QList reallocation moves/destroys every QString element) or 移除选中 (onRemoveSelected → m_inputFiles.removeAt(row) destroys/overwrites the referenced element). After the nested event dispatch returns, line 424/430 read/pass the dangling reference. Also, index-based iteration over a mutated list skips/duplicates files and mislabels outputs.`
- **DLGB-11**: CONFIRMED
  - *Callchain*: `set a typed (nonexistent) output dir, or add two same-named files from different directories -> 运行批量 -> per-item writes fail / second overwrites first -> summary reports 'N failed' or false success with half the data`
  - *Code context*: `The #217 fix itself is present (m_outputDirEdit->text() is now read: textChanged wiring at 257-259 and the fallback read at 363-365). But: (1) the directory is never created or checked for existence — no QDir().mkpath(m_outputDir) / QFileInfo::exists anywhere; every runBatchItem then fails with a per-item error after processing the whole list. (2) Output name is '<completeBaseName>_processed<ext>' with no collision policy: '/a/img.tif' and '/b/img.tif' both map to outDir/img_processed.tif; the second item silently overwrites the first's result. (3) Skip/fail policy and summary reporting ARE implemented (fail-continue loop 428-451, counts + first-8-error box 468-482).`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #184 (CLOSED) added cancel only — the synchronous-per-item freeze is its original symptom, incompletely addressed; DLGA-3 is a NEW regression introduced by the same #184 commit. #217 (CLOSED) fixed the text-not-read bug; the remaining dir/collision gaps are completeness. Not duplicative as live issues.
- Finding DLGB-12: #184 (P1, closed by cac5da9469) — that fix added the cancel toggle and inter-item processEvents; verified current HEAD still runs items synchronously, so the core freeze is unfixed
- Finding DLGA-3: Not a dupe of #184 (which reported the sync freeze/no-cancel that cac5da9469 addressed); this is a NEW defect introduced by that same commit's processEvents change — verified at HEAD the call is plain QApplication::processEvents() and the reference spans it
- Finding DLGB-11: #217 (fixed in 8f57fbbd79 — text is now read; verified) but completeness gaps remain at HEAD; not covered elsewhere


---

# #323: [P2][performance/cpu] OBIA window runs full-scene feature extraction synchronously on the GUI thread on every spin change; per-polygon OGR_G_Contains loop on import

### Area
Performance / CPU & Main Thread Responsiveness

### Type
performance/cpu

### Severity
P2

### Affected code
- `src/app/obia/rs_obia_main_window.cpp:950-965 (setActiveLevelMap, call to RsSegmentFeatures::extract at 957); 1155-1166 (onActiveLevelChanged); 1442-1630 (importRoiLabels per-pixel loop 1558-1575)` (RsObiaMainWindow::setActiveLevelMap / onActiveLevelChanged / importRoiLabels)

### User impact
- Repeated GUI freezes during normal OBIA level inspection; distinguishes from closed #214/#218/#256 which covered the classification ROI tools and contrast widgets, not the OBIA hierarchy window

### Reproduction
- **Trigger (SHELLB-3)**: After a 2-level hierarchy on a large scene, click the View L spinbox up/down; or import a training polygon covering a large area

### Expected behavior
- **Expected (SHELLB-3)**: Level switch re-indexes asynchronously (or reuses cached per-level stats — the hierarchy task already computed level-0 stats)

### Actual behavior
- **Actual (SHELLB-3)**: UI freezes for the duration of a full-scene multi-band read+scan (seconds to tens of seconds on realistic scenes)

### Root cause
- **SHELLB-3**: CONFIRMED
  - *Callchain*: `View L spinbox valueChanged -> onActiveLevelChanged -> setActiveLevelMap -> RsSegmentFeatures::extract (full-scene GDAL read + full pixel scan on GUI thread); Import ROI button -> importRoiLabels -> per-pixel OGR_G_Contains loop on GUI thread`
  - *Code context*: ``mSegStats = RsSegmentFeatures::extract( mRasterPath, mSegMap, allBandIndices() );` executes directly in the slot. RsSegmentFeatures::extract (src/analysis/segmentation/rs_segment_features.cpp:14-70) opens the raster and reads EVERY requested band into a w*h float buffer (line 48-61: `bandData[b].resize(nPixels); GDALRasterIO(... GF_Read, 0,0,w,h ...)`) then scans every pixel. This runs on the GUI thread each time the user flips the View L spinbox (connected at line 172-173 to onActiveLevelChanged). Same class of defect: importRoiLabels loops `for r... for c...` over each training polygon's envelope calling OGR_G_Contains per pixel center (1559-1572) synchronously — a 10kx10k envelope is 1e8 GEOS point-in-polygon tests.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #214 (ROI spectrum), #218 (contrast-stretch histogram), #256 (recomputeSpectralCurves) are different files/symptoms; no closed issue covers rs_obia_main_window level-switch re-extract or ROI-import voting. Not covered.
- Finding SHELLB-3: #214 (ROI mean spectrum in canvasReleaseEvent), #218 (contrast-stretch histogram), #256 (recomputeSpectralCurves) are different files/symptoms; none cover rs_obia_main_window level switching or ROI import voting.


---

# #325: [P2][correctness] #243 incomplete: ROI-group train/test split API has no production caller - holdout OA/Kappa still inflated by pixel-level split

### Area
Core Remote Sensing / Georeferencing / Classification

### Type
correctness

### Severity
P2

### Affected code
- `src/app/classification/qgsclassificationmainwindow.cpp:2312-2313 (Apply), 2540-2541 (Preview); src/analysis/classification/rs_classification_pipeline.cpp:332 (operator path)` (QgsClassificationMainWindow::applyClassification / applyPreview; RsClassificationPipeline::run)

### User impact
- Inflated accuracy metrics reported to users/agents (operator result overallAccuracy/kappa/confusionMatrix); fix cc58241e37 landed the API but wired no caller.

### Reproduction
- **Trigger (RSCLS-2)**: Any supervised classification run with contiguous ROI polygons (the normal case): neighboring, spatially autocorrelated pixels of one ROI land in both train and test.

### Expected behavior
- **Expected (RSCLS-2)**: ROI-level split via the groupIds overload (each ROI polygon kept wholly in train or test) so holdout accuracy estimates generalization, as specified by #243.

### Actual behavior
- **Actual (RSCLS-2)**: Pixel-level shuffling inflates OA/Kappa; identical defect to pre-fix #243 in all production consumers.

### Root cause
- **RSCLS-2**: CONFIRMED (P2; #243 incomplete: RsClassificationSplit::stratifiedSplit added groupIds parameter in commit cc58241e37, but no production caller passes it — qgsclassificationmainwindow.cpp:2312, 2540 and rs_classification_pipeline.cpp:332 call stratifiedSplit(X, y, ratio) without group IDs, leaving all GUI and pipeline splits pixel-level with spatial autocorrelation data leakage)
  - *Callchain*: `GUI Apply -> stratifiedSplit(pixel) -> fitScalerOntoConfig -> pipeline accuracy block; operator testSplit>0 -> pipeline line 332 stratifiedSplit(ex.X, ex.y, 1.0-testSplit, seed) pixel-level`
  - *Code context*: `const auto split = RsClassificationSplit::stratifiedSplit( X, y, m_classifierBar->trainRatio() ); — no groupIds argument. grep: groupIds is passed only by tests/test_stratified_split.cpp:161; rs_classification_split.cpp:10-13 still documents 'Adjacent pixels from the same ROI polygon can land in both train and test, so reported OA/Kappa is optimistic'.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #243 closed (API introduced); cite as [regression-check: #243 fix incomplete].
- Finding RSCLS-2: #243 closed by cc58241e37; git show confirms only rs_classification_split.{h,cpp}+tests gained groupIds; grep confirms no production caller passes it.


---

# #327: [P2][algorithm/numerics] Terrain aspect returns mirrored angle and hillshade mixes azimuth conventions - lighting/aspect wrong except default sun azimuth

### Area
Algorithms / Image Processing / Numerics

### Type
algorithm/numerics

### Severity
P2

### Affected code
- `src/processing/algorithms/terrain_analysis.cpp:186-190, 274-278` (TerrainAnalysis::aspect / TerrainAnalysis::hillshade)

### User impact
- Aspect rasters are systematically mirrored (N<->W, E<->S quadrants swapped) - wrong input for landform classification, solar exposure, hydrology. Hillshade renders with wrong light direction for any non-default azimuth; quantitative shade masks wrong.

### Reproduction
- **Trigger (RSALGO-1)**: rs:terrain_analysis with product=aspect (any DEM) or product=hillshade with sunAzimuth outside {135,315} (e.g. 0/45/90/180/225/270 for time-of-day analysis)

### Expected behavior
- **Expected (RSALGO-1)**: aspect = compass azimuth of downhill direction (0=N, 90=E, e.g. east-facing slope -> 90); hillshade term cos(az_compass - aspect_compass)

### Actual behavior
- **Actual (RSALGO-1)**: aspect = 270deg - true azimuth (east-facing -> 180, south-facing -> 90); hillshade lighting rotated/mirrored for azimuths other than 135/315 (default 315 accidentally correct, hiding the bug)

### Root cause
- **RSALGO-1**: CONFIRMED (P2; TerrainAnalysis::aspect (terrain_analysis.cpp:188) computes std::atan2(-dzdy, dzdx) * 180 / M_PI, returning polar angle from East rather than compass azimuth clockwise from North (Horn 1981 / GDAL convention), systematically mirroring and swapping quadrants; test_terrain.cpp:94-127 encodes the inverted formula into test expectations; hillshade (lines 230, 275-278) mixes azRad in compass azimuth with aspectRad in polar angle, rendering correct lighting only at default 315/135 deg)
  - *Callchain*: `RsTerrainAnalysisOperator::run (src/operators/rs/rs_terrain_analysis_operator.cpp:146-149) -> TerrainAnalysis::aspect / TerrainAnalysis::hillshade -> processNativeFusion-style output write`
  - *Code context*: `aspect: `float angle = std::atan2( -dzdy, dzdx ) * toDeg; if ( angle < 0 ) angle += 360.0f;` with dzdx = east-west gradient, dzdy = south-north gradient. atan2(-dzdy, dzdx) is the CCW-from-East math angle of the UPHILL gradient, not the documented compass azimuth of the downhill direction (header: 'aspect in degrees [0, 360), clockwise from north'; GDAL/ESRI convention: azimuth = 90 - atan2(dzdy,-dzdx) mapped to [0,360)). Verify: east-facing slope (dzdx<0, dzdy=0) -> code outputs 180 (South) where GDAL/ESRI output 90 (East); output = 270deg - trueAzimuth for all cases. hillshade: `aspectRad = std::atan2( -dzdy, dzdx )` then `hs = cosZen*cos(slopeRad) + sinZen*sin(slopeRad)*cos(azRad - aspectRad)` mixes azRad (compass, CW-from-north) with aspectRad (CCW-from-East math angle) in one cos difference. Correct term is cos(az - aspect_compass) = cos(az - 270 + m); the code computes cos(az - m) - equal only for az=135/315. Example: sun azimuth 0 (north) on a south-facing slope: correct term = cos(0-180) = -1 (fully shadowed); code computes cos(0-90) = 0 (mid-gray).`

### Suggested fix
- Correct the mathematical formulas, coordinate system transforms, and NoData boundary conditions to align with standard GIS conventions (Horn 1981, GDAL, IEEE-754).

### References / Dedupe
- #203 closed (addressed cellSize geotransform); trigonometric azimuth coordinate conventions were not addressed.
- Finding RSALGO-1: #203 (terrain cellSize from geotransform) fixed only the 30.0 default; aspect/hillshade convention never covered in issues-closed.txt (233 lines read). No commit in `git log -20 -- src/processing/algorithms/` touches the aspect math.


---

# #329: [P2][data/crs-nodata-metadata] Atmospheric correction NoData handling incomplete across paths: QUAC percentiles over sentinels; provider adapter feeds NoData as data

### Area
Data Model / Raster Grids / NoData & CRS

### Type
data/crs-nodata-metadata

### Severity
P2

### Affected code
- `src/processing/algorithms/atmospheric_correction.cpp:308-381, 261-306` (AtmosphericCorrection::processFileMultiBand / quac / percentile)
- `src/processing/providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.cpp:93-99, 140-147` (AtmosphericCorrectionAlgorithm::processAlgorithm)

### User impact
- QUAC output radiometry wrong on every scene with NoData; magnitude scales with the fraction of fill pixels (a 30% border-filled scene shifts dark[b] entirely to the sentinel).
- numerically wrong atmospheric correction and NoData-contract violation (#192/#202 class); fix incomplete

### Reproduction
- **Trigger (RSALGO-5)**: QUAC on any raster declaring a numeric NoData sentinel (0, -9999, fill values) - the common case for clipped scenes
- **Trigger (PROVO-5)**: any input raster with NoData pixels (scene edges, nodata=0) using DOS1/DOS2/DN-to-radiance

### Expected behavior
- **Expected (RSALGO-5)**: NoData pixels excluded from the 1st/99th percentile statistics and mapped to NoData in the output (as the DOS path does post-#190)
- **Expected (PROVO-5)**: NoData masked to NaN and output NoData declared (as the #192 fix did for band math / spectral index / raster_ndvi)

### Actual behavior
- **Actual (RSALGO-5)**: Sentinel values dominate the dark percentile, skewing per-band gains; sentinels are output as clipped reflectance values
- **Actual (PROVO-5)**: NoData DNs are 'corrected' as valid pixels, DOS haze level biased toward the sentinel value, downstream treats nodata as reflectance

### Root cause
- **RSALGO-5**: CONFIRMED (P2; AtmosphericCorrection::processFileMultiBand (atmospheric_correction.cpp:339) reads raw DNs into memory without masking band NoData sentinels before calling quac(), corrupting 1st and 99th percentile statistics on scenes with fill values)
  - *Callchain*: `rs:atmospheric_correction(method=quac) (rs_atmospheric_correction_operator.cpp:237-245) -> AtmosphericCorrection::processFileMultiBand -> quac -> percentile`
  - *Code context*: `processFileMultiBand reads bands with `srcDataset.readBandData(b + 1, dnBands[b].data(), ...)` and never calls bandNoDataValue(); quac then takes `percentile(..., 1.0f)` / `percentile(..., 99.0f)` where the percentile helper partitions out only NaN (`return !std::isnan(v)`). A scene with nodata=0 or -9999 puts the sentinel at the 1st percentile: dark[b] = sentinel, meanDark contaminated, refRange = meanBright - meanDark inflated, gain = 0.5*refRange/(range*meanBright) wrong for every band. Also the sentinel pixels themselves are 'corrected' into clipped [0,1] values instead of passed through as NoData. The sibling single-band path processFile() masks `!isfinite(v) || abs(v - bandNoData) < 1e-4f -> NaN` before statistics (lines 424-430, added in 0169bd7eac for #190/#193) - the multi-band QUAC path was not given the same treatment.`
- **PROVO-5**: CONFIRMED (P2; AtmosphericCorrectionAlgorithm::processAlgorithm (atmospheric_correction_algorithm.cpp:89-147) reads raw block values without checking isNoData(), computes dark level over sentinels, and writes output without setting NoData on outProvider)
  - *Callchain*: `qgis_algorithms:rs_atmospheric_correction -> processAlgorithm -> provider->block(1,...) including NoData -> AtmosphericCorrection::dos1/dos2 findMin corrupted -> Float32 GeoTIFF with no NoData metadata`
  - *Code context*: `Read loop: 'for (size_t i=0;i<totalPixels;++i) { int row=i/nCols; int col=i%nCols; dnData[i]=static_cast<float>(inBlock->value(row,col)); }' - no inBlock->isNoData(i) check, unlike the sibling adapters fixed for #192 (band_math_algorithm.cpp:100-101 and spectral_index_algorithm.cpp:61-62 map NoData to NaN). Output block writes result values unconditionally and never calls outProvider->setNoDataValue / setIsNoData. Downstream, AtmosphericCorrection::dos1 computes findMin() over all pixels (src/processing/algorithms/atmospheric_correction.cpp:25-35), so sentinel NoData (typically 0) becomes the dark-object minimum and corrupts the haze estimate for every pixel.`

### Suggested fix
- Validate grid compatibility rigorously, propagate NoData masks across all processing steps, and preserve CRS/metadata on output datasets.

### References / Dedupe
- #129 closed (added QUAC method), #190/#192 closed (NoData fixes on OpenCV/indices); cite as [regression-check: #190/#192 fix incomplete].
- Finding RSALGO-5: #190 (commit 0169bd7eac) patched atmospheric_correction.cpp only in the single-band processFile/streaming path; QUAC multi-band path verified still unmasked at HEAD.
- Finding PROVO-5: Closed #192 (band math/spectral index/raster calculator NoData) and #202 (output NoData contract) - this provider adapter was not covered by the fix. Sibling OPS/RSALGO 'atmospheric' findings concern the rs: operators' metadata fallback, a different code path/file.


---

# #331: [P2][processing/provider] readBandWindowScaled out-of-raster padding contract false on current GDAL - RasterIO overwrites prefill with 0/band-NoData

### Area
Processing Framework / GDAL & OTB Providers

### Type
processing/provider

### Severity
P2

### Affected code
- `src/processing/gdal/gdal_dataset_wrapper.cpp:294-308` (GdalDatasetWrapper::readBandWindowScaled)

### User impact
- Silent data corruption along the right/bottom edges of pansharpened/heterogeneous fusion outputs; fake zeros propagate into downstream statistics and classification

### Reproduction
- **Trigger (PROVG-4)**: Heterogeneous-resolution fusion (pan res != MS res). The MS window msW = ceil((xOff+tw)*scaleX) - msXOff rounds past the MS edge on the last tile column/row whenever width*scale is non-integer; extents of pan/MS are also unchecked for heterogeneous pairs because compareGrids early-returns at PixelSizeMismatch and image_fusion skips that verdict (see PROVG-5)

### Expected behavior
- **Expected (PROVG-4)**: Out-of-raster buffer region contains the caller-supplied nodata so the fusion kernel masks it (kernel checks msData[i] == nodata || isnan)

### Actual behavior
- **Actual (PROVG-4)**: Region contains 0 for MS bands without declared NoData — a valid DN — so edge pixels are fused as real zero-reflectance data (or the MS band's own sentinel, which != the fusion nodata)

### Root cause
- **PROVG-4**: CONFIRMED (P2; GdalDatasetWrapper::readBandWindowScaled (gdal_dataset_wrapper.cpp:294-308) pre-fills buffer with NoData and assumes GDALRasterIO will leave out-of-raster padding untouched; GDALRasterIO returns CE_Failure whenever xOff+srcWidth > rasterWidth or yOff+srcHeight > rasterHeight, failing edge tile reads in pan/MS fusion)
  - *Callchain*: `AlgorithmEngine -> ImageFusion::process tile loop (src/processing/algorithms/image_fusion.cpp:803-814 readMsWindow -> msDataset.readBandWindowScaled(band, msXOff, msYOff, msW, msH, buf, tw, th, nodata)) -> GdalDatasetWrapper::readBandWindowScaled -> GDALRasterIO`
  - *Code context*: `Comment claims 'GDAL resamples the source window into the buffer and handles a window that extends past the raster edge by only filling the valid intersection, leaving the rest of the buffer at its pre-filled value', implemented as std::fill(buffer, buffer + bufWidth*bufHeight, nodata) followed by GDALRasterIO(band, GF_Read, xOff, yOff, srcWidth, srcHeight, ...) with the UNCLAMPED window (negative offsets allowed by the line-301 guard). Empirically verified on the linked GDAL 3.13.2 via `gdal_translate -srcwin -1 -1 6 6`: out-of-source pixels come back as 0 when the band declares no NoData, and as the band's declared NoData when it does — the caller's pre-fill is clobbered in both cases.`

### Suggested fix
- Align parameter definitions, argument builders, and result harvesting with the QGIS/GDAL/OTB provider execution contract.

### References / Dedupe
- issues-closed.txt scanned; no closed issue on readBandWindowScaled boundary handling.
- Finding PROVG-4: #174 is the apply_mask window bug (operators, different symbol); #202 covers output NoData declaration. No prior issue or sibling-agent finding covers readBandWindowScaled fill semantics. Verified at HEAD.


---

# #333: [P2][error-handling] raster_merge_bands: cancel during read leaves blocks short - write loop OOB crash/UB

### Area
Error Handling / Exception Safety

### Type
error-handling

### Severity
P2

### Affected code
- `src/processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.cpp:66-82 vs 117-134` (RasterMergeBandsAlgorithm::processAlgorithm)

### User impact
- deterministic crash on a routine cancel path; #170 rewrote this algorithm but did not make the cancel path safe

### Reproduction
- **Trigger (PROVO-2)**: Start Merge Raster Bands on a multi-band/multi-layer input and press Cancel during the 'Merging raster bands...' read phase

### Expected behavior
- **Expected (PROVO-2)**: cancellation returns a partial/empty result safely (return {} like raster_calculator.cpp:88-89 does)

### Actual behavior
- **Actual (PROVO-2)**: out-of-bounds std::vector operator[] then member call on an uninitialized unique_ptr - segmentation fault / memory corruption

### Root cause
- **PROVO-2**: CONFIRMED (P2; RasterMergeBandsAlgorithm::processAlgorithm (raster_merge_bands.cpp:71-82, 117-124) inner loop breaks on feedback->isCanceled() during band reading, leaving blocks.size() < totalBands; execution falls through to GDAL output creation and accesses blocks[b] up to totalBands, causing out-of-bounds memory access / UB / crash on cancel)
  - *Callchain*: `QgisAlgorithmsProvider::loadAlgorithms -> run raster_merge_bands -> processAlgorithm -> user clicks Cancel while bands are being read -> blocks.size()==currentBand < totalBands -> blocks[b] reads past end of std::vector<std::unique_ptr<QgsRasterBlock>> -> dereference of garbage pointer in hasNoDataValue()/bits()`
  - *Code context*: `Read loop: 'for (int band...) { if (feedback->isCanceled()) break; ... blocks.push_back(std::move(block)); ... }' (lines 71-72,78) - break exits only the inner band loop and there is NO early return after the loops. Write loop: 'for (int b = 0; b < totalBands; ++b) { ... if (blocks[b]->hasNoDataValue()) ... const void *data = blocks[b]->bits();' (lines 117-127) iterates to totalBands regardless. totalBands was computed from all layers (lines 55-57) before any cancellation.`

### Suggested fix
- Implement robust error reporting, return explicit error codes/exceptions, and prevent silent failures or unhandled exception aborts.

### References / Dedupe
- #170 closed (raster_merge_bands rewrite); cite as [regression-check: #170 cancel safety].
- Finding PROVO-2: #170 fixed merge writing all bands; #227 'partial outputs left behind' and #179/#184 cancellation issues are about other files. Grep findings-*.jsonl for raster_merge_bands: no hits.


---

# #335: [P2][correctness] rs:endmember_extraction returns full per-pixel ppiCounts array in tool result (50-100 MB JSON on 25 MP)

### Area
Core Remote Sensing / Georeferencing / Classification

### Type
correctness

### Severity
P2

### Affected code
- `src/operators/rs/rs_endmember_extraction_operator.cpp:357-360` (RsEndmemberExtractionOperator::run)

### User impact
- MCP responses blow past transport/LLM context limits (agent tool calls become unusable or truncated mid-JSON); logger memory inflates by ~24 bytes/pixel per run; schema/outputs contract drift.

### Reproduction
- **Trigger (OPS-5)**: rs:endmember_extraction on any real scene: a 25 MP Landsat tile returns a 25,000,000-element JSON array (~40-100 MB serialized); even a 1024x1024 teaching scene returns ~1M numbers.

### Expected behavior
- **Expected (OPS-5)**: Per-pixel counts belong in a raster/sidecar, not the result map; result should carry endmembers, indices, and at most top-K counts.

### Actual behavior
- **Actual (OPS-5)**: The full pixel-sized array is embedded in the operator result, the MCP tool response, the pipeline result payload, and the operation logger.

### Root cause
- **OPS-5**: confirmed
  - *Callchain*: `MCP execute_algorithm -> JobEngine 'processing:' executor -> RsOperatorAdapter::execute -> run(); the whole result becomes info.resultPayload and is returned verbatim by the MCP get_execution_result handler (src/agent/mcp_server.cpp:330-333); the GUI execute() path additionally stores a copy in the in-memory RSOperationLogger (rs_operation_logger.cpp:43-47).`
  - *Code context*: ``Json::Value countsJson(Json::arrayValue); for (int c : result.ppiCounts) countsJson.append(c); json["ppiCounts"] = countsJson;` — result.ppiCounts is std::vector<int> sized pixelCount (allocated at line 287). The schema outputs (lines 32-38) declare only endmembers/indices; ppiCounts is an undeclared, unbounded result key.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- No prior issue on result-map size; #239 and #238 are unrelated catalog perf minors; verified against issues-closed.txt.
- Finding OPS-5: #239 (catalog perf minors) and #238 unrelated; no prior issue about result-map size. Verified at HEAD.


---

# #337: [P2][build/portability] Build/deps: WITH_EPT option tested before declaration; install_deps.sh missing jsoncpp/qt6-declarative; vendored modes (SICNU_VENDOR_GDAL/SICNU_BUILD_OTB) cannot configure

### Area
Build System / Packaging / Installation

### Type
build/portability

### Severity
P2

### Affected code
- `CMakeLists.txt:81-84, 179-180` (WITH_EPT / ZSTD_LIBRARY)
- `scripts/install_deps.sh:39-125` (install_arch/install_ubuntu/install_fedora/install_macos)
- `CMakeLists.txt:59-74, 494-506` (SICNU_VENDOR_GDAL / SICNU_BUILD_OTB / SICNU_HAS_OTB)

### User impact
- Point-cloud EPT/COPC decode support (on by default) is un-declared as a dependency: breaks the documented Windows build tier, and on Linux any GDAL/Qt built without zstd turns into a link failure far from the root cause.
- The documented bootstrap path for every supported platform produces a broken environment; users must reverse-engineer the real dependency set from .github/workflows/ci.yml.
- README-documented SICNU_VENDOR_GDAL/SICNU_BUILD_OTB flows are unusable; a clean-clone OTB attempt leaves the user with an unrelated-looking CMake error about test_otb_smoke.

### Reproduction
- **Trigger (BUILD-3)**: Any toolchain where zstd is not ambient: MSVC + vcpkg manifest mode (the Tier-3 Windows CI config) does not add vcpkg installed includes globally → `fatal error C1083: cannot open include file: 'zstd.h'` (and unresolved ZSTD_* at link even if the header were found)
- **Trigger (BUILD-4)**: Fresh machine following the README one-step dependency install on any supported distro
- **Trigger (BUILD-5)**: Any use of the two documented vendored-dependency switches on a fresh clone / this checkout

### Expected behavior
- **Expected (BUILD-3)**: option(WITH_EPT ...) declared before its first use (or the find_package moved below), so ZSTD_INCLUDE_DIR/ZSTD_LIBRARY are populated and qgis_core links an explicit zstd
- **Expected (BUILD-4)**: Lists include jsoncpp (+ qt6-declarative family) so the documented flow configures and compiles
- **Expected (BUILD-5)**: Vendor mode bootstraps (two-phase or guarded includes); OTB mode either builds or cleanly disables, and SICNU_HAS_OTB reflects reality

### Actual behavior
- **Actual (BUILD-3)**: zstd dependency silently absent from the build graph; compilation succeeds only on systems where zstd.h/libzstd are ambient
- **Actual (BUILD-4)**: Fresh-machine flow fails at configure (Qt components / version) and would fail at compile (jsoncpp) even then
- **Actual (BUILD-5)**: Both optional modes abort at configure; the OTB skip path additionally poisons the test configuration

### Root cause
- **BUILD-3**: confirmed
  - *Callchain*: `configure (line 81: WITH_EPT undefined → no find_package(ZSTD)) → qgis_core compiles qgseptdecoder.cpp with no zstd -I and links no zstd → resolves only via ambient system/Qt-GDAL transitive deps`
  - *Code context*: `CMakeLists.txt:81-84 `if(WITH_EPT) find_package(ZSTD REQUIRED) endif()` executes at line 81, but `option(WITH_EPT "" ON)` is declared later at line 180 — WITH_EPT is undefined at the point of the test, so the branch is dead and find_package(ZSTD) never runs (ZSTD_* absent from the audit build's CMakeCache). Downstream, src/core/CMakeLists.txt:2326 `include_directories(SYSTEM ${ZSTD_INCLUDE_DIR})` and :2811-2815 `if (WITH_EPT) target_link_libraries(qgis_core ${ZSTD_LIBRARY})` therefore inject empty values, while src/core/pointcloud/qgseptdecoder.cpp:20 `#include <zstd.h>` + :196-210 real ZSTD_* API calls are compiled whenever WITH_EPT OR WITH_COPC (both default ON at root lines 179-180). On Linux this only works accidentally: zstd.h is in the compiler default include path and libzstd.so.1 arrives transitively (ldd of built libqgis_core.so resolves via another linked lib; nm shows 4 undefined ZSTD_* refs). cmake/FindZSTD.cmake and the vcpkg.json `zstd` (windows) entry exist but are never used for this.`
- **BUILD-4**: confirmed
  - *Callchain*: `README.md:40 `sudo ./scripts/install_deps.sh` → fresh machine → cmake configure fails on missing Qt Qml/QuickWidgets component (or succeeds with distro Qt<6.8 error), and with Qt fixed, compile fails on json/json.h not found`
  - *Code context*: `jsoncpp is absent from all four distro lists (lines 41-59, 65-83, 88-105, 110-124) yet the root CMake requires it for compile and link: algorithm_descriptor.h and friends include <json/json.h> via the JSONCPP_INCLUDE_DIRS set up at CMakeLists.txt:528-549, and every consumer falls back to `target_link_libraries(... jsoncpp)` (bare -ljsoncpp). ci.yml installs libjsoncpp-dev / jsoncpp explicitly on all three tiers, confirming it is required. Arch/Fedora/Ubuntu lists also lack the provider of QtQml/QtQuickWidgets (qt6-declarative / qt6-declarative-dev / qt6-qtdeclarative-devel) while root CMakeLists.txt:54-56 does `find_package(Qt6 6.8 REQUIRED COMPONENTS ... Qml QuickWidgets ...)`. The Ubuntu path additionally cannot work at all: distro Qt is 6.4 (< required 6.8, per the root's own comment). The macOS list omits qca (WITH_AUTH defaults ON and qgis_core compiles auth sources that include QtCrypto headers — macOS CI tier installs qca via brew for exactly this reason) and qtkeychain. GSL is present in all four lists (#234's gap is fixed at HEAD).`
- **BUILD-5**: confirmed
  - *Callchain*: `README 'With OTB' flow (`./scripts/build_with_otb.sh`) or `cmake -DSICNU_VENDOR_GDAL=ON` → configure fails before any compilation`
  - *Code context*: `(a) With -DSICNU_VENDOR_GDAL=ON in a fresh WS build dir, configure emits include-search failures for ${SICNU_VENDOR_PREFIX}/lib/cmake/gdal/GDALConfig.cmake, proj-config.cmake and geos-config.cmake (CMakeConfigureLog shows the search of the empty vendor_prefix) and ends 'Configuring incomplete, errors occurred!' with NO build.ninja generated — the prefix is only populated by the ExternalProjects at build time, which can never start (chicken-and-egg; verified empirically). (b) With SICNU_BUILD_OTB=ON: root line 499 `if(EXISTS .../otb_ref) add_subdirectory(otb_ref)` — in this checkout otb_ref/ is a partial tree (only CI/Data/Docker/Documentation/Examples/Packaging/SuperBuild/Utilities; no top-level CMakeLists.txt, no Modules/), so add_subdirectory fails; itk_ref/ does not exist at all (README claims 156MB vendored ITK). Worse, line 505 `set(SICNU_HAS_OTB TRUE)` runs even when both warnings fired, so with ENABLE_TESTS=ON, tests/CMakeLists.txt:3233 `target_link_libraries(test_otb_smoke PRIVATE OTBCommon ...)` references a target that was never created → configure error.`

### Suggested fix
- Update CMakeLists.txt install rules, dependency discovery, and RPATH configuration to ensure correct runtime linking and packaging.

### References / Dedupe
- #234's batch covered only install_deps GSL on this axis; ZSTD ordering, jsoncpp/QtQml gaps and vendor modes unaddressed (verified in issues-closed.txt).
- Finding BUILD-3: #234 (misc build/portability batch) did not include ZSTD/WITH_EPT ordering.
- Finding BUILD-4: #234 covered 'install_deps.sh missing GSL' only — that is fixed; the jsoncpp/QtQml gaps are new.
- Finding BUILD-5: #234 misc build batch did not cover vendor/OTB configure paths.


---

# #339: [P2][concurrency/lifecycle] Closing a running non-modal SicnuAlgorithmDialog (window X, WA_DeleteOnClose) destroys mContext while the worker thread is inside runPrepared - worker-thread UAF (#220 sibling path)

### Area
Concurrency / Job Engine / Lifecycle

### Type
concurrency/lifecycle

### Severity
P2

### Affected code
- `src/app/dialogs/sicnu_algorithm_dialog.cpp:739-759 (contextPtr capture + runPrepared); 609-615 (isFinalized)` (SicnuAlgorithmDialog::runAlgorithm / SicnuProcessingRunState executor lambda / SicnuAlgorithmDialog::isFinalized)

### User impact
- Use-after-free on the GUI-adjacent worker thread: crash or silent memory corruption for any toolbox algorithm closed mid-run; window is the entire algorithm duration (seconds to minutes)

### Reproduction
- **Trigger (DLGB-1)**: Open any Processing Toolbox algorithm dialog (non-modal), press Run, then close the window with the X button while the algorithm is executing

### Expected behavior
- **Expected (DLGB-1)**: Dialog either refuses to close while the Task Center job is in flight, or the run state (context/feedback) is owned by the job so the worker never touches dialog-owned memory after destruction

### Actual behavior
- **Actual (DLGB-1)**: Dialog is destroyed mid-run; the worker thread continues runPrepared against freed QgsProcessingContext memory until the cancel hook (feedback->cancel()) happens to take effect; mContext is used pervasively by every QGIS algorithm

### Root cause
- **DLGB-1**: confirmed
  - *Callchain*: `menu qgis_algorithms:* -> QgisDesktopWindow::openProcessingAlgorithm (main_window_processing.cpp:115-134, sets WA_DeleteOnClose, show()) -> user clicks Run -> SicnuAlgorithmDialog::runAlgorithm -> GuiJobHandle::submitJob -> JobEngine worker thread -> state->algorithm->runPrepared(*contextPtr /* &mContext */, feedbackPtr) -> user clicks window X -> closeEvent accepted -> WA_DeleteOnClose -> ~SicnuAlgorithmDialog frees mContext/mRunState-order members -> worker still dereferences *contextPtr`
  - *Code context*: `runAlgorithm() captures raw pointers into dialog-owned members for the worker-thread executor: 'QgsProcessingContext *contextPtr = &mContext;' (line 739) and 'QgsProcessingFeedback *feedbackPtr = feedback;' (line 740); the worker executes 'state->results = state->algorithm->runPrepared( state->parameters, *contextPtr, feedbackPtr );' (lines 758-759). The only close-while-running guard is isFinalized() (returns false while mJobHandle.isRunning()), which the vendored base consults only to decide its own deleteLater(): QgsProcessingAlgorithmDialogBase::closeEvent (src/gui/processing/qgsprocessingalgorithmdialogbase.cpp:725-736) accepts the close event unconditionally and never ignores it while a Task Center job runs. The sole construction site sets WA_DeleteOnClose before show(): main_window_processing.cpp:129-133 'auto *dlg = new SicnuAlgorithmDialog(this); ... dlg->setAttribute(Qt::WA_DeleteOnClose); dlg->show();'. QWidget::close() honors WA_DeleteOnClose unconditionally (deleteLater), bypassing isFinalized().`

### Suggested fix
- Ensure proper synchronization, atomic state transitions, clean thread termination, and avoidance of use-after-free or race conditions on worker/GUI threads.

### References / Dedupe
- Not a dup of #220 (which covered ~GuiJobHandle destructor re-entrancy + destroyed-mRunState callback); the X-close/WA_DeleteOnClose mid-run mContext path is genuinely the sibling branch #220's fix left open.
- Finding DLGB-1: #220 (closed, fixed in 5116f95a66) covered the ~GuiJobHandle destructor re-entrancy and the destroyed-mRunState callback read; verified current HEAD still ships the raw contextPtr/feedbackPtr into the worker with no close guard — the X-close/WA_DeleteOnClose UAF path was not addressed by that fix


---

# #341: [P2][correctness] ImageEnhancementPanel band-ratio silently writes the full input band count with bands 2..N all zeros

### Area
Core Remote Sensing / Georeferencing / Classification

### Type
correctness

### Severity
P2

### Affected code
- `src/app/dialogs/image_enhancement_panel.cpp:367-368,405-412,445` (ImageEnhancementPanel::onRun (method==2, ratioType==0 path))

### User impact
- Silent wrong output content (data integrity); consumers indexing bands get zeros; wasted file size; contradicts the standalone tool's contract

### Reproduction
- **Trigger (DLGA-4)**: Run a band ratio on any multi-band raster with more than 2 bands via the unified 影像增强 panel

### Expected behavior
- **Expected (DLGA-4)**: Single-band ratio GeoTIFF (the standalone BandRatioDialog correctly builds outputBands with outBandCount=1, band_ratio_dialog.cpp:176,185)

### Actual behavior
- **Actual (DLGA-4)**: Output has the input band count; bands 2..N are silently all-zero floats; downstream band-aware operators see a bogus multi-band product

### Root cause
- **DLGA-4**: confirmed
  - *Callchain*: `影像增强 panel → type 波段比值, band1/band2 → 运行 → onRun → runGdalTask worker → ratio fills outputBands[0] only → writeGdalOutput writes all N vectors → user loads N-band TIFF where only band 1 holds the ratio`
  - *Code context*: `Line 367-368 allocate `std::vector<std::vector<float>> outputBands(bands); for (...) outputBands[b].resize(w * h);` (bands = input band count, zero-filled). Ratio branch lines 409-412: `int b1 = std::min(band1, bands); int b2 = std::min(band2, bands); ImageEnhancement::bandRatio( inputBands[b1-1].data(), inputBands[b2-1].data(), outputBands[0].data(), pixelCount); bands = 1;` — only outputBands[0] is written and the local `bands` assignment changes nothing about the vector. Line 445 then calls `writeGdalOutput( outPath, w, h, outputBands, geo.geoTransform, geo.projection, &error )`, and writeGdalOutput (gdal_dataset_wrapper.cpp:505-551) creates the TIFF with `int bandCount = static_cast<int>(bands.size());` — i.e. ALL vectors, writing the untouched zero buffers as bands 2..N.`

### Suggested fix
- Address the root cause directly with minimal, surgical changes adhering to project coding standards.

### References / Dedupe
- #182 fixed band-combo population only; no prior issue on this write path.
- Finding DLGA-4: #182 fixed band-combo population in this panel (cac5da9469) but not the write path; no closed issue mentions the zero-band output; verified writeGdalOutput semantics at HEAD


---

# #343: [P3][ui/interaction] ImageEnhancementPanel minors batch: IHS on <3-band raster writes all-zero success; onCompleted/onFailed dead wiring + null m_runButton; missing raster guard; no memory cap (bad_alloc escapes worker)

### Area
GUI / Application Shell / Dialogs

### Type
ui/interaction

### Severity
P3

### Affected code
- `src/app/dialogs/image_enhancement_panel.cpp:405-427` (ImageEnhancementPanel::onRun (method==2, ratioType==1 path))
- `src/app/dialogs/image_enhancement_panel.cpp:455-467` (ImageEnhancementPanel::onCompleted / ImageEnhancementPanel::onFailed)
- `src/app/dialogs/image_enhancement_panel.h:35-36 (slot redeclarations), 81 (shadowing m_runButton); src/app/dialogs/image_enhancement_panel.cpp:455-467; src/app/dialogs/raster_processing_dialog_base.cpp:275-277, 333-336` (ImageEnhancementPanel::onCompleted / onFailed / ImageEnhancementPanel::m_runButton vs RasterProcessingDialogBase::onCompleted/onFailed/m_runButton)
- `src/app/dialogs/image_enhancement_panel.cpp:36-42,293-303` (QgisDesktopWindow::openImageEnhancementPanel / ImageEnhancementPanel::onRun)
- `src/app/dialogs/contrast_stretch_dialog.cpp:133-191` (ContrastStretchDialog::onRun worker lambda)

### User impact
- Silent garbage output presented as success — data integrity and user trust; the standalone BandRatioDialog has the same latent shape but its combos make <3 bands unreachable for IHS
- No crash today, but misleading status feedback and a hidden trap: shadowed slot + shadowed null m_runButton will crash if the dispatch is 'fixed' naively
- Dead completion UI in the enhancement panel plus a maintenance trap (shadowed member + would-be overrides)
- Dead-end dialog state; same root cause family as DLGA-1/DLGA-2 (base-class m_rasterLayer contract vs dialog-owned selection)
- Process-level instability on large scenes; inconsistent memory policy across the three enhancement entry points that share ImageEnhancement kernels

### Reproduction
- **Trigger (DLGA-5)**: Select IHS Transform in the 影像增强 panel on a raster with fewer than 3 bands
- **Trigger (DLGA-6)**: Run any enhancement from the panel; observe the status label after a failure (dialog stays open with 'Processing…' shown)
- **Trigger (DLGB-14)**: Run any enhancement from the panel and observe the status label; or any future code that connects the panel's onCompleted/onFailed slots by pointer-to-member
- **Trigger (DLGA-8)**: Activate a vector layer (or nothing) in the layer tree, then open 影像增强 and click 运行
- **Trigger (DLGA-11)**: Run contrast stretch or band ratio/IHS on a raster whose bandCount×W×H×8 bytes exceeds available memory

### Expected behavior
- **Expected (DLGA-5)**: Reject with 'IHS requires at least 3 bands' (like the guard message style used elsewhere) and write nothing
- **Expected (DLGA-6)**: Panel status label shows Completed!/Failed! after a run (the overrides' evident intent)
- **Expected (DLGB-14)**: Derived completion hooks run after the task (status text updates) per the apparent override pattern
- **Expected (DLGA-8)**: Launcher refuses to open with an info box (the pattern used by Band Math / Atmospheric Correction launchers), or the panel offers a layer picker
- **Expected (DLGA-11)**: Same soft-cap estimate + graceful structured failure as ImageEnhancementPanel (and bad_alloc caught)

### Actual behavior
- **Actual (DLGA-5)**: An all-zero GeoTIFF of the input band count is written and the task completes successfully
- **Actual (DLGA-6)**: Overrides are dead code; status label stuck on 'Processing…' after failures (success closes the dialog via accept()); latent null-deref if anyone ever fixes the dispatch
- **Actual (DLGB-14)**: Handlers never invoked; status label stays stale; the shadowing nullptr m_runButton would crash if they were
- **Actual (DLGA-8)**: Panel opens fully interactive but Run always fails with a layer error; nothing the user can do inside the dialog fixes it
- **Actual (DLGA-11)**: Allocation failure either kills the worker with an unhandled exception or drives the process into the OOM killer; user gets at best a generic failure with no size hint

### Root cause
- **DLGA-5**: confirmed
  - *Callchain*: `影像增强 panel on a 1- or 2-band raster → type IHS Transform, pick bands → 运行 → onRun validation passes (line 327 checks distinct bands only for ratioType==0) → worker: no branch executes → zero buffers written → onCompleted → success log + dialog accept`
  - *Code context*: `Lines 407-427: `if (ratioType == 0 && bands >= 2) { ... } else if (ratioType == 1 && bands >= 3) { ... }` — there is no else. With ratioType==1 and bands<3 (e.g. a 2-band raster, where the pre-validation at line 327 only rejects band1==band2 for ratioType==0), neither branch runs, outputBands stays zero-filled, and writeGdalOutput writes the zeros; the worker returns outPath so the run is reported as completed.`
- **DLGA-6**: confirmed
  - *Callchain*: `runGdalTask → GuiJobHandle success callback → base lambda → RasterProcessingDialogBase::onCompleted (static dispatch) → handleCompleted → finishRun/accept — ImageEnhancementPanel::onCompleted/onFailed never entered`
  - *Code context*: `Header (image_enhancement_panel.h:33-36) declares `private slots: void onCompleted(const QString &outputPath); void onFailed(const QString &errorMessage);` and (line 81) a separate `QPushButton *m_runButton = nullptr;` that shadows the base's protected m_runButton and is never assigned (setupButtonBar assigns the base member). raster_processing_dialog_base.cpp:275-288 invokes completion via a lambda defined inside RasterProcessingDialogBase: `[this]( const QString &outPath, const Json::Value & ) { onCompleted( outPath ); }` — onCompleted is NOT virtual, so this statically calls RasterProcessingDialogBase::onCompleted; the panel overrides never run. Had they run, `m_runButton->setEnabled(true)` would null-deref (panel member is nullptr). Net effect: m_statusLabel stays at 'Processing…' forever (line 308 sets it; the 'Completed!'/'Failed!' updates at 458/465 are unreachable).`
- **DLGB-14**: confirmed (dup of DLGA-6)
  - *Callchain*: `panel Run -> base runGdalTask -> base onCompleted (static dispatch) -> base handleCompleted -> accept(); panel onCompleted/onFailed unreachable`
  - *Code context*: `Base declares 'public slots: void onCompleted(const QString&); void onFailed(const QString&);' WITHOUT virtual (raster_processing_dialog_base.h:181-190), and runGdalTask's callbacks call onCompleted/onFailed by unqualified name (statically resolved to the base implementations). The panel's redeclarations under 'private slots:' therefore never run: grep of image_enhancement_panel.cpp shows no connect() targeting them. The panel additionally declares its own 'QPushButton *m_runButton' (header line 81) shadowing the base's protected member that setupButtonBar actually assigns — the derived member stays nullptr, so 'm_runButton->setEnabled(true);' inside the dead handlers (cpp:457, 464) is a latent null dereference, and m_statusLabel ('Completed!'/'Failed!') never updates.`
- **DLGA-8**: confirmed
  - *Callchain*: `影像增强 menu with a vector/no active layer → openImageEnhancementPanel still exec()s the panel → user configures method/params → 运行 → base validateInputs fails (!m_rasterLayer) → onRun never invoked`
  - *Code context*: `Launcher main_window_processing.cpp:141-149: `QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(activeLayer()); if (rasterLayer) { dialog.setRasterLayer(rasterLayer); } dialog.exec();` — unlike openBandMathDialog/openAtmosphericCorrectionDialog (lines 155-167, 205-218) it does NOT return early with 'Please select a raster layer first' when rasterLayer is null. The panel has no layer combo of its own (only method/band combos) and no validateInputs override, so with m_rasterLayer null the base RasterProcessingDialogBase::validateInputs (raster_processing_dialog_base.cpp:76-80) rejects every Run with 未选择有效的栅格图层。, and onRun's own `if (!rl)` branch (298-301) is unreachable because the base Run lambda gates on validateInputs.`
- **DLGA-11**: confirmed
  - *Callchain*: `运行 on a large raster (e.g. 7-band 20k×20k ≈ 2.8 GB per buffer pair) → runGdalTask worker → vector allocation throws std::bad_alloc → not caught by `catch ( const std::runtime_error & )` → propagates out of the task lambda into the JobEngine executor`
  - *Code context*: `contrast_stretch_dialog.cpp:143-150: `std::vector<std::vector<float>> allBands( bandCount, std::vector<float>( pixelCount ) ); ... std::vector<std::vector<float>> outputBands( bandCount, std::vector<float>( pixelCount ) );` — 8 bytes/pixel/band with no estimate check. band_ratio_dialog.cpp:178-185 does the same for up to 6 buffers. The sibling ImageEnhancementPanel added an explicit guard (image_enhancement_panel.cpp:345-352: `constexpr qint64 kMaxBytes = 2LL * 1024 * 1024 * 1024; ... if ( estBytes > kMaxBytes || estBytes < 0 ) { ... return QString(); }`) that these two dialogs lack. BandRatioDialog's worker catches only `const std::runtime_error &` (band_ratio_dialog.cpp:230-233) and ContrastStretchDialog's worker has no try/catch at all, so std::bad_alloc (derives from std::exception, not runtime_error) escapes the lambda into the JobEngine callable executor.`

### Suggested fix
- Ensure widgets are fully initialized or stubbed safely, signals/slots are correctly connected to backend results, and UI elements do not block the main event loop.

### References / Dedupe
- No prior issues for the panel minors; #182 (band combo) is unrelated to these paths.
- Finding DLGA-5: No closed issue covers this path; #182 was combo population; verified no else branch at HEAD
- Finding DLGA-6: No closed issue mentions these overrides; verified non-virtual base call path at HEAD (raster_processing_dialog_base.cpp:275-277)
- Finding DLGB-14: #182 (band combos never populated — fixed) and #218 (sync stats — fixed) are different defects in the same panel; verified at HEAD
- Finding DLGA-8: #182 fixed band-combo population for this panel (populateBandCombos on setRasterLayer) — different defect; verified launcher and panel at HEAD
- Finding DLGA-11: #204/#201 are kmeans/OOM issues in operators; #227 covered partial outputs; no closed issue covers the dialog-side unguarded full-scene buffers; verified HEAD lacks the guard in both dialogs


---

# #345: [P3][ui/interaction] Georeferencing minors batch: #251 errorMessage never surfaced; RPC bias never reset across refits; ignored collectEnabledGcps ok flag; #247 absolute collinearity threshold; I2I GCP CRS stamping; DestMap columns untransformed

### Area
GUI / Application Shell / Dialogs

### Type
ui/interaction

### Severity
P3

### Affected code
- `src/app/georeferencer/qgsgeoref_shell_window.cpp:1270-1378` (QgsGeorefShellWindow::onSessionFitChanged)
- `src/app/georeferencer/rs_georeferencing_session.cpp:318-324` (RsGeoreferencingSession::transformFromSnapshot)
- `src/analysis/georeferencing/qgsleastsquares.cpp:69-75` (QgsLeastSquares::linear)
- `src/app/georeferencer/qgsgeoref_shell_window.cpp:1706-1710` (QgsGeorefShellWindow::commitGcpPair)
- `src/app/georeferencer/qgsgcplistmodel.cpp:67-77,113-121,210-213` (QgsGCPListModel::setTargetCrs / rowDestinationPoint / data(DestMapX/DestMapY))

### User impact
- The user-facing goal of #251 ('user sees no diagnostic') is only half-met: engine-side message added, presentation-side never wired.
- Low frequency but high consequence: a garbage warp output instead of an error.
- Garbage Linear fits with plausible-looking RMS on nearly-degenerate GCP layouts from float sources.
- Silent garbage fit/output after a plausible UI action; affects both fit and warp (snapshot uses the same points).
- Misleading table values for mixed-CRS sessions; users comparing rows draw wrong conclusions; minor since fit itself is homogenized.

### Reproduction
- **Trigger (GEOREF-5)**: Collinear GCPs with Linear/Projective (SingularException caught, 'Parameter estimation failed'), unprojectable mixed-CRS GCP, or too few GCPs after method switch.
- **Trigger (GEOREF-7)**: Mixed-CRS GCP list where one point's CRS->destCrs transform throws (out-of-domain) and the snapshot is built from a stale-but-ready fit (createWarpSnapshot only re-checks counts, not that the fit matches the current GCP set, lines 290-295).
- **Trigger (GEOREF-8)**: Float-precision source coordinates from SIFT/template matches or decimal .points files that are almost collinear (e.g. along a coastline/road); canvas clicks at integer pixels hit exactly 0 and are caught.
- **Trigger (GEOREF-9)**: Changing the target CRS in the params panel after loading the reference and then adding GCPs (a legitimate workflow: output in a different projection).
- **Trigger (GEOREF-10)**: Any GCP list whose points carry differing destinationPointCrs (possible via .points v2 crs column, ADR 0056, or the map-coords dialog's selector).

### Expected behavior
- **Expected (GEOREF-5)**: The diagnostic string produced for #251 to be surfaced (status bar message, panel label, or tooltip on the disabled Apply action).
- **Expected (GEOREF-7)**: transformFromSnapshot passes &ok and refuses to build the transform (mirroring fit's gate).
- **Expected (GEOREF-8)**: A relative conditioning check (e.g. sumNormY vs sumNormX+sumNormY) so near-collinear sets are rejected or flagged, per #247's 'near-collinear ... no error signal'.
- **Expected (GEOREF-9)**: Label = the CRS the coordinates were actually captured in (dst canvas/layer CRS), or all stored GCPs re-projected when the panel CRS changes.
- **Expected (GEOREF-10)**: DestMapX/Y rendered in the target CRS (that is why setTargetCrs exists and why it refreshes exactly those columns).

### Actual behavior
- **Actual (GEOREF-5)**: The message exists only in the result struct; UI behavior is identical to pre-#251 (silent 'RMS: —' + disabled Apply). The internal GCP-list model likewise maps invalid residuals to (0,0).
- **Actual (GEOREF-7)**: The warp fits through a point in the wrong CRS, producing a silently wrong output exactly where #245's fix aimed to prevent it.
- **Actual (GEOREF-8)**: Only exact degeneracy is detected; near-collinear fits produce wild scale factors silently. Note helmert (sumNormSq < 1e-12) and the projective SVD check (relative 1e-6*s0) do not share this gap to the same degree.
- **Actual (GEOREF-9)**: Coordinates and CRS label diverge; fit and warp output are silently wrong (e.g. degree values treated as meters or vice versa), the very failure mode #245's homogenization was meant to eliminate.
- **Actual (GEOREF-10)**: Each row shows its own stored CRS units; a degrees row next to a meters row under the same 'X参(map)' header; residual/scatter comparisons in the shell are equally fed per-point units for marker display (QgsGeorefDataPoint::destinationDisplayPoint does transform - only the table does not).

### Root cause
- **GEOREF-5**: confirmed
  - *Callchain*: `refit -> fitChanged(fit) -> onSessionFitChanged -> (errorMessage dropped) -> Apply stays disabled with no reason.`
  - *Code context*: `fit() fills errorMessage at qgsgeoreftransform.cpp:355 ('Need at least %1 GCPs'), 379 ('Failed to reproject...'), 428 ('Parameter estimation failed' - the #251 singular diagnostic). grep for 'errorMessage' consumers: onSessionFitChanged never reads fit.errorMessage; it only updates counts, residuals, RMS label ('RMS: —') and calls updateApplyEnabled(). No QMessageBox/statusBar/panel text anywhere references lastFit().errorMessage.`
- **GEOREF-7**: confirmed
  - *Callchain*: `createWarpSnapshot freezes GCPs -> transformFromSnapshot -> collectEnabledGcps silently keeps bad point -> updateParametersFromGcps fits through it -> RsWarpTask warps with a corrupted GCP.`
  - *Code context*: `Line 320: 'QgsGeorefTransform::collectEnabledGcps( snap.gcps, src, dst, snap.destCrs );' - no bool* ok argument. collectEnabledGcps (qgsgeoreftransform.cpp:299-331) appends the UNTRANSFORMED point on QgsCsException (transformedDestinationPoint returns mDestinationPoint with ok=false, #253). The fit path checks the flag (fit() lines 371-381 aborts with 'Failed to reproject one or more GCPs'); the warp path does not.`
- **GEOREF-8**: confirmed
  - *Callchain*: `fit() -> QgsLinearGeorefTransform::updateParametersFromGcps -> QgsLeastSquares::linear -> parameters used for residuals and warp.`
  - *Code context*: `'if ( sumNormX < 1e-12 || sumNormY < 1e-12 ) throw QgsLeastSquares::SingularException();' - sumNormY = sum of squared y-deviations in pixel units. 1e-12 px^2 means RMS y-deviation < 1e-6 px, i.e. only exact/near-exact collinearity is caught. Three GCPs on a horizontal line with 0.01 px y-jitter give sumNormY ~ 3e-8 > 1e-12 -> bY = sumCrossY/3e-8, a hyper-sensitive scaleY accepted without any error or warning (finite check at line 82 passes).`
- **GEOREF-9**: confirmed
  - *Callchain*: `User loads REF (panel CRS := REF CRS) -> user changes 目标CRS to e.g. EPSG:4326 for the output -> subsequent dest picks remain in REF-crs units but are labeled 4326 -> fit's #245 homogenization sees label==target for all points and skips conversion -> dst values interpreted in the wrong units.`
  - *Code context*: `onDestPointPicked (1738-1751) converts the pick via mapPickToLayerCrs(mDstCanvas, mDstRaster, ...) into the REF raster's CRS, but commitGcpPair labels with 'const QgsCoordinateReferenceSystem destCrs = mParamsPanel ? mParamsPanel->destCrs() : ...; mGeorefSession.addGcp( QgsGcpPoint( src, dst, destCrs, true ) )'. loadReferenceRaster syncs panel<-REF CRS (qgsgeoreferencermainwindow.cpp:559-561) at load time only; nothing re-syncs or re-labels when the user changes the target CRS afterwards (the picker is freely editable, rs_georef_params_panel.cpp:330-357).`
- **GEOREF-10**: confirmed
  - *Callchain*: `onSessionFitChanged -> gcpModel()->setTargetCrs(dstCrs, ctx) -> data() renders raw values.`
  - *Code context*: `setTargetCrs (67-77) stores mTargetCrs and emits dataChanged for the DestMap columns, but the DisplayRole handler returns 'formatFixed( destMap.x(), 2 )' where 'destMap = rowDestinationPoint( index.row() )' is the RAW stored point (113-121); transformedDestinationPoint/mTargetCrs are never referenced anywhere else in the file (dead members). The shell wires it with the dst canvas/panel CRS (qgsgeoref_shell_window.cpp:1298).`

### Suggested fix
- Ensure widgets are fully initialized or stubbed safely, signals/slots are correctly connected to backend results, and UI elements do not block the main event loop.

### References / Dedupe
- #251 fixed only the singular fit diagnostic part; #247/#245/#253 are distinct sibling gaps; #286 is the RPC-bias units fix whose state model GEOREF-6 critiques.
- Finding GEOREF-5: Incomplete-fix report for closed #251.
- Finding GEOREF-7: Incomplete sibling-path report for closed #245/#253 (fit path fixed, warp path not).
- Finding GEOREF-8: Incomplete-fix report for closed #247.
- Finding GEOREF-9: #245 fixed mixed-CRS homogenization during fit; this is the mislabeled-single-CRS case (producer-side), a distinct root cause the fix cannot catch.
- Finding GEOREF-10: Not covered by #245 (that was the fit); display-path gap only.


---

# #347: [P3][agent/mcp] LLM/MCP minors batch: double finished(); errors replied to notifications; tools/call rejects prefix names the allowlist permits; unescaped LLM text in QLabel rich text; interaction-tool schema drift; dead completion watcher; plaintext API keys; test-connection stuck state

### Area
Agent Interface / Model Context Protocol (MCP)

### Type
agent/mcp

### Severity
P3

### Affected code
- `src/agent/llm_streaming_client.cpp:140-148,272-290` (parseSseLine ([DONE] branch) + onReplyFinished)
- `src/agent/mcp_server.cpp:430-449,616-619` (McpServer::handleRequest (initialize + fallthrough))
- `src/agent/mcp_server.cpp:587-599,884-907` (McpServer::handleRequest(tools/call routing) + isToolIdAllowed)
- `src/agent/agent_copilot_dock_widget.cpp:350-357,443-457,514-521` (onContentTokenReceived/appendToolCallCard/onErrorOccurred)
- `src/agent/tool_catalog/interaction_tool_provider.cpp:69-270 (also src/agent/tool_catalog/agent_tool_catalog.cpp:345 vs src/agent/mcp_server.cpp:266)` (makeSetBandCompositeTool/makeSetStretchTool/makeDrawRoiTool vs InteractionToolRegistry schemas (interaction_tool_registry.cpp:134-262) and RasterDisplayService::setStretch)
- `src/agent/agent_copilot_dock_widget.cpp:390-397,404-433` (onToolCallParsed submit call / watchToolCallCompletion / onTaskCenterTaskUpdated / m_pendingToolCallCompletions)
- `src/agent/llm_config_manager.cpp:174-199 (read side 90-99)` (LlmConfigManager::updateProfiles)
- `src/agent/llm_settings_dialog.cpp:111-139` (LlmSettingsDialog::onTestConnectionClicked)

### User impact
- Contract wart today (dock handler is idempotent), latent UI-state corruption for any future consumer; 'pure transport' (ADR 0049) signal is not well-defined
- Strict clients (SDK validators) may log errors, abort, or mis-negotiate; #222's handshake fix left these protocol-shape gaps
- Catalog/call contract inconsistency; clients must know to re-shape calls into execute_algorithm; allowlist entries are dead code on this route
- UI corruption plus a local-file-read side channel into rendered content; inconsistent escaping across sibling code paths
- Capability loss and misrouting of agent calls; schema drift is the exact class #186 targeted
- Agent UX blind spot; ADR 0047 removed toolExecutionFinished as 'dead' but left no replacement observer
- API key leakage via standard file access; focus item 'key storage'
- Minor UX dead-end; inconsistent with the client's finished signal contract

### Reproduction
- **Trigger (AGMCP-6)**: Any successful streaming chat completion
- **Trigger (AGMCP-7)**: Standard MCP client cancels a request or updates roots list
- **Trigger (AGMCP-8)**: MCP client that discovers the unified catalog via list_tools/get_tool_schema and calls tools by their advertised name
- **Trigger (AGMCP-9)**: Model emits (or is prompt-injected to emit) HTML in its answer, e.g. <img src="file:///home/user/.ssh/..."> or layout-breaking markup
- **Trigger (AGMCP-10)**: Agent asked for e.g. '3-sigma stretch' or '5-95 percent clip' — expressible in the registry schema, impossible via the exported catalog schema
- **Trigger (AGMCP-11)**: Any algorithm task submitted from the copilot that fails or is cancelled mid-run
- **Trigger (AGMCP-13)**: User saves any provider configuration
- **Trigger (AGMCP-14)**: Provider/model combination that answers 'ping' with reasoning_content only or empty content

### Expected behavior
- **Expected (AGMCP-6)**: finished() exactly once per request (terminal signal)
- **Expected (AGMCP-7)**: No response object for notifications; initialize responds with a server-supported protocolVersion when the requested one is unknown
- **Expected (AGMCP-8)**: dispatchToolCall handles any allowlisted id (the code path already exists and validates)
- **Expected (AGMCP-9)**: All LLM/network-derived strings HTML-escaped before entering rich text (as the reasoning path already does)
- **Expected (AGMCP-10)**: One schema owner per tool across catalog/registry (ADR 0021 single-owner principle)
- **Expected (AGMCP-11)**: Terminal status (success/error) appended to the chat
- **Expected (AGMCP-13)**: Secrets in a credential store (or at minimum DPAPI/keyring-backed)
- **Expected (AGMCP-14)**: Terminal state reported via finished() (success or explicit 'no content received')

### Actual behavior
- **Actual (AGMCP-6)**: Two emissions; if the user sends a new prompt between them (m_isStreaming already false), the stale finished #2 resets m_isStreaming=false and the button text while the NEW stream is live, allowing a click to cancel the in-flight stream
- **Actual (AGMCP-7)**: Spurious error responses with id:null on the wire; version negotiation always 'succeeds'
- **Actual (AGMCP-8)**: Method-not-found error for the exact ids the server advertises
- **Actual (AGMCP-9)**: Raw model output interpreted as rich text; local-file <img> refs are resolvable by Qt's text engine
- **Actual (AGMCP-10)**: Two divergent schema definitions per interaction tool; LLM-visible surface is a strict subset
- **Actual (AGMCP-11)**: User sees only the '⚡ 准备执行工具' card; failures vanish
- **Actual (AGMCP-13)**: World-readable-per-umask plaintext secrets file, included in backups/config sync
- **Actual (AGMCP-14)**: Permanent 'testing' state until dialog closed

### Root cause
- **AGMCP-6**: confirmed
  - *Callchain*: `onReadyRead -> parseSseLine('[DONE]') -> finished() #1 ... QNetworkReply::finished -> onReplyFinished -> finished() #2`
  - *Code context*: `[DONE] handler: emitParsedToolCallOnce(); emit finished(); — then when QNetworkReply finishes without error, onReplyFinished again runs emitParsedToolCallOnce() (no-op) and emit finished(). OpenAI-compatible streams always end with data:[DONE], so every successful request double-emits finished(). The header doc (llm_streaming_client.h:79-83) only claims tool-call exactly-once, not finished-once.`
- **AGMCP-7**: confirmed
  - *Callchain*: `client sends {method:"notifications/cancelled",params:{requestId:N}} (no id) -> handleRequest -> sendError(null,-32601)`
  - *Code context*: `Only notifications/initialized is silently consumed; any other notification (notifications/cancelled, notifications/roots/list_changed) falls to the else branch: sendError(id, -32601, ...) producing {"jsonrpc":"2.0","id":null,"error":{...}} — JSON-RPC 2.0 §'notifications MUST NOT be replied to'. initialize echoes params.protocolVersion verbatim (result[protocolVersion] = protocolVersion, defaulting to 2024-11-05 only when absent), claiming support for any future/unknown version instead of answering with a version the server actually implements.`
- **AGMCP-8**: confirmed
  - *Callchain*: `tools/call {name:'rs:spectral_index'} -> no matching branch -> -32601`
  - *Code context*: `tools/call routes only the 18 meta-tool names, 3 data: aliases, and prefixes view:/roi:/canvas:/layer:/raster: to dispatchToolCall; a name like "rs:spectral_index" or "rs_spectral_index" hits sendError(-32601, "Method not found: rs:spectral_index") even though isToolIdAllowed lists rs:/gdal:/gdal_tools:/otb:/qgis:/qgis_algorithms:/opencv: as allowed and handleListTools/handleGetToolSchema advertise exactly those ids.`
- **AGMCP-9**: confirmed
  - *Callchain*: `SSE content delta -> contentTokenReceived -> setText(raw) -> Qt rich-text engine parses model-emitted HTML`
  - *Code context*: `onContentTokenReceived: m_currentContentLabel->setText( m_accumulatedContent ) — no toHtmlEscaped(), unlike onReasoningTokenReceived (line 346) and appendUserMessageCard (line 309) which escape. appendToolCallCard: QLabel(QString("⚡ 准备执行工具: <b>%1</b>").arg(algName)) with algName taken from the LLM tool_calls delta. onErrorOccurred: .arg(errorMsg) unescaped inside a <font> tag. QLabel AutoText renders these as rich text.`
- **AGMCP-10**: confirmed
  - *Callchain*: `dock sendPrompt exports provider schemas -> LLM constrained to drifted enums -> RasterDisplayService::setStretch maps the fixed std_dev_2/percentile_2_98 aliases`
  - *Code context*: `Catalog raster:set_stretch advertises only {layer_id, stretch_type enum [min_max, percentile_2_98, std_dev_2, none], min_val, max_val} while the executable surface accepts method/lower/upper/factor/min/max (registry schema) — the agent cannot vary stddev factor or clip percentiles at all. canvas:draw_roi catalog omits geometry(WKT)/crs (bbox-only). raster:set_band_composite catalog marks layer_id+RGB ints required, but the service also accepts band roles ('nir') and gray, with layer optional. Additionally search_tools meta description says 'Exact or substring group filter' while agent_tool_catalog.cpp:345 is exact case-insensitive only.`
- **AGMCP-11**: confirmed
  - *Callchain*: `tool call submitted -> TaskCenter runs -> taskUpdated(terminal) -> onTaskCenterTaskUpdated finds nothing -> silence`
  - *Code context*: `submit is called with a no-op callback: m_toolCallDispatcher.submit(cppEnvelope, [](const Json::Value&){}, &error) — watchToolCallCompletion has no callers, so m_pendingToolCallCompletions stays empty and onTaskCenterTaskUpdated always returns at the find() miss. Consequently a submitted task that later FAILS produces no chat feedback at all (only synchronous rejections reach handleToolCallRejection).`
- **AGMCP-13**: confirmed
  - *Callchain*: `LlmSettingsDialog OK -> setActiveProfile/updateProfiles -> QSettings ini on disk`
  - *Code context*: `settings.setValue(QStringLiteral("apiKey"), p.apiKey) writes provider API keys (DeepSeek/DashScope/OpenAI) as plain strings into the user's QSettings ini (~/.config/SICNU/...), with no OS keyring use or obfuscation; keys round-trip into every loadProfiles() consumer.`
- **AGMCP-14**: confirmed
  - *Callchain*: `test button -> sendChatCompletion('ping') -> reply OK but no content deltas -> no signal updates the label`
  - *Code context*: `Only contentTokenReceived and errorOccurred are connected; there is no finished() handler. A 200 reply whose stream yields zero content deltas (reasoning-only models, empty completion, content filtered) leaves m_statusLabel at '正在测试连接...' indefinitely.`

### Suggested fix
- Comply with MCP protocol specifications, raise proper JSON-RPC errors, and handle lifecycle/path policies securely.

### References / Dedupe
- AGMCP-9 explicitly excludes #236 (different widget); #233/#235 unrelated; no prior issue on the copilot labels.
- Finding AGMCP-6: #188 fixed parallel tool-call merge + timeout; double-finished not covered
- Finding AGMCP-7: #222 covered initialize existence; echo + notification-reply behaviors not covered
- Finding AGMCP-8: #186 fixed catalog/schema divergence and data: routing; direct-name routing gap remains — related but distinct seam
- Finding AGMCP-9: #236 'hidden log-panel HTML appends' is a different widget; no prior issue on copilot labels
- Finding AGMCP-10: #186 'drifted schemas' fixed the processing-tool side (cac5da9469); interaction-provider drift and group-filter doc mismatch remain
- Finding AGMCP-11: No prior issue; #241 tested TaskCenterDock idle-path only, unrelated
- Finding AGMCP-13: No prior issue covers key storage
- Finding AGMCP-14: No prior issue; #188 streaming fixes unrelated


---

# #349: [P3][error-handling] Python host minors batch: unbounded crash-restart loop; daemon malformed-line handler stale msg; setArgvCommand quoting unfixed despite #234; re-entrant lease leak; unauthenticated IPC socket + connection steal; /dev/shm 0644; double raster materialization; daemon line correlation; plugin menu empty lambdas + destroyed model on unload

### Area
Error Handling / Exception Safety

### Type
error-handling

### Severity
P3

### Affected code
- `src/python/isolated/python_worker_process_pool.cpp:223-332` (PythonWorkerProcessPool::handleWorkerCrash)
- `src/python/scripts/worker_daemon.py:205-208, 505-517` (main() dispatch loop)
- `src/python/sicnu_python_runner.cpp:27-40` (SicnuPythonRunner::setArgvCommand)
- `src/data/data_manager.cpp:1131-1140, 1204-1226` (DataManager::unload)
- `src/python/isolated/python_ipc_server.cpp:30-35, 67-84` (PythonIpcServer::listen / onNewConnection)
- `src/python/isolated/shared_memory_segment.cpp:64-100` (SharedMemorySegment::create)
- `src/python/isolated/app_interface_bridge.cpp:57-209` (migrateRasterInputToShm (anonymous namespace))
- `src/python/scripts/worker_daemon.py:45-62, 113-130, 199-208` (SicnuMapCanvasProxy._get_state / SicnuPythonIface.activeLayer / main dispatch loop)
- `src/plugins/layer_tree/layer_tree_plugin.cpp:50, 69-72, 84-96` (LayerTreePlugin::initialize / unload / menuActions (and src/plugins/processing/processing_plugin.cpp:56-60))

### User impact
- Resource exhaustion / log spam and an unusable Python host that never reports a terminal failure. ADR 0045's PoolHealthSnapshot exposes totalRestarts but nothing consumes it to stop the loop.
- Low-frequency robustness hole in the daemon protocol layer (focus: malformed JSON lines / zombie daemons); current C++ host emits well-formed QJsonDocument lines so reachability is limited to partial writes/encoding edge cases.
- Functional failure for paths containing quotes; also an injection surface for any caller-controlled argv (same-user). dup_check: issue #234 ('Misc build/portability batch: … setArgvCommand quoting …') is CLOSED and its fix commit 646d3ea112 (verified via git show --stat) touched none of src/python/sicnu_python_runner.cpp — the defect is present at baseline HEAD d9c59e5b, so the fix is genuinely incomplete.
- Catalog state inconsistency in a re-entrancy scenario the function otherwise carefully defends against; narrow trigger keeps this P3.
- Same-user DoS (kills the daemon mid-plugin-run, in-flight py: tasks then time out after 300 s) and unauthenticated use of the app's data/UI RPC surface (arbitrary file registration and rendering). Socket names embed the app PID but live in the world-readable temp dir.
- Information disclosure of potentially sensitive imagery to co-resident local users; transient (segments are unlinked on detach/reader completion — that leak-fix part works).
- Memory pressure and main-thread stall for the advertised zero-copy path; large scenes can fail segment creation and silently fall back to the file path (returning {} at 143-144), losing the optimization exactly where it matters.
- Dropped plugin actions and wrong canvas/activeLayer data under concurrent use; complements rather than duplicates the fixed #211 deadlock.
- No-op menu entries (user-visible, #215 class) and a crash window during plugin teardown; currently mitigated only because unloadAll runs at exit.

### Reproduction
- **Trigger (DATAPY-4)**: Any environment where the selected python interpreter starts but its numpy (or another top-level import) aborts the process — mixed-ABI numpy installs are a classic case.
- **Trigger (DATAPY-5)**: Any invalid JSON line or undecodable byte from the host; also any exception raised between assignment and use.
- **Trigger (DATAPY-6)**: Any argv element containing an apostrophe (legal in Unix paths) or a quote.
- **Trigger (DATAPY-7)**: A slot connected to assetAboutToUnload that acquires a lease (defensive re-pin patterns); re-entrancy is explicitly anticipated by the code for records but not for leases.
- **Trigger (DATAPY-8)**: Local same-user execution (script, other tool, compromised same-user process). No cross-user boundary is crossed (UserAccessOption).
- **Trigger (DATAPY-9)**: Multi-user machine; any zero-copy py: execution in progress.
- **Trigger (DATAPY-10)**: Every opted-in zero-copy py: execution; cost scales linearly with scene size and is paid on the main thread inside the algorithm adapter (sendRequestSync caller).
- **Trigger (DATAPY-11)**: Any host→daemon request arriving while a plugin callback is blocked in a proxy RPC (double menu click is enough).
- **Trigger (DATAPY-12)**: Plugin unload path (unloadAll at teardown today; any future per-plugin unload UI) or clicking the dead 'Add Raster Layer…'/'Processing Toolbox' actions if surfaced.

### Expected behavior
- **Expected (DATAPY-4)**: A restart budget or exponential backoff (restartCount exists precisely to support this) so a poisoned worker environment parks the node and surfaces poolHealth degradation instead of spinning.
- **Expected (DATAPY-5)**: A parse failure replies with id=null (JSON-RPC error -32700) and the loop continues.
- **Expected (DATAPY-6)**: Arguments escaped (or marshaled via a non-source channel) so sys.argv matches the input exactly.
- **Expected (DATAPY-7)**: Post-emit revalidation of leaseImpacts (mirroring the record iterator re-location at 1212-1214) so no live lease outlives its asset.
- **Expected (DATAPY-8)**: At minimum: reject a second connection while a worker is attached (fail-closed), and treat unexpected disconnects as a worker loss (they do propagate via clientDisconnected).
- **Expected (DATAPY-9)**: 0600 segments (fchmod after create, or a Qt API that takes permissions).
- **Expected (DATAPY-10)**: Windowed/tiled reads straight into the interleaved segment (or band-planar layout) and memcpy per row rather than per element.
- **Expected (DATAPY-11)**: Proxy reads match on the correlated request id and queue/dispatch non-matching lines back to the main loop.
- **Expected (DATAPY-12)**: unload() detaches the view model and the host unloads libraries only after menu-owned plugin objects are destroyed.

### Actual behavior
- **Actual (DATAPY-4)**: Unbounded crash-restart loop: continuous process churn, CPU burn, and warning-log spam for the whole app session.
- **Actual (DATAPY-5)**: Stale-id error (protocol mis-correlation, caller times out) or daemon suicide on the first message.
- **Actual (DATAPY-6)**: Broken generated Python source; the command fails and QGIS-side behavior that depends on sys.argv silently misbehaves.
- **Actual (DATAPY-7)**: Lease leak: the asset id is gone, the lease remains active, and any future asset re-registered with the same id would inherit a phantom lease count blocking unload.
- **Actual (DATAPY-8)**: Last-connector-wins replacement plus full data./canvas./ui. RPC surface with zero authentication.
- **Actual (DATAPY-9)**: World-readable raster data in shared memory.
- **Actual (DATAPY-10)**: Full-plane buffering + O(W·H·bands) scalar copies; ~2× peak memory and significant wall time for large scenes.
- **Actual (DATAPY-11)**: Request/response mis-correlation: lost host requests and empty proxy results.
- **Actual (DATAPY-12)**: Dead menu actions; teardown ordering leaves dangling model/actions referencing unmapped code.

### Root cause
- **DATAPY-4**: confirmed
  - *Callchain*: `daemon start → import numpy segfaults (exit 139) → QProcess finished(CrashExit) → workerCrashed → handleWorkerCrash → new server (listen on fresh unique name) + startWorker → daemon crashes again → …infinite tight loop.`
  - *Code context*: `handleWorkerCrash increments `node->restartCount++` (line 254) but the counter is only ever reported in PoolHealthSnapshot::totalRestarts — never compared against a limit, no delay before `node->worker->startWorker(...)` (line 268). onProcessFinished (src/python/isolated/python_worker_process.cpp:120-127) emits workerCrashed for CrashExit or unusual exit codes (137/15/9/1 are tolerated), so a deterministic SIGSEGV/SIGABRT at daemon startup (e.g. a broken native numpy in the probed python — worker_daemon.py:10 `import numpy as np` at module top) re-enters handleWorkerCrash immediately and forever.`
- **DATAPY-5**: confirmed
  - *Callchain*: `C++ host → malformed/non-UTF8 line on the unix socket → json.loads/decode raises → except at 507 → msg.get('id') on stale binding → wrong-id error sent (or daemon exit on first message).`
  - *Code context*: ``msg = json.loads(line)` (206) is inside the per-message try; the handler `except Exception as ex:` (507) builds the error response with `"id": msg.get("id")` (511). If json.loads raised, `msg` is either unbound (very first message → NameError inside the except, caught by the outer except at 515 → `break` → daemon exits) or still bound to the PREVIOUS iteration's message (error response carries a stale id; the malformed request's real id never gets a reply, and a pending C++ callback with the stale id can be resolved with the wrong error).`
- **DATAPY-6**: confirmed
  - *Callchain*: `Vendored QGIS core QgsPythonRunner::setInstance(new SicnuPythonRunner()) (qgis_python.cpp:229) → QGIS processing/expression paths calling QgsPythonRunner::setArgv (src/core/qgspythonrunner.cpp:78) → runString of the concatenated literal.`
  - *Code context*: ``cmd += QStringLiteral( "'%1'" ).arg( arguments.at( i ) );` (line 35) — no escaping of `'`, backslash, or newlines. Contrast QgisPython::addPath (src/python/qgis_python.cpp:344-352) which escapes \\, ', \n, \r for exactly this reason. An argument like /home/user/kevin's tile.tif produces `sys.argv = ['/home/user/kevin's tile.tif']` → SyntaxError → runString fails and returns false.`
- **DATAPY-7**: DataManager::unload cascade path revokes a lease list captured before assetAboutToUnload — leases acquired in re-entrant slots leak as live records on an erased asset
  - *Callchain*: `cascade unload → emit assetAboutToUnload → connected slot re-enters DataManager::acquire (new Task/View lease) → back in unload(): only the stale impact list revoked → record erased → dangling LeaseRecord until DataManager destruction.`
  - *Code context*: ``liveLeaseImpacts` is captured at 1131-1132 before any signal. The cascade branch emits `assetAboutToUnload(confirmedPlan.assetId())` (1204) — the code's own comment at 1188-1189 acknowledges 'a connected slot may re-enter the manager and mutate the records'. A slot that acquires a new lease during that emit is not in liveLeaseImpacts; only those are revoked (1206-1210) before the record is erased (1214-1226). The new lease's LeaseRecord stays in m_impl->leases with control->manager still set, so leaseCount()/leases() report leases for an asset id that no longer exists, and AssetLease::isValid() stays true.`
- **DATAPY-8**: confirmed
  - *Callchain*: `Any same-user process → connect(sicnu_pool_<pid>_<id> in tempdir, name discoverable via /proc or directory listing) → onNewConnection kicks the daemon → impostor holds the socket → bridge dispatches its RPCs against the app's DataManager/UI.`
  - *Code context*: `listen() sets only `QLocalServer::UserAccessOption` (33) — any process of the same user can connect, and there is no handshake or peer verification. onNewConnection unconditionally replaces the current client: 'if (m_socket) { m_socket->disconnect(); m_socket->close(); m_socket->deleteLater(); } m_socket = m_server->nextPendingConnection();' (69-77). A stray same-user connect therefore disconnects the real daemon (its recv loop breaks at worker_daemon.py:196-197 → daemon exits) and the impostor becomes the worker, free to call data.add_layer (arbitrary local path registration/display), catalog.set_active_layer, ui.push_message_bar, and processing.register_algorithm via handleIpcMessage/dispatchIpcMessage (app_interface_bridge.cpp:611-688).`
- **DATAPY-9**: confirmed
  - *Callchain*: `py: task with __shm_key__ opt-in → migrateRasterInputToShm → SharedMemorySegment::create → /dev/shm object with default 0644 → other-user `cat /dev/shm/sicnu_shm_*` exfiltrates the scene.`
  - *Code context*: `create() calls `m_shm->create(static_cast<int>(totalSize))` (74) on a QSharedMemory in PosixRealtime mode without any permission control; Qt's POSIX backend shm_open()s the object with mode 0644, so /dev/shm/sicnu_shm_<uuid> (payload = full raster pixels migrated by app_interface_bridge.cpp:57-209) is readable by every local account for the lifetime of the py: execution. The matching semaphore (sem.sicnu_shm_<uuid>) is equally exposed.`
- **DATAPY-10**: confirmed
  - *Callchain*: `py: execute with __shm_key__ → migrateRasterInputToShm → readBandDataNative full planes → per-pixel interleave into SharedMemorySegment payloads.`
  - *Code context*: `Lines 116-132 read every band into a full W·H plane (`nativePlanes.resize(bands, vector<unsigned char>(width*height*elemSize))`), then lines 146-169 scatter per pixel: `for x … memcpy(payload + ((dstRow + x) * bands + b) * elemSize, src, elemSize); src += elemSize;` — an element-wise loop per row per band. Peak host RAM is ~1× the raster for the planes plus the /dev/shm segment(s) holding another full interleaved copy, so a 1 GB scene costs ~2 GB process memory + 1 GB /dev/shm before the RPC even starts.`
- **DATAPY-11**: confirmed
  - *Callchain*: `User triggers plugin menu action → bridge sends ui.on_action_triggered (async, app_interface_bridge.cpp:439-447) → daemon invokes plugin callback → callback calls iface.mapCanvas().extent() → _get_state blocks on recv → meanwhile host sends a second ui.on_action_triggered (or any request) on the same socket → daemon consumes it as the 'response' → second action lost, first call returns {} with no canvas state.`
  - *Code context*: `_get_state sends canvas.get_state then `while True: data = self._s.recv(4096); …; if "\n" in buf: … return json.loads(line).get("result", {})` — it accepts ANY newline-terminated line, including a host→daemon REQUEST (which has "method" and no "result" → returns {}). The real response line later arrives at the main dispatch loop, which skips it: `if "method" not in msg and ("result" in msg or "error" in msg): continue` (207-208). The consumed host request is never dispatched, so its C++ callback stays pending until timeout/crash-recovery.`
- **DATAPY-12**: confirmed
  - *Callchain*: `PluginHost::loadPlugins → QPluginLoader instance → menuActions() added to menu → (shutdown) PluginHost::unloadAll → interface->unload() (no view detach) → loader->unload() unmaps .so → menu/model hold objects whose vtables/code are gone.`
  - *Code context*: `m_model = new QgsLayerTreeModel(root, this) is parented to the plugin (50); layerTree->setModel(m_model) (62) leaves the view holding it. unload() (69-72) does nothing to detach, and PluginHost::unloadAll (src/core/plugin_host.cpp:159-177) calls loader->unload() which destroys the root component (and with it the model) and unmaps the library — while main_window.cpp:167 has already reparented the plugin's menuActions() QActions into a QMenu. menuActions themselves connect to empty lambdas ('// This will be handled by the main window', 85-87 and 91-93; ProcessingPlugin 'Show processing toolbox' → empty lambda at processing_plugin.cpp:57-59), the same defect class as the closed #215.`

### Suggested fix
- Implement robust error reporting, return explicit error codes/exceptions, and prevent silent failures or unhandled exception aborts.

### References / Dedupe
- DATAPY-8/#116 different contract issue; DATAPY-9/#117 covers unlink only; DATAPY-6/#234 includes the quoting (verified unfixed).
- Finding DATAPY-4: Grepped issues-closed.txt for 'restart'/'zombie'/'worker': #232 (path discovery, fixed & verified), #210/#233 (IPC lifetime, fixed). No closed issue covers restart capping. Still defective at HEAD.
- Finding DATAPY-5: Grepped issues-closed.txt for 'daemon'/'JSON'/'malformed': #211 (deadlock, fixed), #116 (IPC seam truthfulness, closed feature). Not covered.
- Finding DATAPY-6: #234 claimed; verified unfixed at HEAD (commit 646d3ea112 modified 13 files, sicnu_python_runner.cpp not among them).
- Finding DATAPY-7: #106/#110 (TaskCenter lock reentrancy) and #56 (dependency DAG) do not cover lease revalidation on unload. Not in issues-closed.txt.
- Finding DATAPY-8: issues-closed.txt has no entry for IPC socket authentication/connection stealing (#116 'Make the Python plugin IPC seam truthful' is a different contract issue). Verified present at HEAD.
- Finding DATAPY-9: ADR 0064 leak fix (unlink lifecycle) is covered by closed issue #117 ('Wire or delete the shared-memory raster transfer path'); permissions were not part of it. Not otherwise covered.
- Finding DATAPY-10: #218/#184-class main-thread stalls were GUI-layer findings; the shm migration cost is not covered by any closed issue (#117 wired the path).
- Finding DATAPY-11: #211 (closed) fixed the C++-side deadlock inside execute_algorithm; the daemon-side correlation flaw is separate and still present.
- Finding DATAPY-12: #215 (empty lambda, ribbon) and #236 (task/layer menu UAFs) are adjacent but neither covers the plugin .so unload hazard or these two plugins' actions. Reported as residual instances of a fixed class plus a new teardown hazard.


---

# #351: [P3][processing/provider] gdal provider minors batch 2: dead global error handler; exact-WKT CRS equality false positives; gdal:warp missing -overwrite; rasterize rotation/gdal_edit/extent conventions

### Area
Processing Framework / GDAL & OTB Providers

### Type
processing/provider

### Severity
P3

### Affected code
- `src/data/raster_grid_compat.cpp (fed by src/processing/gdal/gdal_grid_compat.cpp):118-143` (compareGrids / gridFromDataset)
- `src/processing/providers/gdal_tools/algorithms/gdal_warp.cpp:19-44` (GdalWarpAlgorithm::buildArgs)
- `src/processing/providers/gdal_tools/algorithms/:gdal_rasterize.cpp:61-71; gdal_edit.cpp:61-67; gdal_warp.cpp:29-34 + gdal_grid.cpp:72-80 vs ogr2ogr.cpp:24-26,96-101; gdal_calc.h:20 + tool_path_manager.cpp:18-40; gdal_translate.cpp:12-13,27-30 + gdalbuildvrt.cpp:22-23; gdaltransform.cpp:147-157` (GdalRasterizeAlgorithm::buildArgs; GdalEditAlgorithm::buildArgs; toolName()/ToolPathManager::gdalToolPath; GdalTransformAlgorithm::processAlgorithm)

### User impact
- False blocking of legitimate operations; user must reproject a raster that is already in the right CRS (safe-direction failure, but a real usability/correctness gap in the #191-fixed service)
- Reruns fail confusingly at best; at worst produce a mosaic of two runs' pixels without any warning
- Individually small; collectively wrong-or-confusing CLI behavior in the gdal tool layer that the #169/#228 fix batches did not reach

### Reproduction
- **Trigger (PROVG-9)**: Pair rasters whose CRSs are the same projection but stored as different WKT flavors (very common when mixing ESRI-produced and GDAL-produced files)
- **Trigger (PROVG-10)**: Run gdal:warp twice to the same explicit OUTPUT path (user-specified file or repeated batch target), with TARGET_CRS/EXTENT set
- **Trigger (PROVG-11)**: (a) rotated raster template; (b) only X_RES filled; (c) one extent string reused across warp and ogr2ogr; (d) deployment with C++-only GDAL utilities; (e) non-GTiff FORMAT selected; (f) new output folder in gdal:transform

### Expected behavior
- **Expected (PROVG-9)**: Semantic CRS equality (e.g. QgsCoordinateReferenceSystem comparison via OSRIsSame/authority codes) — mismatch only for genuinely different CRSs
- **Expected (PROVG-10)**: -overwrite emitted (matching QGIS behavior) so reruns replace the output
- **Expected (PROVG-11)**: Rotation-aware -te; validation error or single-axis -tr; one canonical extent order across tools (or format-aware parsing); .py/name fallback in resolution; extension matching the chosen format; resolved destination in gdaltransform

### Actual behavior
- **Actual (PROVG-9)**: False-positive blocking 'grid.crs_mismatch' forcing unnecessary reprojection; multi-layer ops refuse to run
- **Actual (PROVG-10)**: Hard failure with GDAL's 'delete existing dataset' error, or — with no creation options — silent blending of old and new coverage
- **Actual (PROVG-11)**: As listed per sub-item — all silent except (d)/(f) which fail with generic errors

### Root cause
- **PROVG-9**: confirmed
  - *Callchain*: `rs change-detection / post-classification compare / fusion dialogs -> gridFromDataset + compareGrids -> blocking grid.crs_mismatch`
  - *Code context*: ``if ( aHasCrs && bHasCrs && a.crsWkt != b.crsWkt )` -> blocking CrsMismatch. gridFromDataset stores ds.projection() raw (GDALGetProjectionRef), which returns whatever WKT dialect the producer wrote (ESRI .aux/.prj vs OGC GeoTIFF key encoding vs WKT2); identical projections expressed differently never compare equal as strings. All consumers (change detection rs_change_primitives.cpp:80, rs_change_detection_operator.cpp:265, rs_post_classification_change_operator.cpp:155, image_fusion.cpp:712, dialog_utils.cpp:148) block on this verdict.`
- **PROVG-10**: confirmed
  - *Callchain*: `Toolbox dialog / pipeline rerun -> TaskCenter -> ProviderAlgorithmAdapter::execute -> GdalToolWrapper::processAlgorithm -> GdalWarpAlgorithm::buildArgs -> gdalwarp subprocess`
  - *Code context*: `buildArgs emits only -t_srs/-te/EXTRA + INPUT/OUTPUT — no -overwrite. Empirically verified on GDAL 3.13.2: second `gdalwarp -te ... src dst` with existing dst -> 'ERROR 1: Output dataset ... exists, but some command line options were provided indicating a new dataset should be created. Please delete existing dataset and run again.' (exit != 0). Without -te/-t_srs/-tr, gdalwarp instead warps INTO the existing dataset (append/blend semantics) — stale pixels survive wherever the new coverage does not overwrite. QGIS's own gdalwarp wrapper always passes -overwrite. Other wrappers (gdal_translate & co.) truncate via GDALCreate, so only warp is affected.`
- **PROVG-11**: confirmed
  - *Callchain*: `Toolbox dialogs / pipelines -> TaskCenter -> ProviderAlgorithmAdapter::execute -> GdalToolWrapper::processAlgorithm -> buildArgs -> external tools`
  - *Code context*: `(a) rasterize template path derives -te from only gt[0],gt[3],gt[1],gt[5] — gt[2]/gt[4] rotation/shear ignored (same class as #189 VRT fix; rotated template -> wrong burn extent); (b) gdal_edit emits -tr only when BOTH X_RES and Y_RES are set — a lone X_RES is silently ignored, no validation error; (c) warp/grid/rasterize parse EXTENT as 'xmin,xmax,ymin,ymax' while ogr2ogr documents and parses SPAT as 'xmin,ymin,xmax,ymax' — one 4-number string silently yields a transposed box in the other tool (split(',').size()==4 passes either way); (d) toolName() hardcodes .py names (gdal_calc.py, gdal_merge.py, gdal_edit.py, gdal_retile.py, pct2rgb.py, rgb2pct.py, gdal2xyz.py) and gdalToolPath has no basename fallback — works on distros shipping both (Arch GDAL 3.13) but fails 'GDAL tool not found' on GDAL builds without Python scripts (e.g. vcpkg Windows); (e) gdal_translate/pct2rgb/rgb2pct FORMAT enums include PNG/JPEG/AAIGrid while OUTPUT is a RasterDestination defaulting to a .tif name (gdalbuildvrt likewise writes VRT content into output.tif) — extension lies about format for external tools; (f) gdaltransform's processAlgorithm override skips resolveDestinationParameters, writing to the raw OUTPUT string (QFileInfo parent-dir creation that parameterAsOutputLayer performs never happens -> 'Failed to write output file' when the folder is new).`

### Suggested fix
- Align parameter definitions, argument builders, and result harvesting with the QGIS/GDAL/OTB provider execution contract.

### References / Dedupe
- No prior issues for these provider minors; #231/#241 are schema/runtime path items.
- Finding PROVG-9: #191 addressed over-permissive mirrored acceptance; the over-strict string-CRS comparison is the unreported opposite direction. Verified at HEAD.
- Finding PROVG-10: #169/#228/#230 did not touch gdal_warp.cpp overwrite behavior (git log: last substantive change 8cc594609a). Verified at HEAD.
- Finding PROVG-11: #169 (commit 8cc594609a) fixed -of enum index/-ts/-te order/-outsize; #228 fixed contour interval, gdalmanage NEWNAME, gdaltransform failure path; none of sub-items (a)-(f). #189 fixed VRT SrcRect rotation only. Verified all at HEAD.


---

# #353: [P3][algorithm/numerics] Algorithms numerics minors batch: NaN-neighbor terrain edges; cellSize ignores |gt[5]|; QVector<bool> cross-thread writes; ROI means include NoData; GS through-origin coefficients; 4-band fusion cap + NN upsample; computeStats skips inf; histogram-equalize normalization; Otsu bin/threshold grid mismatch

### Area
Algorithms / Image Processing / Numerics

### Type
algorithm/numerics

### Severity
P3

### Affected code
- `src/processing/algorithms/terrain_analysis.cpp:66-67, 94-95, 176-177, 263-264` (TerrainAnalysis::slope/aspect/hillshade (processBorder/processInterior hasNodata checks))
- `src/operators/rs/rs_terrain_analysis_operator.cpp:106-111` (RsTerrainAnalysisOperator::run (cellSize derivation), TerrainAnalysis kernels take a single cellSize)
- `src/processing/algorithms/chunked_processor.cpp:50, 60-81` (ChunkedProcessor::process)
- `src/processing/algorithms/spectral_roi.cpp:121-135` (SpectralRoiProfile::meanSpectrum)
- `src/processing/algorithms/image_fusion.cpp:1338-1352 (streaming) vs 556-603 (in-memory)` (ImageFusion::processNativeFusion(method=gram_schmidt) vs ImageFusion::gramSchmidtFusion)
- `src/processing/algorithms/image_fusion.cpp:739, 805-815` (ImageFusion::processNativeFusion (nMsBands cap; readMsWindow))
- `src/processing/algorithms/math_utils.cpp:32-76, 81-138` (MathUtils::computeStats / computeStatsWithNodata)
- `src/processing/algorithms/image_enhancement.cpp:119-142` (ImageEnhancement::histogramEqualize)
- `src/processing/algorithms/change_detection.cpp:158-170, 213` (ChangeDetection::otsuThreshold / otsuThresholdFromHistogram)

### User impact
- Mixed sentinels (-9999 vs NaN) in one output plus undeclared NoData (see RSALGO-4); edge statistics differ between terrain products on the same DEM.
- Slope magnitude and aspect direction biased on anisotropic grids; silent because outputs remain plausible-looking.
- Latent correctness hazard in the shared parallel primitive; TSan reports under any multi-chunk failure.
- Wrong mean spectra feed the spectral library matching workflow (SAM/SID against polluted references); same class as #230 segment-stats means.
- Degraded spectral fidelity in the operator-facing GS path plus an inconsistency between the two GS implementations; fused colors drift toward the pan.
- Data loss on multi-band products; measurable fused-image quality loss vs bilinear upsample; user gets no signal that 4+ bands were discarded.
- Whole-band display/statistics collapse from one bad pixel; inconsistent validity semantics across the math utilities.
- Systematically flatter equalization than the reference implementations; subtle but visible contrast loss on dark-scene bands.
- Small systematic bias in reported SICNU_CHANGE_THRESHOLD and the derived change mask; inconsistencies with the percentile variant on identical histograms.

### Reproduction
- **Trigger (RSALGO-9)**: DEM whose band NoData is NaN (common for Float32 DEM exports) with interior NaN regions or border pixels
- **Trigger (RSALGO-10)**: DEM with non-square pixels (warped/resampled products, lat-lon tiles with differing dLat/dLon spacing, aggregated products like MODIS 250m x 500m bands)
- **Trigger (RSALGO-12)**: Any future callback returning false (or checking the return) from a chunk running concurrently with another chunk in the same 32-index window
- **Trigger (RSALGO-13)**: Draw an ROI partially over NoData fill on any multispectral raster and read the mean spectrum
- **Trigger (RSALGO-14)**: Gram-Schmidt pan-sharpening on DN-scale MS/pan (the normal case)
- **Trigger (RSALGO-15)**: Fusion with >4-band MS input, or any pan/MS resolution ratio >1 (the advertised heterogeneous-resolution path)
- **Trigger (RSALGO-16)**: Any input containing +/-inf pixels (e.g. unguarded division upstream, Float32 saturation) fed to a stddev stretch or change statistics
- **Trigger (RSALGO-17)**: Histogram-equalization stretch on any band whose lowest populated bin holds a non-trivial fraction of pixels (typical: shadow/water-heavy scenes, clipped products)
- **Trigger (RSALGO-18)**: Any Otsu auto-threshold change detection (compare against percentile on the same magnitude raster)

### Expected behavior
- **Expected (RSALGO-9)**: NaN neighbor treated as missing -> fallback dzdx=(f-d)/2cs formula, out = nodata sentinel for missing centers
- **Expected (RSALGO-10)**: Separate cellSizeX/cellSizeY (gt[1], |gt[5]|) as in GDAL gdaldem (-s accepts both via geotransform)
- **Expected (RSALGO-12)**: Thread-safe per-chunk status (std::vector<std::atomic<bool>> or per-thread results + merge)
- **Expected (RSALGO-13)**: NoData-sentinel pixels excluded like NaN pixels
- **Expected (RSALGO-14)**: Centered coefficient (covariance/variance) as used by the in-memory variant
- **Expected (RSALGO-15)**: All MS bands fused (or an explicit band-selection parameter); bilinear/cubic MS resample
- **Expected (RSALGO-16)**: Non-finite (NaN and inf) values excluded, matching DarkObjectStats' isfinite convention
- **Expected (RSALGO-17)**: out = (cdf - cdf_min)/(N - cdf_min) * 255, darkest populated value -> 0, brightest -> 255
- **Expected (RSALGO-18)**: Threshold placed at the center of the accumulated histogram bin: min + (b+0.5)*range/(bins-1)

### Actual behavior
- **Actual (RSALGO-9)**: NaN enters (c2+2f+i)-(a+2d+g) -> out=NaN (a second, undeclared sentinel) instead of -9999; results inconsistent with tri/tpi/roughness
- **Actual (RSALGO-10)**: Single |gt[1]| for both axes
- **Actual (RSALGO-12)**: UB data race on shared bit words; unreliable failure propagation
- **Actual (RSALGO-13)**: Sentinel values averaged into the per-band mean/stddev
- **Actual (RSALGO-14)**: Through-origin coefficient biased by band means; different results from the in-memory API for identical inputs
- **Actual (RSALGO-15)**: Bands 5..N silently dropped; nearest-neighbor replication introduces blocky artifacts that the pan substitution then sharpens
- **Actual (RSALGO-16)**: min/max/mean/stddev become inf or -inf; linear stretch degenerates to a flat output
- **Actual (RSALGO-17)**: out = cdf*255; output dynamic range compressed by cdf_min*255 and never reaches 0
- **Actual (RSALGO-18)**: min + (b+0.5)*range/bins - biased low by up to 0.4% of the magnitude range

### Root cause
- **RSALGO-9**: confirmed
  - *Callchain*: `rs:terrain_analysis -> TerrainAnalysis::slope (rs_terrain_analysis_operator.cpp:144)`
  - *Code context*: `hasNodata checks are `a == nodata || b == nodata || ...` with no std::isnan on neighbors (the center pixel does get `std::isnan(z)` at :55/:83). tri/tpi/roughness DO check `v == nodata || std::isnan(v)` (:332, :383, :434). The operator passes nodata=-9999 whenever the band NoData is NaN (`if (hasNodata && std::isfinite(dsNodata))`, rs_terrain_analysis_operator.cpp:119) so float DEMs with NaN NoData produce NaN results through the Horn sum instead of the intended neighbor-fallback, and the output mixes NaN pixels with -9999 pixels.`
- **RSALGO-10**: confirmed
  - *Callchain*: `rs:terrain_analysis -> TerrainAnalysis::slope/aspect/hillshade`
  - *Code context*: ``const std::array<double,6> gt = ds.geoTransform(); if (std::abs(gt[1]) > 1e-7) cellSize = static_cast<float>(std::abs(gt[1]));` - gt[5] (Y resolution) never consulted; the kernels accept one scalar used for both dzdx and dzdy (terrain_analysis.cpp:47-49). Horn slope magnitude = atan(sqrt((dz/dx)^2+(dz/dy)^2)); with cellSizeX != |gt[5]| the steeper-axis gradient is scaled by the wrong factor (e.g. 1m x 2m pixels -> dzdy halves -> slope underestimated up to ~2x, aspect rotated toward the X axis).`
- **RSALGO-12**: confirmed (latent data race)
  - *Callchain*: `ImageEnhancement::leeFilter/enhancedLeeFilter/frostFilter/kuanFilter/gammaMapFilter -> ChunkedProcessor::process`
  - *Code context*: ``QVector<bool> results(m_chunks.size());` then inside QtConcurrent::blockingMap lambda `results[idx] = callback(...)` from multiple pool threads. QVector<bool> packs 32+ booleans per word; distinct indices in the same word race on load-modify-store (not std::vector<bool> style atomicity, no synchronization). The check loop `for (bool r : results) if (!r) return false;` may observe lost updates. Currently latent: the only users (ImageEnhancement speckle filters, image_enhancement.cpp:667/729/806/885/956) always `return true` and ignore process()'s return value, so no functional impact today.`
- **RSALGO-13**: confirmed
  - *Callchain*: `rs_roi_spectrum_tool (src/app/map_tools/rs_roi_spectrum_tool.cpp:96) -> SpectralRoiProfile::meanSpectrum -> spectral workbench / library matching`
  - *Code context*: `Per-band accumulation filters only `if (!std::isfinite(value)) continue;` - the band's declared NoData sentinel (read via GDALGetRasterNoDataValue is never done here) is included in sum/sumSq/validCount. An ROI overlapping scene border fill (nodata=0) or -9999 drags the mean/stddev toward the sentinel for every band.`
- **RSALGO-14**: confirmed
  - *Callchain*: `rs:image_fusion / fusion aliases (rs_fusion_aliases.cpp:108) -> processNativeFusion('gram_schmidt')`
  - *Code context*: `Streaming pass 1: `normSq[0] += synVal*synVal; coef[k][0] += msBuf[b][i]*synVal;` then `coef[b+1][0] /= normSq[0]` - a through-origin slope sum(ms*syn)/sum(syn^2). The in-memory gramSchmidtFusion computes the centered coefficient with the explicit justification at 558-563: 'A through-origin dot product is dominated by the bands' mean offsets (DN imagery has large means), which leaves nearly all of band k's offset in the residual and degrades spectral fidelity'. On DN-scale data the through-origin slope ~= mean_ms/mean_syn (~1) instead of Cov/Var, applying the full pan detail to weakly-correlated bands.`
- **RSALGO-15**: confirmed
  - *Callchain*: `rs:image_fusion / fusion aliases -> processNativeFusion`
  - *Code context*: ``const int nMsBands = std::min( msBands, 4 );` with no warning or parameter - an 8-band (e.g. WorldView/SuperDove) or 224-band MS input yields a 4-band fused product and bands 5+ are silently dropped (non-IHS methods promise 'fused bands (same count as msBands)' in the header docs). readMsWindow resamples differing-resolution MS onto the pan grid via readBandWindowScaled -> GDALRasterIO default GRA_NearestNeighbour (gdal_dataset_wrapper.cpp:304-307), so each 20m MS pixel is block-replicated 2x2 on a 10m pan grid before fusion - standard practice is bilinear/cubic for the MS upsample.`
- **RSALGO-16**: confirmed
  - *Callchain*: `stddevStretch/histogramEqualize (image_enhancement.cpp:75, 92) -> MathUtils::computeStatsWithNodata; ChangeDetection::statistics -> operator change reports`
  - *Code context*: `Validity predicate is `std::isnan(data[i])` only (find-first-valid loop :33, accumulation :55, stddev pass :69). A single +inf (saturated sensor value, overflow from an upstream division, Float32 max fill) makes max=inf, mean=inf, stddev=inf. Consumers: ChangeDetection::statistics (change_detection.cpp:75), ImageEnhancement::stddevStretch (means lo/hi=inf -> whole band collapses) and histogramEqualize (min/max=inf -> binWidth=inf -> all values bin 0), AtmosphericCorrection DarkObjectStats correctly uses isfinite (atmospheric_correction.cpp:93) showing the intended convention.`
- **RSALGO-17**: confirmed
  - *Callchain*: `contrast stretch presets (post-#183 mapping) / image enhancement panel -> ImageEnhancement::histogramEqualize`
  - *Code context*: ``cdf[0] = hist[0]/validCount; cdf[i] = cdf[i-1] + hist[i]/validCount; ... output[i] = cdf[bin]*255.0f;` - the raw CDF is mapped straight to 0-255. The standard equalization (Gonzalez-Woods; OpenCV equalizeHist) remaps via (cdf(v) - cdf_min)/(N - cdf_min)*(L-1) so the darkest populated value maps to 0. With hist[0]/N = h0, the darkest pixels output h0*255 (e.g. 5% dark clip -> floor at ~13) and the effective range shrinks to (1-h0)*255.`
- **RSALGO-18**: confirmed
  - *Callchain*: `rs:change_detection thresholdMethod=otsu (and the streaming histogram variant) -> otsuThresholdFromHistogram`
  - *Code context*: `Histogram fill: `int bin = static_cast<int>((v - minVal) / range * (bins - 1));` (bin width = range/(bins-1), maxVal lands in bin bins-1) but the threshold is placed at `minVal + (bestBin + 0.5) * range / bins` (bin width = range/bins). The reconstruction grid does not match the accumulation grid; for bestBin=b the returned level is min + (b+0.5)*range/bins while the true bin center is min + (b+0.5)*range/(bins-1) - off by (b+0.5)*range*(1/bins - 1/(bins-1)), up to ~0.4% of range at b=255 (bins=256). percentileThresholdFromHistogram (217-253) consistently uses range/bins, so Otsu and percentile thresholds computed from the same histogram are on different grids.`

### Suggested fix
- Correct the mathematical formulas, coordinate system transforms, and NoData boundary conditions to align with standard GIS conventions (Horn 1981, GDAL, IEEE-754).

### References / Dedupe
- RSALGO-16 #193/#190 are spatial-filter NoData modules, not stats; no prior math_utils inf issue.
- Finding RSALGO-9: #203 covered cellSize only; no issue covers neighbor NaN handling in these kernels.
- Finding RSALGO-10: #203 (commit 763966246f) fixed the hard 30.0 default; anisotropy remains at HEAD.
- Finding RSALGO-12: #229 (ddc029502c) made completedChunks atomic - the results array race was not addressed.
- Finding RSALGO-13: #230 fixed segment_stats and mosaic-Y in operators; the ROI profile kernel was not covered. #256 fixed its per-pixel I/O only.
- Finding RSALGO-14: No closed issue covers GS fusion numerics (7a822b2f40 was grid preflight/PCA NaN).
- Finding RSALGO-15: No closed issue; #182 was dialog band combos.
- Finding RSALGO-16: #175 fixed PPI NaN guards; #287 JM covariance underflow. No issue covers computeStats inf handling.
- Finding RSALGO-17: #224 mentions 'histogram-equalize edge cases' tests as never-compiled (test files unused) - the formula itself was never reported.
- Finding RSALGO-18: 519e8b7872 added the statistical/MMU methods; no closed issue covers Otsu bin math.


---

# #355: [P3][algorithm/numerics] OBIA minors batch: GLCM features include NoData pixels; int index overflow past 2^31 pixels; all-band W*H float materialization; kernel/bins spinboxes no-op on flat Segment

### Area
Algorithms / Image Processing / Numerics

### Type
algorithm/numerics

### Severity
P3

### Affected code
- `src/analysis/segmentation/rs_segment_features.cpp:113-125 (any-band NoData exclusion for sum/min/max) vs 266-290 (GLCM bbox scan with label-only check); level quantization 273 and 283` (RsSegmentFeatures::extract (buildStat GLCM block))
- `src/analysis/segmentation/rs_segment_features.cpp:90 and 107 ('for ( int i = 0; i < nPixels; ++i )' with size_t nPixels), 170/203/270/281 ('labels[r * w + c]'); same pattern in rs_segment_map.cpp:160/188/233 and rs_class_raster.cpp:161/195` (RsSegmentFeatures::extract / RsSegmentMap::labelAt / RsSegmentMap::buildCoordsForSegment / RsSegmentMap::toGeoTIFF)
- `src/analysis/segmentation/rs_segment_features.cpp:43-69` (RsSegmentFeatures::extract)
- `src/app/obia/rs_obia_main_window.cpp:516-527 (runSegmentation builds segCfg without spatialRadius/rangeRadius) vs 1047-1052 (runHierarchicalSegmentation maps kernelSpin->spatialRadius, binsSpin*0.5->rangeRadius); runOtb mapping at src/app/obia/rs_obia_segmentation.cpp:24-31` (RsObiaMainWindow::runSegmentation vs runHierarchicalSegmentation)

### User impact
- Wrong object classifications for segments adjacent to NoData regions in every OBIA path (flat + hierarchical); inconsistent feature semantics vs the spectral columns of the same matrix.
- Silently wrong per-segment features / label access on >2^31-pixel scenes - same defect class accepted as #201 for kmeans.
- GUI freezes/OOM on hyperspectral OBIA - the same defect class as #204 (continuum removal full-scene materialization, fixed there) which the audit focus explicitly asks to check elsewhere; this instance remains.
- Misleading parameter UI on the primary OBIA segmentation action; silent divergence from the hierarchy path

### Reproduction
- **Trigger (RSHYP-3)**: Any segment touching a NoData/NaN pixel (segment maps label them 0 only where the segmenter saw the sentinel; per-band NoData of bands other than band 1 and NaN float products are not masked by the segmenter at all) in the 8 GLCM columns that RsFeatureSelection enables by default.
- **Trigger (RSHYP-4)**: Scenes larger than 2^31 pixels (e.g. 46342x46342, 50000x45000 Sentinel-2 mosaics) on machines whose RAM satisfies the >=8.6 GB-per-band float allocation that precedes the loops.
- **Trigger (RSHYP-5)**: OBIA inspect/classify on a multi-band or hyperspectral raster in the OBIA window (band selection is not offered): 224-band 1024x1024 -> 224 * 4 MB ~ 900 MB RSS in one call; 224-band 3000x3000 -> ~8 GB (OOM-class); GLCM then re-scans every segment bbox per band.
- **Trigger (SHELLB-4)**: OTB installed; change Segments kernel/bins spinboxes; press Segment

### Expected behavior
- **Expected (RSHYP-3)**: GLCM pair accumulation skips the same pixels the mean/stddev/min/max accumulators skip (any-band NoData/NaN rule), keeping spectral and texture features consistent.
- **Expected (RSHYP-4)**: size_t/qsizetype loop variables and 64-bit index arithmetic, matching the size_t nPixels used for allocation.
- **Expected (RSHYP-5)**: Sequential per-band streaming (read band -> accumulate stats+GLCM -> release), as already done inside RsSimpleSegmenter::segmentMultiBand (one band resident at a time) - the kernel needs no cross-band buffer.
- **Expected (SHELLB-4)**: Spinbox values influence segmentation (as they do via Hierarchy)

### Actual behavior
- **Actual (RSHYP-3)**: Sentinel/NaN pixels are quantized into levels 0..15 and pollute contrast/energy/homogeneity/correlation; NaN additionally makes the level computation UB. Segments near NoData get systematically wrong texture features while their mean/stddev look correct (also rectangularity: area excludes NoData pixels but bbox includes them).
- **Actual (RSHYP-4)**: Accumulation silently covers only the first 2^31 pixels (or traps under UB); label writes/reads use overflowed int indices.
- **Actual (RSHYP-5)**: All bands resident for the whole call; peak RSS scales linearly with band count on the GUI thread path.
- **Actual (SHELLB-4)**: Identical result for any kernel/bins value (only minRegionSize has an effect on the OTB path)

### Root cause
- **RSHYP-3**: confirmed
  - *Callchain*: `OBIA GUI: src/app/obia/rs_obia_main_window.cpp:586/957/1133 (RsSegmentFeatures::extract with allBandIndices()) and src/app/obia/rs_obia_task.cpp:95 -> features feed RsSegmentFeatures::toFeatureMatrix -> RsObjectClassify::classify / RsHierarchyFeatures::buildFeatureMatrix (glcmContrast/Correlation/Energy/Homogeneity are 4 of the per-band feature groups enabled by default in RsFeatureSelection)`
  - *Code context*: `The accumulator loop skips pixels nodata in ANY band: 'bool isPixelNodata = false; for ( int b = 0; b < nBands; ++b ) { const float v = bandData[b][i]; if ( std::isnan( v ) || ( hasNoData[b] && static_cast<double>( v ) == bandNoData[b] ) ) { isPixelNodata = true; break; } } if ( isPixelNodata ) continue;' (113-125). The GLCM loop only checks segment membership: 'for ( int r = box.minR; r <= box.maxR; ++r ) ... if ( labels[r * w + c] != segId ) continue; const int level1 = std::clamp( static_cast<int>( ( bandData[b][r * w + c] - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 );' (266-273) - a pixel that is NoData in band b still contributes a level pair with its neighbor. With NaN the expression is NaN and 'static_cast<int>( NaN )' is undefined behavior (x86-64 typically yields INT_MIN, clamped to 0); with sentinel -9999 the level is clamped to 0, fabricating high-contrast pairs against valid neighbors.`
- **RSHYP-4**: confirmed (latent >2^31)
  - *Callchain*: `OBIA GUI and tasks call extract() with allBandIndices() (rs_obia_main_window.cpp:957/586/1133, rs_obia_task.cpp:95); toGeoTIFF/labelAt are on every segment-map write/read path`
  - *Code context*: `nPixels is computed correctly as size_t ('const size_t nPixels = static_cast<size_t>(w) * static_cast<size_t>(h);', line 40) but both full-image scans iterate an int: 'for ( int i = 0; i < nPixels; ++i ) maxLabel = std::max( maxLabel, labels[i] );' (90) and the accumulator loop (107). When nPixels > 2^31, i overflows at INT_MAX (UB); with the usual wrap the comparison (size_t)i < nPixels goes false and the loop silently stops at 2^31 pixels - maxLabel and all per-segment statistics are computed from only the first half of the scene. 'labels[r * w + c]' (170 etc.) and 'const int rowBase = r * mWidth;' (rs_segment_map.cpp:160) overflow for the same sizes.`
- **RSHYP-5**: confirmed
  - *Callchain*: `RsObiaMainWindow::inspectSelectedSegment / segmentation task completion (957, 586) and hierarchy build (1070, 1133), RsObiaTask (rs_obia_task.cpp:95) -> RsSegmentFeatures::extract -> per-band accumulators + per-band GLCM`
  - *Code context*: `'QVector<QVector<float>> bandData( nBands ); ... for ( int b = 0; b < nBands; ++b ) { bandData[b].resize( nPixels ); CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h, bandData[b].data(), w, h, GDT_Float32, 0, 0 ); ... }' (43-61) reads and retains ALL bands simultaneously although the GLCM/statistics kernel is strictly per-band, and the OBIA main window has no band selection: 'mSegStats = RsSegmentFeatures::extract( mRasterPath, mSegMap, allBandIndices() );' (rs_obia_main_window.cpp:957; allBandIndices() at 935-940 appends every band; hierarchy path 1070/1133 same). Complexity: memory O(W*H*B*4) + GLCM O(B * sum-of-segment-bbox-areas).`
- **SHELLB-4**: confirmed
  - *Callchain*: `Segment button -> runSegmentation -> segCfg{spatialRadius=5(default), rangeRadius=15(default)} -> RsObiaSegmentation::run -> runOtb -> RsOtbSegmenter with hardcoded radii`
  - *Code context*: `runSegmentation sets only `segCfg.smoothKernel/quantizeBins/minRegionSize` from kernelSpin/binsSpin/minRegionSpin; spatialRadius and rangeRadius keep their struct defaults (5 / 15, rs_obia_segmentation.h:18-19). With OTB available (preferOtb=true, the default: RsObiaSegmentation::isOtbAvailable), runOtb uses cfg.spatialRadius/cfg.rangeRadius for the MeanShift spec and only minRegionSize from the UI — so kernelSpin (tooltip: "平滑核大小 3–21。越大对象边界越粗…", presented as a live parameter) and binsSpin have zero effect on the default path. runHierarchicalSegmentation on the same two spinboxes DOES map them (spatialRadius=kernelSpin, rangeRadius=binsSpin*0.5), so identical UI controls produce different behavior between the two adjacent buttons — a copy-paste divergence between sibling branches.`

### Suggested fix
- Correct the mathematical formulas, coordinate system transforms, and NoData boundary conditions to align with standard GIS conventions (Horn 1981, GDAL, IEEE-754).

### References / Dedupe
- RSHYP-4 cited #201 as precedent; no prior GLCM/RSHYP-5 issues.
- Finding RSHYP-3: #230 covers the rs:segment_stats OPERATOR including NoData in means and explicitly names RsSegmentFeatures as the correct reference - the GLCM defect in RsSegmentFeatures itself is new. No closed issue mentions GLCM.
- Finding RSHYP-4: #201 fixed the kmeans reservoir gate only (src/processing); this occurrence in the analysis layer is unfixed. Not covered by any other closed issue.
- Finding RSHYP-5: #204 fixed continuum removal only (per focus note); no closed issue covers RsSegmentFeatures memory. New finding.
- Finding SHELLB-4: Not covered by closed issues; #240 is about MCP catalog categories, unrelated.


---

# #357: [P3][build/portability] Build/runtime minors batch: installed data/styles unresolved; dev-run plugins path; orphan sample-data tool+mislabel; AppImage missing Qt plugin/GDAL data; dead build seams; WITH_VECTORTILE=OFF breaks; dead HAVE_QT6KEYCHAIN; unloadAll signal-after-unload

### Area
Build System / Packaging / Installation

### Type
build/portability

### Severity
P3

### Affected code
- `src/app/CMakeLists.txt:344-378` (install(resources) / AppPaths::resolveDataPath)
- `src/app/main_window.cpp:154` (PluginHost::loadPlugins)
- `tools/generate_sample_data.cpp:294-330` (ROIDef rois[])
- `packaging/build-appimage.sh:70-86` (linuxdeploy invocation)
- `cmake/DownloadGdalTools.cmake:1-33` ((module includes))
- `src/core/CMakeLists.txt:1026-1045 vs 2300-2309` (QGIS_CORE_SRCS / WITH_VECTORTILE)
- `src/core/CMakeLists.txt:2792-2798` (HAVE_QT6KEYCHAIN / qgis_core target_link_libraries(Qt6Keychain::Qt6Keychain))
- `src/core/plugin_host.cpp:68-113, 157-174` (PluginHost::unloadAll / PluginHost::loadPlugin)

### User impact
- Functional degradation of every installed deployment; invisible to CI because the install smoke test only runs `sicnu_geo_rs_cli --help`.
- The 'Plugin architecture' feature is dead in the primary developer workflow (only works from an installed tree where bin/../plugins exists).
- The guided-lab workflows advertised by guided_workflow_widget.cpp:803 ('Pipeline JSON labs: data/pipelines/obia_*.json') cannot run as shipped, and would train mislabeled classifiers if the data were generated.
- The shipped packaging script cannot produce a working self-contained AppImage on a clean machine.
- Misleading build surface; the OTB bundle/lib staging expectations in the scripts have no producing targets, so those code paths operate on empty globs.
- Build-config portability only; no runtime effect with the default ON.
- Build-config dead code; no runtime effect where Qt6Keychain exists.
- Latent UAF/double-init in the plugin facade used by the app shell and CLI; low present-day reachability.

### Reproduction
- **Trigger (BUILD-8)**: Running the installed (non-source-tree) application built by the documented install flow
- **Trigger (BUILD-9)**: Every run from the build tree (the README 'Build' flow `./sicnu_geo_rs`)
- **Trigger (BUILD-10)**: Anyone trying to materialize the sample data referenced by the shipped teaching pipelines
- **Trigger (BUILD-11)**: Running packaging/build-appimage.sh
- **Trigger (BUILD-12)**: Anyone relying on these modules' documented behavior (installed GDAL CLI tools, staged OTB bundle target, svm/muparserx libs in the build tree)
- **Trigger (VPATCH-5)**: Configure/build with WITH_VECTORTILE=OFF.
- **Trigger (VPATCH-6)**: Configure without Qt6Keychain present.
- **Trigger (VPATCH-8)**: Requires a consumer of pluginUnloaded/plugin() during teardown (none exists at HEAD — only pluginLoaded/pluginError lambdas in src/gui/main_window.cpp dead code and app-side logging), or loading two plugins reporting the same name().

### Expected behavior
- **Expected (BUILD-8)**: Install rules for data/ plus install-layout candidates in resolveRuntimeDataPath (mirroring the fixed worker_daemon.py seam)
- **Expected (BUILD-9)**: LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins on the plugin targets (matching the ../plugins probe and the install DESTINATION 'plugins')
- **Expected (BUILD-10)**: A build target/script that generates data/samples, with ROI boxes overlapping their intended land-cover regions
- **Expected (BUILD-11)**: --plugin qt (or explicit Qt plugin deployment), bundled proj.db/GDAL data plus AppRun exports, and a complete lib set
- **Expected (BUILD-12)**: Either wired in or removed
- **Expected (VPATCH-5)**: Source list also guarded (or the option removed).
- **Expected (VPATCH-6)**: If keychain is meant to be optional, its uses in qgsauthmanager need #ifdef HAVE_QT6KEYCHAIN guards.
- **Expected (VPATCH-8)**: Mark entries unloaded (or erase) before emitting; reject/handle duplicate plugin names explicitly (pluginError signal exists for this).

### Actual behavior
- **Actual (BUILD-8)**: Installed app silently loses theming/fonts and the shipped custom-tool catalog and sample pipelines
- **Actual (BUILD-9)**: Both shipped C++ plugins and the sample Python plugin are silently absent in dev runs
- **Actual (BUILD-10)**: No generator is reachable from the build; the only candidate tool produces mislabeled training data
- **Actual (BUILD-11)**: AppImage lacks Qt plugins, QML, and PROJ/GDAL data, and inherits the missing libsicnu_agent
- **Actual (BUILD-12)**: Silent no-ops that make the build system advertise capabilities it does not deliver
- **Actual (VPATCH-5)**: Option exists but OFF path fails to build — dead/broken configuration knob.
- **Actual (VPATCH-6)**: Define is emitted but never read; the build still hard-requires Qt6Keychain. Misleading no-op guard.
- **Actual (VPATCH-8)**: Dangling-instance window during emit; silent shadowing on duplicate names.

### Root cause
- **BUILD-8**: confirmed
  - *Callchain*: `cmake --install → <prefix>/bin/sicnu_geo_rs → resolveDataPath("resources/styles.qss") walks up from bin/, finds no CMakeLists.txt/data → '/resources/styles.qss' → QFile::open fails → default Qt look, no fonts, empty generic-CLI tool set`
  - *Code context*: `src/app/CMakeLists.txt:345-354 installs resources/fonts, resources/icons and styles.qss(+dark) to share/sicnu_geo_rs/resources, and src/python/isolated/CMakeLists.txt:39-42 installs worker scripts — but there is no install rule for the data/ subtree (data/tools/custom, data/pipelines, data/processing/toolbox_manifest.json, data/plugins/sample_plugin). Runtime resolution for all of these goes through AppPaths::resolveDataPath (src/app/app_paths.h:35-38) → resolveRuntimeDataPath walk-up, which has no install-prefix candidate — unlike AppPaths::qgisRefResourcesDir()/samplesDataDir() (app_paths.h:52-93) which do probe ../share/sicnu_geo_rs/... layouts. main.cpp:235-242 loads fonts/styles.qss through this path and logs 'Could not load theme' when missing; the runtime generic-CLI provider loads shipped tools via resolveRuntimeDataPath("data/tools/custom") (src/processing/providers/generic_cli/provider.cpp:40, src/processing/tools/cli_tool_discovery.cpp:144).`
- **BUILD-9**: confirmed
  - *Callchain*: `dev run ./sicnu_geo_rs from build dir → QDir(<build>/plugins).exists() false → qWarning → layer-tree and processing C++ plugins never loaded, no Python plugins discovered`
  - *Code context*: `main_window.cpp:154 `m_pluginHost->loadPlugins(QCoreApplication::applicationDirPath() + "/../plugins")`. The exe is at <build>/sicnu_geo_rs, so it probes <build>/plugins. The plugin targets (src/plugins/layer_tree/CMakeLists.txt:1, src/plugins/processing/CMakeLists.txt:1) set no LIBRARY_OUTPUT_DIRECTORY, so Ninja places them at <build>/src/plugins/layer_tree/liblayer_tree_plugin.so and <build>/src/plugins/processing/libprocessing_plugin.so (verified in the audit build). PluginHost::loadPlugins (src/core/plugin_host.cpp:41-48) just qWarning's 'Plugin directory not found' and returns; data/plugins/sample_plugin is likewise never staged into <build>/plugins.`
- **BUILD-10**: confirmed
  - *Callchain*: `user compiles the tool by hand → training_samples.shp with Water=vegetation, BareSoil=Urban, Forest=Urban pixels → rs:supervised_classification / rs:obia_classify labs train on wrong labels`
  - *Code context*: `No CMakeLists, script, or doc references tools/generate_sample_data.cpp (repo-wide grep), and data/samples/ does not exist — yet 8 of the 10 shipped data/pipelines JSONs reference data/samples/landsat_sample.tif and data/samples/training_samples.shp. If compiled manually, the ROI geometry is wrong: geotransform is (116.0,40.0) with 0.001 px over 256 px, so e.g. the 'Water' ROI at lat 39.87-39.90 maps to rows 100-130 (ny 0.39-0.51) while getLandCoverClass puts Water at ny>0.85 → it samples vegetation; 'Bare Soil' ROI (nx 0.195-0.273, ny 0.117-0.273) hits the Urban rule (nx<0.4 && ny<0.3); 'Forest' ROI (nx 0.273-0.344, ny 0.078-0.234) also hits Urban (forest requires ny>0.3).`
- **BUILD-11**: confirmed
  - *Callchain*: `build-appimage.sh → make install (missing sicnu_agent) → linuxdeploy (no qt plugin, no geodata) → AppImage that cannot start (loader error) and, once that is fixed, cannot create a platform plugin or transform CRS without host files`
  - *Code context*: `Lines 79-83 invoke linuxdeploy with only --appdir/--desktop-file/--icon-file/--output appimage — no `--plugin qt`. linuxdeploy's default dependency resolution walks DT_NEEDED only; Qt platform plugins (libqxcb/libqoffscreen), imageformats, and QML module files are dlopen'ed and never referenced, so none are bundled (the app links Qt6QuickWidgets/Qml per readelf). Nothing bundles or env-sets PROJ data (proj.db) or GDAL_DATA either — the only data copied is resources/fonts|icons|styles.qss and symbology-style.xml (lines 24-38); there is no AppRun hook (linuxdeploy's generated AppRun sets no GDAL_DATA/PROJ_LIB/QT_PLUGIN_PATH). The script also inherits BUILD-1 via `make install DESTDIR="$APPDIR"` (line 21): libsicnu_agent.so is not in the AppDir, so linuxdeploy's ldd resolution of usr/bin/sicnu_geo_rs fails.`
- **BUILD-12**: confirmed
  - *Callchain*: `n/a (never executed)`
  - *Code context*: `Repo-wide grep for include()/add_subdirectory() consumers: cmake/DownloadGdalTools.cmake and cmake/DownloadOtbTools.cmake (which promise installed tools/gdal + tools/otb) are included by no CMakeLists; images/CMakeLists.txt and its icons/svg/themes subdirs are never add_subdirectory'd from the root (and reference undefined WITH_DESKTOP/QGIS_DATA_DIR); cmake/sicnu_otb_bundle.cmake's sicnu_setup_otb_bundle() has no caller (the bundling is done ad hoc by scripts/build_with_otb.sh:36-40); cmake/LibSVM/CMakeLists.txt and cmake/MuParserX/CMakeLists.txt define targets nothing adds (yet bundle_otb_tools.sh:17-24 expects libmuparserx.so*/libsvm.so* in <build>/lib, which nothing produces); cmake/VcpkgToolchain.cmake and cmake/VcpkgInstallDeps.cmake are unreferenced (Windows CI uses the preinstalled vcpkg toolchain directly).`
- **VPATCH-5**: confirmed
  - *Callchain*: `cmake -DWITH_VECTORTILE=OFF -> protobuf_generate_cpp skipped -> vectortile sources still compiled -> fatal error: vector_tile.pb.h: No such file.`
  - *Code context*: `Root CMakeLists.txt:175 'option(WITH_VECTORTILE "" ON)'. Local delta wraps only 'protobuf_generate_cpp(...)' + 'add_definitions(-DWITH_VECTORTILE)' in if(WITH_VECTORTILE), but the ~20 'vectortile/*.cpp' entries remain unconditional in QGIS_CORE_SRCS and vectortile/qgsvectortilemvtencoder.h:23 and qgsvectortilemvtdecoder.h:24 '#include "vector_tile.pb.h"' unconditionally — with the option OFF the generated header does not exist, so compilation fails. The code-level guards (#ifdef WITH_VECTORTILE in qgsproviderregistry.cpp:39/207, qgspallabeling.cpp:52/4441) show the OFF configuration was intended to be supported.`
- **VPATCH-6**: confirmed
  - *Callchain*: `Build on a system without Qt6Keychain -> link target skipped and define added -> qgsauthmanager.o references QKeychain:: symbols with no library -> link failure.`
  - *Code context*: `Local delta: 'if(TARGET Qt6Keychain::Qt6Keychain) target_link_libraries(qgis_core Qt6Keychain::Qt6Keychain) target_compile_definitions(qgis_core PRIVATE "HAVE_QT6KEYCHAIN") endif()'. Repo-wide grep finds ZERO #ifdef HAVE_QT6KEYCHAIN consumers; src/core/auth/qgsauthmanager.h:42 '#include <qt6keychain/keychain.h>' and qgsauthmanager.cpp:3631-3913 use QKeychain:: symbols unconditionally.`
- **VPATCH-8**: confirmed
  - *Callchain*: `PluginHost::~PluginHost -> unloadAll -> emit pluginUnloaded -> (future consumer) -> PluginHost::plugin(name) -> dangling SicnuPluginInterface*.`
  - *Code context*: `unloadAll(): 'it.value().instance->unload(); if (it.value().loader) it.value().loader->unload(); emit pluginUnloaded(it.key());' — loader->unload() destroys the plugin's root object, but the entry stays in m_plugins with loaded==true and a dangling instance until the loop finishes; any slot connected to pluginUnloaded that calls pluginHost()->plugin(name) receives the freed pointer for a virtual call. loadPlugin() has no isPluginLoaded check: 'm_plugins[interface->name()] = info;' silently orphans a previously loaded same-name plugin's loader and instance (double initialize, orphaned QPluginLoader child).`

### Suggested fix
- Update CMakeLists.txt install rules, dependency discovery, and RPATH configuration to ensure correct runtime linking and packaging.

### References / Dedupe
- BUILD-8 shares the #232 walk-up family (install side of worker_daemon only); not a duplicate.
- Finding BUILD-8: #232 covered worker_daemon.py only (verified fixed at HEAD: install rule + matching probe path); #180 covered sicnu_core. This covers resources/data/ payloads.
- Finding BUILD-9: No closed issue covers the build-tree plugin output directory.
- Finding BUILD-10: No closed issue covers sample-data generation.
- Finding BUILD-11: No closed issue covers AppImage packaging.
- Finding BUILD-12: #234's batch did not cover these modules.
- Finding VPATCH-5: #234 build/portability batch does not mention WITH_VECTORTILE.
- Finding VPATCH-6: #234 'install_deps.sh missing GSL' etc. does not cover the keychain define.
- Finding VPATCH-8: #233 covers Python adapter/AppInterfaceBridge lifetime, not PluginHost C++ unload ordering; #234 mentions a PluginHost name-collision LEAK in build context — this is the runtime duplicate-name behavior plus emit-order UAF window, distinct and latent.


---

# #359: [P3][build/portability] Uncompiled first-party files carry latent defects (mosaic_panel m_pendingTaskId, 4 orphan widgets, legacy vendored SicnuMainWindow/python console) - #224 class

### Area
Build System / Packaging / Installation

### Type
build/portability

### Severity
P3

### Affected code
- `src/app/panels/mosaic_panel.cpp:149 (`if ( m_pendingTaskId >= 0 ) return;`); header src/app/panels/mosaic_panel.h:60-68 declares no such member` (MosaicPanel::runMosaic)
- `src/app/widgets/roi_statistics_widget.cpp:127-137 (xOff/yOff from bbox map coordinates); dead siblings: src/app/widgets/multi_temporal_widget.cpp, src/app/widgets/cross_section_widget.cpp, src/app/widgets/annotation_widget.cpp` (RoiStatisticsWidget::computeStatistics)
- `src/gui/main_window.cpp:1-647; python_console_widget.cpp:58-80` (SicnuMainWindow / PythonConsoleWidget)

### User impact
- Same class as closed #224 (files never compiled); latent compile break for whoever resurrects the panel
- Rot risk and #224-class coverage gap; latent incorrect statistics
- Hygiene/rot risk; a future 're-enable' would regress #237 and ship the double-add layer bug. No runtime exposure at HEAD.

### Reproduction
- **Trigger (SHELLB-7)**: n/a (any attempt to build or reuse the panel)
- **Trigger (SHELLB-8)**: n/a today; becomes user-facing garbage stats if the file is ever added to a target
- **Trigger (VPATCH-7)**: None at HEAD; any future re-wiring of these files into a target.

### Expected behavior
- **Expected (SHELLB-7)**: Dead files either compiled or at least compilable
- **Expected (SHELLB-8)**: Dead widgets removed or maintained; ROI stats use geotransform-inverse mapping (compare cross_section_widget.cpp:53-56 which does it correctly)
- **Expected (VPATCH-7)**: Legacy files either deleted or kept in sync with the fixes that landed elsewhere.

### Actual behavior
- **Actual (SHELLB-7)**: Silently excluded from the build and broken if included
- **Actual (SHELLB-8)**: Four orphaned files, one with a coordinate-space bug and one no-op stub
- **Actual (VPATCH-7)**: Unrotted-in-place copies that silently diverge from the fixed live implementations.

### Root cause
- **SHELLB-7**: confirmed (latent-only, uncompiled)
  - *Callchain*: `n/a (file not compiled)`
  - *Code context*: ``grep -rn mosaic_panel src --include=CMakeLists.txt` finds no reference — the file is dead (MosaicDialog is the live path per the in-file comment). Line 149 reads member `m_pendingTaskId` which is not declared in mosaic_panel.h (members end at line 68 with `sicnu::app::GuiJobHandle m_jobHandle{this};`), so adding the file back to any target would be a compile error. The intended busy-guard is presumably the run-button disablement plus GuiJobHandle::isRunning().`
- **SHELLB-8**: confirmed (latent-only, uncompiled)
  - *Callchain*: `n/a (not compiled); latent: computeStatistics -> GDALRasterIO with xOff=bbox.xMinimum()`
  - *Code context*: `src/app/CMakeLists.txt lists only histogram_widget, histogram_stretch_widget, band_composition_rail, rs_toolbar_flow_host, spectral_profile_widget, band_role_combo, raster_layer_combo, resolution_widget, crs_selector, progress_dialog, comparison_widget, guided_workflow_widget (+python_script_editor under a flag) — roi_statistics/multi_temporal/cross_section/annotation are absent from every CMakeLists. Inside roi_statistics_widget.cpp the read window is computed as `int xOff = std::max(0, static_cast<int>(bbox.xMinimum())); int yOff = std::max(0, static_cast<int>(bbox.yMinimum())); int xSize = std::min(m_rasterLayer->width() - xOff, static_cast<int>(bbox.width()) + 1); ...` treating layer-CRS map coordinates (e.g. UTM ~500000) as pixel offsets: for any georeferenced raster xSize/ySize go negative, every band is skipped by `continue`, and updateTable() then renders the zero-initialized BandStats (all 0.0 / 0 pixels) while the summary claims success. annotation_widget.cpp is a log-only stub (onAddText just SICNU_LOG_INFO; Clear All clears nothing).`
- **VPATCH-7**: confirmed (latent-only, uncompiled)
  - *Callchain*: `N/A (dead code; zero instantiation sites — only a forward declaration in src/plugins/layer_tree/layer_tree_plugin.h:7).`
  - *Code context*: `No CMakeLists references either file (grep over all CMake/txt) and no source includes their headers; the real shell is src/app/main_window.cpp and the real console src/python/sicnu_python_console.cpp (src/app/CMakeLists.txt:305). If ever recompiled they reintroduce defects: SicnuMainWindow::loadRasterLayer adds every layer twice ('QgsProject::instance()->addMapLayer(layer)' with addToLegend=true followed by 'group->addLayer(layer)'); m_layerTree (QgsLayerTreeView) is never created so selectedLayers() always returns empty and Layer-menu actions are no-ops; PythonConsoleWidget::executeCommand runs arbitrary Python synchronously on the GUI thread via QgisPython::runString — exactly the defect class #237 fixed in the live console.`

### Suggested fix
- Update CMakeLists.txt install rules, dependency discovery, and RPATH configuration to ensure correct runtime linking and packaging.

### References / Dedupe
- VPATCH-7 references #237 (console) and the whole batch is the #224 class; no dup of an open issue.
- Finding SHELLB-7: #224 covered five TEST files; this is a src/app panel, not listed there.
- Finding SHELLB-8: #224 covered never-compiled TEST files only.
- Finding VPATCH-7: #237 fixed the live sicnu_python_console; nothing tracks these dead copies. #236 layer-menu UAF is a different file.


---

