# INTERFACE_HANDOFF — R10-A findings owned by other tracks (read-only handoff)

Found during R10-A; fixes live outside this track's ownership. No files were modified.

## → geospatial/data track (PR #1475 adjacency)

1. **PR-3 [P2]** `src/data/raster_grid_compat.cpp:180-181` — `compareGrids` returns zero
   issues when ONE side lacks a geotransform while a one-sided missing CRS is blocking.
   Consumers (rs_change_detection :264-278, dNBR :738-748) then pixel-compare
   coordinate-less data. Suggested: mirror the CRS-asymmetry blocking semantics
   (issue `grid.missing_geotransform`) — guarded by a test with a CRS-bearing,
   GT-less PNG+prj raster.

## → agent/harness owners (excluded from R10-A: src/agent)

2. **A1-D1 [P1]** `src/agent/harness/capability_catalog.cpp:334-377` — `ioRow()` parameter
   branch drops `isArray`/`itemType`, so 63/157 capability sidecars flatten array params
   (`bands`, `scenes`, `inputs`, …) to scalar element types; the type vocabulary has no
   `array` at all. R10-A fixes the consuming adapter + sidecar data + a drift gate, but
   regenerating sidecars with the current generator will re-flatten until the parameter
   branch emits array/item_type like the data branch (:349-353) does.
3. **A1-D3 [P2]** capability catalog scope is rs-only by design (README) but the MCP
   list/search/get_schema surface does not SAY so — gdal:/io:/otb:/opencv: (32 operators)
   are invisible to agents. Suggested: declare scope in the response or extend the catalog.

## → core owners (src/core not owned by R10-A)

4. **#1467 [P2]** `src/processing/CMakeLists.txt:5-9,415-425` compiles `src/core/spatial_split.cpp`,
   `src/core/temporal_cube.cpp` and even `src/app/widgets/timeline_scrubber_widget.cpp` into
   `sicnu_processing` (tests-only consumers). R10-A can consolidate the classification-side
   folds twin (rs_spatial_cross_validation.h:21) but moving core sources needs the core owner.
5. **#1457 [P1, unclaimed]** `sample_promotion:328` / `dataset_store_impl.h:39` — no open PR
   covers these sub-items (from S1b issue map); leaving a note so a data-track picks them up.

## Shared-contract notes (no action needed)

- #1450 SHA-256 forks: none of the four forks live in R10-A paths (S1b verified); R10-A
  uses existing shared digest helpers only.
- #1475's rs_json_params.cpp / model_catalog* final shape will be re-read after merge
  before any R10-A follow-up touches neighboring call sites.
