# Research: OBIA & OTB segmentation seams

**Ticket:** GitHub issue #8 — Inventory OBIA / OTB segmentation seams  
**Primary files:**

| File / tree | Role |
|-------------|------|
| [`src/app/obia/`](../../../src/app/obia/) | GUI workspace: window, task, shared OTB/simple segment path, select tool, info dock |
| [`src/analysis/segmentation/`](../../../src/analysis/segmentation/) | Label map model, features, teaching segmenter (library) |
| [`src/operators/otb/otb_segmentation_operator.{h,cpp}`](../../../src/operators/otb/otb_segmentation_operator.h) | RSOperator `otb:meanshift_segmentation` (filter + mode) |
| [`src/processing/providers/otb_tools/algorithms/otb_segmentation.{h,cpp}`](../../../src/processing/providers/otb_tools/algorithms/otb_segmentation.h) | Processing toolbox `otb_tools:otb_segmentation` |
| [`src/operators/rs/rs_obia_*.{h,cpp}`](../../../src/operators/rs/rs_obia_segment_operator.h) | Pipeline ops `rs:obia_segment` / `rs:obia_classify` |
| [`src/operators/rs/rs_segmentation_utils.{h,cpp}`](../../../src/operators/rs/rs_segmentation_utils.h) | OpenCV segutil shared by RS ops (not by GUI task) |
| [`src/operators/rs/rs_segment_stats_operator.{h,cpp}`](../../../src/operators/rs/rs_segment_stats_operator.h) | `rs:segment_stats` CSV exporter |
| [`src/workflow/builtin_definitions.cpp`](../../../src/workflow/builtin_definitions.cpp) | `lab.obia` workflow stub |
| Pipelines | [`data/pipelines/obia_*.json`](../../../data/pipelines/) |

**Scope:** inventory of existing **seams** (method/member clusters, operators, data models). Not a redesign.

**Vocabulary (light):**

- **Module** — a method/member cluster with a coherent responsibility.
- **Interface** — the thin public surface other clusters already call.
- **Seam** — a place where those modules already separate, even if co-located.
- **Leakage** — shared mutable state written/read across seams without a single owner.
- **Stack** — a parallel implementation path that does not share code with another stack (same product concept, different types/APIs).

---

## Executive summary

OBIA / segmentation in this repo is **not one pipeline**. Three largely independent stacks produce or consume “objects”:

1. **GUI / analysis stack** — `RsObiaMainWindow` → `RsObiaSegmentation` → `RsSegmentMap` / `RsSegmentFeatures` / `RsSimpleSegmenter` → `RsObiaTask` (classifiers from `src/analysis/classification/`).
2. **RS operator stack** — `rs:obia_segment` / `rs:obia_classify` / `rs:segment_stats` over OpenCV `segutil` (`cv::Mat` labels), registered only when `SICNU_HAS_OPENCV`.
3. **OTB CLI wrappers** — two different surfaces:
   - Processing algorithm `otb_tools:otb_segmentation` (`OtbSegmentationAlgorithm`)
   - RSOperator `otb:meanshift_segmentation` (`OtbSegmentationOperator`)

The GUI OTB path (`RsObiaSegmentation::runOtb`) is a **third** CLI invocation style (not shared with either OTB wrapper class). It only runs MeanShift and loads a temporary label GeoTIFF into `RsSegmentMap`.

**Data model today:** a single flat `uint32` label image (`RsSegmentMap` or equivalent `cv::Mat` / GeoTIFF). **No multi-level hierarchy** (no parent/child segments, no level stack, no object graph).

**Worst leakage / divergence (inventory only):**

- GUI stack types (`RsSegmentMap`, `RsSegmentFeatures`) vs operator stack types (`cv::Mat` + `segutil`) — dual teaching segmenters.
- Three OTB argument dialects (`-mode meanshift` vs `-filter` + `-mode vector|raster` vs processing enum).
- `lab.obia` workflow points at `rs:obia_*` operators while the real UI runs `RsObiaSegmentation` / `RsObiaTask` via JobEngine algorithm ids `module:obia:segment` / `module:obia:classify`.
- `RsObiaTask::Config::threshold` is never passed into `RsObiaSegmentation` / OTB CLI.

