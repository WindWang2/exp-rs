// src/operators/runtime/tile_inference_engine.h — bounded tiled model inference.
//
// Raster → TilePlanner → windowed reads (halo/overlap-aware) → preprocessing
// (manifest contract) → batched forward passes on a shared runtime session →
// postprocessing (mask threshold, nodata reconstruction) → streaming tile
// writes → georeferenced result. Memory is O(batch × tile × bands), never
// O(raster): the input is never materialized whole and the output is written
// tile-by-tile through GdalStreamingOutput. Progress and cancellation are
// checked per tile batch.
#pragma once

#include "operators/runtime/model_runtime.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_context.h"

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// Geometry + counters describing one engine run (also feeds estimates).
struct TileInferenceStats
{
  int tileSize = 0;       ///< core tile edge used (px)
  int halo = 0;           ///< halo radius per side (px)
  int batchSize = 1;
  int tilesPlanned = 0;
  int tilesProcessed = 0;
  int outBands = 0;       ///< model output channels written
  int outWidth = 0;       ///< output raster width (== input width)
  int outHeight = 0;      ///< output raster height (== input height)
};

class TileInferenceEngine
{
  public:
    /**
     * @param model    catalog model (contracts: preprocess, tiling, postprocess)
     * @param runtime  loaded session (from ModelRuntimeRegistry — reused
     *                 across tiles AND across runs)
     */
    TileInferenceEngine( ModelInfo model, ModelRuntimePtr runtime );

    /**
     * Run tiled inference over the input raster.
     * @param bands  1-based band numbers to feed (empty = all bands)
     * @throws RSOperatorError on read/forward/write failure or cancellation.
     */
    TileInferenceStats run( const std::string &inputPath, const std::vector<int> &bands,
                            const std::string &outputPath, RSOperatorContext &context );

    /// Effective tile geometry for a raster (manifest tiling contract +
    /// fixed graph input size fallback, engine floor of 16 px).
    static int effectiveTileSize( const ModelInfo &model );
    static int effectiveHalo( const ModelInfo &model );

  private:
    ModelInfo m_model;
    ModelRuntimePtr m_runtime;
};

} // namespace sicnu::operators::runtime
