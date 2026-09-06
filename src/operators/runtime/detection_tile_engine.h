// src/operators/runtime/detection_tile_engine.h — tiled detection inference.
//
// Detection models never stitched probability planes: their head emits boxes
// per tile, so the authoritative raster tiler's plane path does not apply.
// This engine reuses the SAME skeleton — tile grid, windowed reads,
// manifest preprocessing, bounded batches, per-batch cancel/progress, OOM
// ladder — but produces vector output:
//
//   window read → preprocess/resize → batched forward → per-tile decode
//   (center-in-core-tile rule keeps overlap detections single) → bounded
//   accumulation → whole-raster NMS (the tile dedup) → georeferenced
//   polygons → atomic vector publish (same-dir temp + rename).
//
// Memory is O(batch × input tensor + kept detections) — never O(raster).
#pragma once

#include "operators/runtime/detection_postprocess.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include "operators/framework/rs_operator_context.h"

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

struct DetectionTileStats
{
  int tileSize = 0;
  int contextHalo = 0;
  int batchSize = 1;
  int tilesPlanned = 0;
  int tilesProcessed = 0;
  int rawDetections = 0;     ///< decoded before NMS/dedup
  int detectionsKept = 0;    ///< after whole-raster NMS/dedup
  int batchReductions = 0;   ///< OOM ladder splits
  int rasterWidth = 0;
  int rasterHeight = 0;
};

class DetectionTileEngine
{
  public:
    DetectionTileEngine( ModelInfo model, ModelRuntimePtr runtime );

    /**
     * Run tiled detection over the input raster and publish a georeferenced
     * vector (driver by extension: GPKG | GeoJSON | ESRI Shapefile).
     * @param bands 1-based band numbers to feed (empty = all bands)
     * @throws RSOperatorError on contract/read/forward/write failure or
     *         cancellation. No output file is left behind on failure.
     */
    DetectionTileStats run( const std::string &inputPath, const std::vector<int> &bands,
                            const std::string &outputPath, RSOperatorContext &context,
                            const TileInferenceRunOptions &options = TileInferenceRunOptions{} );

    /// Static contract check (empty = executable): detection contract declared
    /// and valid, resize to_input with a fixed graph input, head enumerated or
    /// advisory. Shared by run() and the operator preflight surfaces.
    static std::string checkContract( const ModelInfo &model );

  private:
    ModelInfo m_model;
    ModelRuntimePtr m_runtime;
};

} // namespace sicnu::operators::runtime