---

## 1. File map (absolute paths under repo)

### 1.1 App OBIA (`src/app/obia/`)

| Path | ~LOC | Role |
|------|------|------|
| `src/app/obia/rs_obia_main_window.{h,cpp}` | 105 + 701 | Standalone QMainWindow shell |
| `src/app/obia/rs_obia_segmentation.{h,cpp}` | 42 + 227 | Shared OTB CLI / SimpleSegmenter entry |
| `src/app/obia/rs_obia_task.{h,cpp}` | 92 + 386 | QgsTask full classify pipeline |
| `src/app/obia/rs_segment_select_tool.{h,cpp}` | 54 + 89 | Click → segment id + rubber band |
| `src/app/obia/rs_segment_info_dock.{h,cpp}` | 29 + 47 | Bottom dock: segment stats text |
| `src/app/obia/CMakeLists.txt` | — | `qgis_app_obia` (gated on OpenCV) |

Host entry: `src/app/main_window_view.cpp` opens `RsObiaMainWindow` when `SICNU_HAS_OBIA`; pointer `m_obiaWindow` on main window.

### 1.2 Analysis segmentation (`src/analysis/segmentation/`)

| Path | ~LOC | Role |
|------|------|------|
| `src/analysis/segmentation/rs_segment_map.{h,cpp}` | 65 + 148 | Flat label image model |
| `src/analysis/segmentation/rs_segment_features.{h,cpp}` | 50 + 244 | Per-segment spectral + shape features |
| `src/analysis/segmentation/rs_simple_segmenter.{h,cpp}` | 51 + 377 | Teaching segmenter (smooth → quantize → CC → merge) |
| `src/analysis/segmentation/CMakeLists.txt` | — | Into `qgis_analysis` |

### 1.3 Operators & processing

| Path | Operator / algorithm id | Role |
|------|-------------------------|------|
| `src/operators/rs/rs_obia_segment_operator.{h,cpp}` | `rs:obia_segment` | Teaching segment → UInt32 GeoTIFF |
| `src/operators/rs/rs_obia_classify_operator.{h,cpp}` | `rs:obia_classify` | Segment + ROI majority + ML → Byte class map |
| `src/operators/rs/rs_segment_stats_operator.{h,cpp}` | `rs:segment_stats` | Labels + image → CSV means/area |
| `src/operators/rs/rs_segmentation_utils.{h,cpp}` | (internal `segutil`) | grid / quantize / merge / write helpers |
| `src/operators/rs/rs_operators_init.cpp` | registration | OpenCV-gated register of three ops |
| `src/operators/otb/otb_segmentation_operator.{h,cpp}` | `otb:meanshift_segmentation` | OTB app `Segmentation` via RSOperator |
| `src/operators/otb/otb_operators_init.cpp` | registration | Registers OTB ops |
| `src/processing/providers/otb_tools/algorithms/otb_segmentation.{h,cpp}` | `otb_tools:otb_segmentation` | Processing toolbox wrapper |
| `data/processing/toolbox_manifest.json` | `otb_rs` required | Lists `otb_tools:otb_segmentation` |

### 1.4 Pipelines, workflow, tests

| Path | Role |
|------|------|
| `data/pipelines/obia_segment.json` | Lab: `rs:obia_segment` |
| `data/pipelines/obia_classify.json` | Lab: `rs:obia_classify` (grid default) |
| `data/pipelines/obia_export.json` | Segment → stats → `gdal:polygonize` |
| `src/workflow/builtin_definitions.cpp` | `registerObia` → `lab.obia` |
| `tests/test_obia_segmentation.cpp` | `RsObiaSegmentation` |
| `tests/test_obia_task.cpp` | `RsObiaTask` |
| `tests/test_otb_segmentation_params.cpp` | Processing `OtbSegmentationAlgorithm` params/args |
| `tests/test_otb_operators.cpp` | `OtbSegmentationOperator` schema/exec |
| `tests/test_rs_operators.cpp` | `rs:obia_*` presence / segment run |
| `tests/test_workflow_runtime.cpp` | `lab.obia` step operator ids |

