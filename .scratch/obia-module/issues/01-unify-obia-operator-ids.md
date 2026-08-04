# 01 — Unify OBIA Operator IDs & Workflow Registry Contracts

Type: grilling
Status: resolved
Blocked by:

## Question

In `PresetCatalogWidget`, the preset `preset_obia_seg_classify` specifies operator IDs `"obia:segmentation"` and `"obia:classification"`, neither of which exist in `RSOperatorRegistry`. Meanwhile, `builtin_definitions.cpp` (`lab.obia`) specifies `rs:obia_segment` and `rs:obia_classify`, while `RsObiaMainWindow` submits `TaskCenter` jobs using `module:obia:segment` and `module:obia:classify`.

How should we unify these operator IDs across `PresetCatalogWidget`, `builtin_definitions.cpp`, `RSOperatorRegistry`, and `TaskCenter` so that visual DAG pipeline execution works seamlessly end-to-end, and what automated regression test should be added to validate that all preset operator IDs exist in `RSOperatorRegistry`?

## Answer

1. Updated `PresetCatalogWidget` preset `preset_obia_seg_classify` (`src/app/workflow/preset_catalog_widget.cpp`) to use canonical operator IDs `rs:obia_segment` and `rs:obia_classify`.
2. Added `$image_import.image_raster` and `$obia_segment.segmented_vector` placeholder parameters for automatic DAG edge parameter binding.
3. Added automated unit tests in `test_workflow_pipeline_ui.cpp` confirming all preset recipes resolve successfully in `RSOperatorRegistry`.

