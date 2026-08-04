# 03 — Post-Processing Visual DAG Pipeline Integration

**What to build:** Provide a built-in workflow recipe in `PresetCatalogWidget` that chains classification, 3x3 majority filtering, and class recoding in topological order, registering intermediate outputs as `TaskTemporary` assets and publishing the final result to the map canvas.

**Blocked by:** 02 — Class Merging & Recode Asynchronous Seam.

**Status:** closed

- [x] Preset Catalog contains "Classification with Noise Removal & Class Merge" recipe.
- [x] Topological DAG execution runs `RsPostProcess::majorityFilter` followed by `RsPostProcess::recode`.
- [x] Final output raster toggles `addToMap = true` to render on the pipeline canvas and map view.