---

## 2. Cluster map — `RsObiaMainWindow`

Shell is smaller than classify mainwindow (~700 lines). Clusters already separate as method groups + members.

### 2.1 Shell setup

**Intent:** Window chrome, toolbar param widgets, docks, canvas.

| Kind | Symbols | Location (cpp) |
|------|---------|----------------|
| Ctor | ctor seeds `mClassDefs`, calls setup* | ~66–80 |
| Setup | `setupUi`, `setupToolbar`, `setupDocks`, `setupMapCanvas` | ~88–223 |
| Status | `updateStatusLabel`, `updateSegmentTable` | ~672–701 |

**Members:** `mCanvas`, `mLayerTree*`, `mToolbar`, docks/tables, `mClassDefs`, `mCurrentClassId`.

**Note:** Layer tree is constructed but not heavily used as a full session workspace (no `addSessionLayer` API like classify). Canvas layers are set directly from `mRasterLayer` / result layer add to `QgsProject`.

**Seam quality:** clean setup boundary; toolbar widgets are anonymous children found later via `findChild` (objectName `kernelSpin`, `binsSpin`, `minRegionSpin`, `classifierCombo`).

---

### 2.2 Source raster lifecycle

**Intent:** Load input image; reset segmentation state.

| Kind | Symbols | Location |
|------|---------|----------|
| Slot | `loadRaster` | cpp ~229–265 |

**Members:** `mRasterLayer`, `mRasterPath`, `mBandCount`.

**On load:** clears `mSegMap`, `mSegStats`, `mSegmentLabels`.

**Natural interface:** other clusters read `mRasterPath` / `mBandCount` only.

---

### 2.3 Segmentation job (OTB / simple)

**Intent:** Run shared segmenter + feature extract off UI thread; apply result.

| Kind | Symbols | Location |
|------|---------|----------|
| Slot | `runSegmentation` | cpp ~267–413 |
| Apply | `applySegmentationResult` | cpp ~415–455 |

**Flow:**

1. Build `RsObiaSegmentationConfig` (`preferOtb = true`; toolbar smooth/quantize/minRegion; all bands 1..N).
2. Submit exclusive JobEngine job `module:obia:segment`.
3. Worker: `RsObiaSegmentation::run` → if ok, `RsSegmentFeatures::extract`.
4. UI finish: `applySegmentationResult` stores map/stats, installs `RsSegmentSelectTool`, refreshes table.

**Members written:** `mSegMap`, `mSegStats`, `mSegmentLabels` (cleared), `mSelectTool`.

**Does not use:** `rs:obia_segment` operator or processing `otb_segmentation`.

---

### 2.4 Segment selection & labeling

**Intent:** Interactive object labeling for training.

| Kind | Symbols | Location |
|------|---------|----------|
| Select tool | `RsSegmentSelectTool` | separate files |
| Info dock | `RsSegmentInfoDock` | separate files |
| Slots | `onSegmentSelected`, `onSelectionCleared`, `onAssignClass` | cpp ~626–666 |

**Members:** `mSelectTool`, `mInfoDock`, `mSegmentLabels` (segmentId → classId), `mSegmentTable`, `mCurrentClassId` / class table.

**Interface:**

- Tool → window: `segmentSelected(quint32)` / `selectionCleared()`
- Assign: selected segment id + `mCurrentClassId` → `mSegmentLabels`

---

### 2.5 Classification job

**Intent:** Train object classifier from labeled segments; write class raster.

| Kind | Symbols | Location |
|------|---------|----------|
| Slot | `runClassification` | cpp ~457–609 |

**Flow:**

1. Require non-empty `mSegMap` / `mSegStats` and ≥2 labeled segments.
2. Build `RsClassifierBackend` (NormalBayes / SVM / KMeans from combo).
3. Build `RsObiaTask::Config` with **`existingSegMap` + `existingStats`** (skip re-segment).
4. JobEngine `module:obia:classify` runs `task->run()`; on success add result layer to project.

