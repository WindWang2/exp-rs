# Tiled Raster Inference

Large rasters never fit a model (or memory) in one pass. The tile engines
turn `input raster → model → geospatial product` into a bounded pipeline:

```text
window read (core tile + halo, NaN padding outside)
  → NoData sentinel → NaN normalization
  → preprocess (manifest contract: mean/std · scale · resize · nodata_policy)
  → batch (budget-aware size, capped by batchCap)
  → forward pass(es) on the shared session
  → OOM ladder (batch shrink only — semantics never change)
  → postprocess (threshold · derived format · uncertainty · NoData restore)
  → streaming tile writes into a same-directory stage file
  → rename onto the caller's path (atomic publish)
```

Memory is **O(batch × tile × bands)** — the input is never materialized whole
and the output is written tile-by-tile. Progress and cancellation are checked
per tile; a cancel leaves no output file.

## Geometry

- Core tile grid: `tile_size` (manifest, or the fixed graph input, default
  512, floor 16). Edge tiles are clipped to the raster.
- Halo: `halo` or `overlap/2` per side. Grid-preserving models get the halo
  cropped away (no seams); resizing models scale the core rect back.
- `resize: to_input` resamples each window to the fixed graph input; output
  mapping uses the window/tensor scale ratio.

## NoData and masks

- Declared band sentinels become NaN before preprocessing.
- Non-finite samples become 0 for the model (`nodata_policy: zero`), tracked
  in a per-tile validity mask, and the output pixel is restored to the
  writer's NoData sentinel afterwards.
- A batch whose tiles are entirely NoData skips the forward pass entirely
  (NoData written directly); an all-NoData raster runs one probe forward to
  establish the output shape.

## Batching and the OOM ladder

The manifest `batch_size` is the request; `effectiveBatchSize` clamps it by
the VRAM budget (env-declared `SICNU_MODEL_VRAM_MB`; CPU RAM admission is
owned by TaskCenter). `batchCap` is a cap, never an upgrade.

On a classified `OutOfMemory` failure the pending batch is **retried
tile-by-tile** (`batchReductions` in the stats). Batch size never changes
per-tile semantics — convolutions are sample-independent — so the scientific
contract holds. An OOM at batch=1 is terminal with a diagnostic naming the
tile size; the engines **never** shrink tiles, change spatial resolution, or
alter model semantics to fit memory.

## Atomic publication

The streaming writer stages into `<output>.tmp~` in the output's directory
(same volume, atomic rename). Only a fully written, closed raster is renamed
onto the caller's path; every failure path abandons/removes the stage. A
crash or failure leaves the OLD output untouched — a truncated file never
looks like a result.

## Detection tiles

Detection models emit boxes, not planes, so the detection engine reuses the
same skeleton with three differences:

1. windows resize to the fixed graph input (mandatory contract);
2. each tile's raw tensor is decoded (`output.detection` contract) into
   raster-pixel boxes; a **center-in-core-tile rule** keeps objects detected
   in the halo/overlap region from doubling;
3. the accumulated boxes pass one whole-raster NMS (the tile dedup) with a
   deterministic order (confidence desc, ties by class/geometry), then are
   published as georeferenced polygons (GPKG/GeoJSON/SHP by extension) with
   `class`, `confidence`, `tile_x`, `tile_y` fields — atomic like the raster
   path. Accumulation is bounded by `max_detections`; exceeding it fails
   loudly instead of silently dropping.

## Verified bounds

- 100k × 100k logical extent (sparse GeoTIFF) runs as 200×200 windows of
  512 px without whole-raster allocation — `tests/test_model_failure_matrix.cpp`
  pins tile counts, bounded file size and publication.
- Mid-run provider crash, cancel and unwritable paths leave no output and no
  `.tmp~` residue (same suite).
