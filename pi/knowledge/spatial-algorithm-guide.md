# Spatial Algorithm Selection Guide (exp-rs knowledge base)

Read this before planning remote-sensing / GIS tasks with exp-rs tools.
Algorithm ids are stable; capability sidecars
(`data/processing/algorithm_meta/`) add task/input/output contracts.

## Golden path for optical satellite data

```
import product (Sentinel-2 / Landsat / MODIS)
  → rs:qa_mask            (cloud / shadow / snow)
  → radiometric calibration → atmospheric correction (DOS1/DOS2/QUAC)
  → gdal:reproject (reference grid alignment)  [only if grids differ]
  → rs:apply_mask         (analysis-ready)
  → analysis: indices / classification / change detection / fusion
  → gdal:polygonize       (vector tail for segmentation/classification)
```

The desktop ships this chain as the `lab.preprocess.optical` DAG;
`run_workflow` accepts the same steps as an agent-generated pipeline.

## Task → algorithm map

| Task | Use | Notes |
|---|---|---|
| Vegetation / water / built-up index | `rs:spectral_index` | Roles (NIR/RED/SWIR) auto-resolve from `SICNU_BAND_ROLE`; verify with `spatial:raster_inspect` first |
| Cloud / QA masking | `rs:qa_mask` + `rs:apply_mask` | Landsat QA_PIXEL, Sentinel-2 SCL, generic bitmask |
| Change detection | `rs:change_detection` | diff/ratio/ND-diff/CVA; Otsu/percentile/statistical thresholds; MMU cleanup; grids + radiometric state must match |
| Post-classification change | `rs:post_classification_change` | transition matrix, gains/losses |
| Supervised classification | `rs:supervised_classification` | NormalBayes/SVM; model sidecar validates feature compatibility |
| Segmentation (OBIA) | `rs:obia_segment` (engine=otb \| auto \| simple) | auto prefers OTB MeanShift with teaching fallback; chain `rs:obia_features` (full stats CSV) → `rs:obia_classify` (labels + segmentClasses/training) or `gdal:polygonize` |
| Vectorize a raster | `gdal:polygonize` | segmentation/classification maps → polygons |
| Deep-learning inference | `rs:infer` | ONNX via cv::dnn; model name from `spatial:list_models` |
| Hyperspectral | `rs:mnf`, `rs:sam_classify`, `rs:spectral_unmixing`, `rs:rx_anomaly` | wavelength-aware; start from an ROI mean spectrum |
| Terrain | `rs:terrain_analysis` (slope/aspect/hillshade/TRI/TPI; geographic DEMs auto-converted) or `gdal_tools:gdaldem` | needs an elevation raster |
| Pan-sharpening | fusion (Brovey/IHS/PCA) | grid preflight runs automatically in dialogs |

## Harness 4.0 fast path (plan lifecycle)

When the task matches a sanctioned recipe, prefer the harness plan lifecycle
over hand-authoring pipeline JSON:

```
harness:context          (typed workspace snapshot, revision-stamped)
  → spatial:understand   (typed dataset facts; never guess bands/CRS/modality)
  → harness:search_recipes → harness:instantiate_recipe
  → harness:preflight    (deterministic; blocked = fix inputs, not retry)
  → harness:execute_plan (compiles to the authoritative workflow engine)
  → harness:run_status   (real run state; PASS/PASS_WITH_WARNINGS/FAIL)
```

A FAIL verification is final: report failure, never success. Error codes
(`harness:error_codes`) are stable — read them instead of parsing logs.

## Planning rules

1. **Inspect before you plan**: `spatial:raster_inspect` reveals CRS,
   resolution, band roles, nodata, and `SICNU_RADIOMETRIC_STATE`. Two rasters
   with different radiometric states cannot feed `rs:change_detection`.
2. **Preflight before you execute**: `preflight_algorithm` validates the
   parameter schema, grid compatibility, and estimates RAM — cheaper than a
   failed run.
3. **One algorithm = one step**: chains belong in `run_workflow` pipelines
   with explicit step connections; TaskCenter resolves the DAG order and
   keeps provenance for every output.
4. **Large rasters**: prefer `large_raster_safe` algorithms
   (`search_algorithms` filter) when the input exceeds ~1 GB.
5. **Outputs are assets**: completed executions return an `asset_id`;
   `get_lineage` recovers sources, parameters, and downstream products.

## Failure modes worth remembering

- Band-number vs band-role: prefer roles (NIR/RED) over hard-coded numbers;
   they survive product differences.
- Mixed CRS/grids: reproject to a reference grid before differencing.
- Nodata leaking into indices: check per-band nodata in the inspect output;
   mask first (`rs:apply_mask`).

## Harness 7.0 planning surfaces (use these first)

| Tool | When |
|---|---|
| `harness:resolve_intent` | Start here for any free-text goal: returns the resolved intent (or typed ambiguity with candidates), plus feasibility-ranked capability candidates against a dataset understanding. Never guess an intent from a tie — disambiguate. |
| `harness:preflight` | Before any execution; blocked verdicts are authoritative. Inference intents accept `"model"` refs; temporal intents accept `"temporal_facts"`; unsupervised classify passes `"supervised": false`. |
| `harness:repair_plan` | When `harness:plan` reports repairable issues: deterministic bounded surgery (duplicate ids, dangling outputs) plus advisory actions for everything else. |
| `harness:decision_record` | Record ambiguities/alternatives/parameter choices as typed decisions; later turns read them from `harness:context.decisions`. |
| `harness:context` | Typed continuity: project, assets, runs, plan bindings with verification status, decisions, experiments. Pass `if_revision` for cheap no-op reads. |

Capability knowledge (band roles, modality, SAR/temporal requirements,
verification contracts) lives in `data/agent/capabilities/`; recipes support
`presets` (`bindings.preset`) so sensor variants no longer duplicate files —
e.g. `harness.optical_ndvi` + preset `landsat` replaces the old Landsat twin.