**Export slot** (`exportResult`, ~611–624) is a stub message pointing users at Classify save path — not a real exporter.

---

### 2.6 Member ownership sketch (window)

| Member | Owner cluster | Readers |
|--------|---------------|---------|
| `mRasterPath` / `mRasterLayer` / `mBandCount` | Source raster | Segment, classify |
| `mSegMap` | Segmentation apply | Select tool, table, classify config |
| `mSegStats` | Segmentation apply | Info dock, classify config |
| `mSegmentLabels` | Labeling | Table, classify config |
| `mClassDefs` / `mCurrentClassId` | Shell / classes | Assign, classify colors |
| `mSelectTool` | Selection | Assign, apply installs map |

---

## 3. Shared segmentation helper — `RsObiaSegmentation`

**Files:** `src/app/obia/rs_obia_segmentation.{h,cpp}`

### Config / result

```
RsObiaSegmentationConfig
  rasterPath, bandIndices (1-based)
  preferOtb = true
  spatialRadius, rangeRadius, minRegionSize, maxIteration   // OTB MeanShift
  smoothKernel, quantizeBins                                // SimpleSegmenter

RsObiaSegmentationResult
  ok, usedOtb, errorMessage, RsSegmentMap segMap
```

### Public surface

| Method | Behavior |
|--------|----------|
| `isOtbAvailable()` | `ToolPathManager::otbToolPath("Segmentation")` non-empty |
| `run(cfg, isCanceled)` | If preferOtb && available → `runOtb`; on failure **fallback** `runSimple` |

### `runOtb` (private)

- CLI: OTB `Segmentation`
- Args (dialect A — same family as processing algorithm, not RSOperator):
  - `-in <raster>`
  - `-mode meanshift`
  - `-mode.meanshift.spatialr|ranger|minsize|maxiter`
  - `-out <temp.shp> <temp/labels.tif> uint32`
- Loads labels via `RsSegmentMap::fromGeoTIFF`; size-checks against source.
- **Only MeanShift**; no cc/watershed/mprofiles/lsms.
- Temp dir destroyed after load (vector output discarded for GUI path).

### `runSimple` (private)

- GDAL read selected bands → `RsSimpleSegmenter::segmentMultiBand`.
- Params: smoothKernel, quantizeBins, minRegionSize.

**Callers:** `RsObiaMainWindow::runSegmentation`, `RsObiaTask::run` (when no `existingSegMap`).

---

## 4. Analysis data models & simple segmenter

### 4.1 `RsSegmentMap` — single-layer label map

**Intent:** In-memory `uint32` label image (row-major). Segment id `0` = nodata/background.

| API | Notes |
|-----|-------|
| ctor `(labels, w, h)` | Takes ownership of buffer |
| `fromGeoTIFF(path)` | Reads band1 as float → round to quint32 |
| `labelAt`, `labels()` | Pixel access |
| `uniqueLabels`, `segmentCount` | Via size cache |
| `pixelCount`, `pixelCoords` | Size cache always; coords **lazy** per segment |

**Caches:** `mSizeCache` (built once), `mCoordsCache` (lazy). No hierarchy, no multi-band labels, no parent pointer.

**Hierarchy gap:** one flat label field only. Cannot represent multi-level OBIA (LSMS levels, nested objects, region adjacency graphs).

### 4.2 `RsSegmentFeatures::SegmentStat`

| Field | Meaning |
|-------|---------|
| `mean`, `stddev`, `min`, `max` | Per-band spectral |
| `area` | Pixel count |
| `perimeter` | Boundary pixel count |
| `shapeIndex` | `perimeter / (4 * sqrt(area))` |

| API | Notes |
|-----|-------|
| `extract(raster, segMap, bandIndices)` | Full-image GDAL read + accumulate |
| `toFeatureMatrix` (OpenCV) | Rows = segments; used by `RsObiaTask` |

Feature matrix layout is fixed by implementation (spectral + shape columns); classify task uses entire matrix for train/predict.

