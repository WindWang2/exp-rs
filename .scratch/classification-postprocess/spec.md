# Specification: Classification Post-Processing, Class Merging & Class Renaming

**Feature Name:** Classification Post-Processing & Interactive Class Merging / Renaming  
**Status:** `READY_FOR_AGENT`  
**Target Path:** `.scratch/classification-postprocess/spec.md`  
**Date:** 2026-08-04  

---

## Problem Statement

After running remote sensing classification (e.g. K-Means clustering or Supervised SVM classification), users receive raster maps containing raw integer pixel class IDs ($1 \dots K$). 

Users currently face three major friction points:
1. **Meaningless Numeric Labels**: Class maps show raw IDs ($1, 2, 3$) rather than human-readable domain categories (e.g. "Water", "Forest", "Built-up").
2. **Over-segmented Clusters**: Spectral clustering produces fine-grained sub-classes (e.g. "Shallow Water" and "Deep Water") that users need to merge into single target classes.
3. **Noise & Salt-and-Pepper Artifacts**: High-frequency single-pixel noise requires spatial filtering (Majority Filtering or Sieve filtering) that should chain directly into class recoding.

---

## Solution

A unified classification post-processing workspace and workflow integration in `exp-rs`:
1. **Interactive Class Renaming & Palette Editing**: Dynamic UI in `ClassTableWidget` enabling double-click renaming of class labels and RGB color editing with immediate map canvas feedback (`QgsPalettedRasterRenderer`).
2. **Multi-Select Class Merging**: Multi-row selection in the class table allowing users to merge selected sub-classes into a target category, executed via `RsPostProcess::recode` and `TaskCenter`.
3. **Sidecar Metadata Persistence**: Class definitions (ID, name, color) auto-save alongside rasters as `<raster_name>.class.json`.
4. **DAG Pipeline Integration**: Built-in visual pipeline recipes chaining classification, majority filtering, and class recoding in topological order.

---

## User Stories

1. As a remote sensing analyst, I want to double-click a class ID in the class table to rename it (e.g. $1 \rightarrow \text{"Water"}$), so that the map legend displays meaningful land cover categories.
2. As a GIS specialist, I want to edit the RGB color swatch for a class in the table, so that the map canvas instantly updates its visual palette without re-writing the raster file.
3. As an analyst working with K-Means results, I want to select multiple spectral cluster rows (e.g. ID 1 and ID 3) and click "Merge Selected Classes", so that they are merged into a single thematic category.
4. As a user, I want class merge operations to run asynchronously through `TaskCenter`, so that the main UI looper remains responsive during large raster processing.
5. As a project manager, I want class names and colors to automatically persist in a sidecar `.class.json` file, so that reloading the project restores the custom legend state.
6. As a workflow designer, I want a pre-configured pipeline recipe in the Preset Catalog that chains classification, majority filtering, and class recoding, so that I can batch-process datasets in one click.
7. As a QA engineer, I want class recoding operations to preserve original raster dimensions and CRS projection metadata, so that spatial alignment remains exact.

---

## Implementation Decisions

### 1. UI Seam (`ClassTableWidget` & `QgisDisplayManager`)
- **Table Controls**: `ClassTableWidget` manages an ordered list of `RsClassDef` objects (`id`, `name`, `color`).
- **Live Canvas Binding**: Modifying a cell emits a signal to `QgisDisplayManager` which updates the active `QgsPalettedRasterRenderer` palette dynamically.

### 2. Execution Engine (`RsPostProcess::recode` & `TaskCenter`)
- **Immutability**: Class merging produces a new GeoTIFF asset via [`RsPostProcess::recode(src, dst, map)`](file:///home/kevin/projects/exp-rs/src/analysis/classification/rs_post_process.h#L58). Original classification rasters remain unchanged.
- **Task Center Integration**: Enqueued as `rs:recode` through `TaskCenter::enqueueTask()`.

### 3. Metadata Serialization Schema
- Sidecar metadata is stored as JSON:
  ```json
  {
    "version": 1,
    "classes": [
      { "id": 1, "name": "Water", "color": "#0000ff" },
      { "id": 2, "name": "Forest", "color": "#00ff00" }
    ]
  }
  ```

---

## Testing Decisions

### 1. External Behavior Verification
- Tests must verify external raster outputs, sidecar JSON payloads, and Catch2 signal assertions without coupling to private widget internal fields.

### 2. Test Suites & Prior Art
- **Unit Test**: `tests/test_classifier_load_save.cpp` (verifies sidecar JSON round-tripping).
- **Integration Test**: `tests/test_classification_task_center.cpp` (verifies `RsPostProcess::recode` execution through `TaskCenter`).
- **Controller Test**: `tests/test_classify_workflow_controller.cpp` (verifies UI controller state machine).

---

## Out of Scope

- Direct pixel-level manual manual digitizing / painting on output rasters (handled via ROI tools prior to classification).
- Cloud-native STAC metadata publishing (deferred to post-v1 release).

---

## Further Notes

- All changes strictly adhere to C++17 / Qt6 guidelines and Andrej Karpathy simplicity rules.