### 4.3 `RsSimpleSegmenter`

**Pipeline:** Gaussian smooth → quantize → 8-connected components → merge small regions.

| Entry | Input |
|-------|-------|
| `segment` | Single-band float buffer |
| `segmentMultiBand` | Mean of bands → single-band path |

**Params:** `smoothKernel`, `quantizeBins`, `minRegionSize` (default 50).

**Output:** `RsSegmentMap` (1-based labels, 0 background).

---

## 5. `RsObiaTask` — classify pipeline seams

**Files:** `src/app/obia/rs_obia_task.{h,cpp}`

### Config clusters

| Cluster | Fields | Used when |
|---------|--------|-----------|
| I/O | `sourceRaster`, `outputRaster`, `bandIndices` | Always |
| Segment (fresh) | `useOtb`, spatial/range/min/maxIteration, `threshold`, smooth/quantize | `existingSegMap` empty |
| Segment (reuse) | `existingSegMap` | From main window |
| Features (reuse) | `existingStats` | From main window |
| Train | `backend`, `segmentLabels`, `classColors`, `algoName` | Classify |

**Note:** `threshold` is on Config but **not** wired into `RsObiaSegmentationConfig` (never reaches CLI).

### `run()` steps

| Step | Progress | Action |
|------|----------|--------|
| 1 | 5–30 | Segment or copy `existingSegMap` |
| 2 | 30–50 | Features or copy `existingStats` |
| 3 | 50–60 | Build train rows from `segmentLabels` |
| 4 | 60–70 | `backend->fit` if not fitted |
| 5 | 70–85 | `predict` all segments |
| 6 | 85–100 | `writeOutput` class GeoTIFF (Byte or UInt16 + optional palette) |

**Output model:** pixel = class id of its segment (flat class map), not multi-level.

**Dependencies:** OpenCV (`cv::Mat`), `RsClassifierBackend`, GDAL write, `RsObiaSegmentation`, `RsSegmentFeatures`.

---

## 6. OTB Segmentation — two wrappers + GUI path

### 6.1 RSOperator `otb:meanshift_segmentation`

**Files:** `src/operators/otb/otb_segmentation_operator.{h,cpp}`  
**OTB app name:** `Segmentation`  
**Registration:** `REGISTER_RS_OPERATOR(..., "otb:meanshift_segmentation")`

| Param | Type / default | Purpose |
|-------|----------------|---------|
| `input` | raster, required | Source image |
| `filter` | enum: `meanshift` (def), `cc`, `watershed`, `mprofiles` | Algorithm |
| `outputMode` | enum: `vector` (def), `raster` | Output kind |
| `spatialRadius` | int 5 | MeanShift spatial |
| `rangeRadius` | double 15 | MeanShift spectral |
| `minRegionSize` | int 100 | MeanShift minsize |
| `maxIterations` | int 100 | MeanShift maxiter |
| `threshold` | double 0.1 | MeanShift thres **or** watershed threshold |
| `ccExpression` | string `(p1b1 > 0)` | CC condition |
| `output` | path, required | Vector or raster path |

**mprofiles extras (read in `buildOtbArgs`, not in schema defaults UI):** `profileSize` (5), `startRadius` (1), `radiusStep` (1), `sigma` (1.0).

**Arg dialect B:**

```
-in … -filter <f> -mode <vector|raster>
  + -filter.meanshift.spatialr|ranger|minsize|maxiter|thres
  + -filter.cc.expr …
  + -filter.watershed.threshold …
  + -filter.mprofiles.size|start|step|sigma
  + -mode.vector.out PATH  |  -mode.raster.out PATH uint32
```

**Outputs (schema):** `output`, `filter`, `outputMode`.

**Not used by** GUI OBIA window or `RsObiaTask`.

---

### 6.2 Processing algorithm `otb_tools:otb_segmentation`

**Files:** `src/processing/providers/otb_tools/algorithms/otb_segmentation.{h,cpp}`  
**Manifest:** `data/processing/toolbox_manifest.json` → group `otb_rs` required list.

| Param | Default | Notes |
|-------|---------|-------|
| `INPUT` | — | Raster layer |
| `MODE` | 0 = meanshift | Enum: meanshift, watershed, mprofiles, cc, **lsms** |
| `SPATIAL_RADIUS` | 5 | MeanShift / LSMS |
| `RANGE_RADIUS` | 15 | MeanShift / LSMS |
| `MIN_REGION_SIZE` | 100 | MeanShift / LSMS |
| `MAX_ITERATION` | 100 | MeanShift / LSMS |
| `THRESHOLD` | 0.1 | watershed / mprofiles / cc only (not meanshift) |
| `OUTPUT` | vector dest | Polygons |
| `OUTPUT_RASTER` | optional | Label image for OBIA |

**Arg dialect A (related to GUI `runOtb`):**

```
-in … -mode meanshift|watershed|mprofiles|cc|lsms
  + -mode.meanshift.spatialr|ranger|minsize|maxiter
  + -mode.lsms.…
  + -mode.<other>.threshold …
  + -out vector.shp [labels.tif uint32]
```

**Difference vs RSOperator:** uses `-mode` for algorithm (not `-filter`); includes **lsms**; dual `-out` vector+raster string; no separate vector/raster “mode” enum.

---

### 6.3 GUI / task OTB path (again)

Dialect A-like MeanShift only; always dual `-out` temp shp + labels; loads only labels into `RsSegmentMap`. Does not expose cc/watershed/mprofiles/lsms in toolbar.

---

### 6.4 Algorithm coverage matrix

| Algorithm | GUI `RsObiaSegmentation` | Processing `OtbSegmentationAlgorithm` | RSOperator `OtbSegmentationOperator` |
|-----------|--------------------------|----------------------------------------|--------------------------------------|
| meanshift | yes | yes | yes (`filter`) |
| cc | no | yes | yes |
| watershed | no | yes | yes |
| mprofiles | no | yes | yes (partial params) |
| lsms | no | yes | no |

---

## 7. RS pipelines: `rs:obia_segment` / `rs:obia_classify` / `rs:segment_stats`

These form the **operator stack** (OpenCV `segutil`), parallel to analysis `RsSimpleSegmenter` + `RsSegmentMap`.

### 7.1 `rs:obia_segment`

**Impl:** `RsObiaSegmentOperator` → `segutil::segmentQuantize` → `writeLabelGeoTiff`.

| Param | Default | Role |
|-------|---------|------|
| `input`, `output` | required | Paths |
| `smoothKernel` | 5 | Gaussian |
| `quantizeBins` | 32 | Levels |
| `minRegionSize` | 50 | Merge |
| `bands` | optional array | 1-based; mean intensity |

**Returns:** `output`, `segments` (max id), `width`, `height`.

**Does not call** `RsObiaSegmentation` or `RsSimpleSegmenter`.

### 7.2 `rs:obia_classify`

**Impl:** end-to-end in one operator (no `RsObiaTask`).

| Stage | Behavior |
|-------|----------|
| Segment | `segmentMethod`: **`grid`** (default, `cellSize`) or **`quantize`** (smooth/bins/min); quantize may fallback to grid if &lt;8 objects |
| Features | Per-segment **mean only** (not full `SegmentStat`) |
| Labels | Rasterize training polygons → majority vote per segment (`minLabelPixels`) |
| ML | OpenCV SVM or NormalBayes (not `RsClassifierBackend`) |
| Output | Byte class map GeoTIFF |

**Pipeline JSON** (`obia_classify.json`) uses grid superpixels + SVM — teaching path, not OTB MeanShift.

### 7.3 `rs:segment_stats`

Input image + label raster → CSV (`segment_id`, `area_pixels`, `mean_b*`). Companion to segment pipeline export lab.

### 7.4 `segutil` helpers

| Function | Role |
|----------|------|
| `segmentGrid` | Regular superpixels 1..N |
| `segmentQuantize` | Mean → smooth → quantize → CC → merge |
| `mergeSmallRegions` | Island merge |
| `rasterizeGeometry` | Training mask |
| `writeLabelGeoTiff` / `writeByteGeoTiff` | Outputs |

### 7.5 Lab pipelines

| File | Operators |
|------|-----------|
| `data/pipelines/obia_segment.json` | `rs:obia_segment` |
| `data/pipelines/obia_classify.json` | `rs:obia_classify` |
| `data/pipelines/obia_export.json` | `rs:obia_segment` → `rs:segment_stats` → `gdal:polygonize` |

### 7.6 Workflow binding (`lab.obia`)

From `registerObia` in `builtin_definitions.cpp`:

| Step id | Kind | Operator / notes |
|---------|------|------------------|
| `open_image` | Interactive | — |
| `segment` | Operator | `rs:obia_segment` → artifact `segment_map` |
| `label` | Interactive | needs `segment_map` |
| `classify` | Operator | `rs:obia_classify` → `classified_output` |
| `export` | Review | needs classified output |

Comment in source: **UI still owns** `RsObiaMainWindow` / `RsObiaTask`; runtime binding can land later. JobEngine module ids used by UI (`module:obia:segment|classify`) are **not** these operator ids.

---

## 8. Dependency sketch

```
                    ┌─────────────────────────────────────┐
                    │  Host: MainWindow (m_obiaWindow)    │
                    └──────────────────┬──────────────────┘
                                       │ opens
                                       v
┌──────────────────────────────────────────────────────────────────┐
│  RsObiaMainWindow                                                │
│  ┌────────────┐  ┌─────────────────┐  ┌──────────────────────┐  │
│  │ Source     │  │ Segment job     │  │ Label / select       │  │
│  │ raster     │─►│ JobEngine       │─►│ mSegmentLabels       │  │
│  │ mRaster*   │  │ module:obia:    │  │ SelectTool+InfoDock  │  │
│  └────────────┘  │  segment        │  └──────────┬───────────┘  │
│                  └────────┬────────┘             │              │
│                           │                      │              │
│                  ┌────────v────────┐             v              │
│                  │ apply: mSegMap  │     Classify job           │
│                  │        mSegStats│────►module:obia:classify   │
│                  └────────┬────────┘     (RsObiaTask)           │
└───────────────────────────┼──────────────────────┬──────────────┘
                            │                      │
         ┌──────────────────┼──────────────────────┘
         v                  v
┌─────────────────┐  ┌──────────────────┐     ┌────────────────────┐
│ RsObia          │  │ RsSegmentFeatures│     │ RsClassifier*      │
│ Segmentation    │  │ + RsSegmentMap   │     │ (analysis/)        │
└────────┬────────┘  └──────────────────┘     └────────────────────┘
         │
    ┌────┴────────────────────┐
    v                         v
 OTB CLI Segmentation    RsSimpleSegmenter
 (temp label GeoTIFF)    (analysis/)
```

### Parallel stacks (no edges between type systems)

```
  Pipelines / lab.obia / toolbox
           │
           ├── rs:obia_segment ──► segutil (cv::Mat) ──► label GeoTIFF
           ├── rs:obia_classify ─► segutil + OpenCV ml ──► class GeoTIFF
           ├── rs:segment_stats ─► CSV
           │
           ├── otb:meanshift_segmentation ──► OTB CLI (filter/mode dialect B)
           └── otb_tools:otb_segmentation ──► OTB CLI (mode dialect A + lsms)
```

### Who depends on whom (high level)

| From | To | How |
|------|-----|-----|
| MainWindow | `RsObiaSegmentation` | Job worker |
| MainWindow | `RsSegmentFeatures` | Job worker after segment |
| MainWindow | `RsObiaTask` | Classify job |
| `RsObiaTask` | `RsObiaSegmentation` | If no existing map |
| `RsObiaTask` | `RsSegmentFeatures` | If no existing stats |
| `RsObiaSegmentation` | `RsSimpleSegmenter` | Fallback |
| `RsObiaSegmentation` | OTB CLI | Prefer path |
| `RsObiaSegmentation` | `RsSegmentMap::fromGeoTIFF` | Load labels |
| `rs:obia_*` | `segutil` | Direct; **not** analysis types |
| Workflow `lab.obia` | `rs:obia_*` ids | Definition only (UI unbound) |
| Processing toolbox | `OtbSegmentationAlgorithm` | Manifest `otb_rs` |

---

## 9. Gaps relevant to multi-level hierarchy (inventory)

These are **observed limits**, not redesign proposals.

| Area | Today | Implication for hierarchy |
|------|-------|---------------------------|
| `RsSegmentMap` | Single `QVector<quint32>` plane | No level index; no parent segment id |
| GUI state | One `mSegMap` / one `mSegStats` | No multi-scale stack in window |
| OTB GUI path | MeanShift only; one label raster | LSMS multi-scale available in **processing** wrapper only, not wired into OBIA UI |
| Features | Stats keyed by flat segment id | No level attribute on `SegmentStat` |
| Operators | Write one label or class GeoTIFF | No multi-band label cube / level GeoPackage |
| Labeling UI | `QMap<quint32,int>` segment→class | No per-level labels |
| Adjacency | Perimeter count only | No region adjacency graph / merge tree |
| Dual stacks | Analysis vs segutil | Hierarchy work would need a chosen stack; they do not share a model |

**Bottom line:** the product seam for “objects” is a **single-layer label map**. Multi-level hierarchy is not represented anywhere in current types or UI state.

---

## 10. Related seams / leakage (inventory)

1. **Dual teaching segmenters:** `RsSimpleSegmenter` (analysis, used by GUI/task) vs `segutil::segmentQuantize` (operators) — same idea, separate code and defaults (e.g. minRegion 100 vs 50).
2. **Dual classify paths:** interactive `RsObiaTask` + `RsClassifierBackend` vs operator `rs:obia_classify` + raw OpenCV ml + polygon training.
3. **Three OTB CLI dialects** (see §6) — same application name `Segmentation`, different argv shapes.
4. **Workflow vs UI:** `lab.obia` references `rs:obia_*`; UI uses JobEngine module ids and analysis APIs.
5. **Layer tree half-built:** `mLayerTree*` constructed but classify result is added via `QgsProject::instance()->addMapLayer`, not a session tree API.
6. **Export stub:** no write of labels/features CSV from the window; export labs go through operators + polygonize.
7. **Config dead field:** `RsObiaTask::Config::threshold` unused by segmentation helper.
8. **mprofiles schema gap:** RSOperator builds mprofiles args from params not fully declared in `schema()`.

---

## 11. Test surface (anchors)

| Test file | Covers |
|-----------|--------|
| `tests/test_obia_segmentation.cpp` | `isOtbAvailable`, simple path, preferOtb fallback |
| `tests/test_obia_task.cpp` | Config; SimpleSegmenter pipeline; OTB fallback; empty labels fail |
| `tests/test_otb_segmentation_params.cpp` | Processing algo params, defaults, MeanShift `buildArgs` + label raster |
| `tests/test_otb_operators.cpp` | RSOperator schema/metadata/registry/exec |
| `tests/test_rs_operators.cpp` | Registry has `rs:obia_*` / `rs:segment_stats`; segment produces labels |
| `tests/test_workflow_runtime.cpp` | `lab.obia` step order and operator ids |

---

## 12. One-page cheat sheet

| Need | Use today |
|------|-----------|
| Interactive OBIA UI | `RsObiaMainWindow` + `RsObiaSegmentation` + `RsObiaTask` |
| In-memory labels | `RsSegmentMap` |
| Object features (GUI/task) | `RsSegmentFeatures` |
| Teaching segment without OTB (GUI) | `RsSimpleSegmenter` via `RsObiaSegmentation` |
| Lab pipeline segment | `rs:obia_segment` |
| Lab pipeline classify | `rs:obia_classify` (grid default) |
| OTB multi-algorithm toolbox | `otb_tools:otb_segmentation` |
| OTB multi-algorithm RSOperator | `otb:meanshift_segmentation` |
| Hierarchy / multi-level | **Not present** — single label layer only |

---

*End of inventory. No redesign recommended in this ticket.*
)
