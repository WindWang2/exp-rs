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

#include <functional>
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
  int tilesSkippedNoData = 0; ///< tiles whose forward pass was skipped (all core pixels nodata, #705)
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

    // --- Manifest contract validators (#690 / #705) ---------------------------
    // Pure functions shared by run() and the unit tests: they return an empty
    // string when the contract holds, else a human-readable failure naming the
    // offending band / tensor / shape.

    /// input.dtype must match the actual GDAL type of EVERY band fed to the
    /// model (band 1 alone misses mixed-type rasters). @a bands are 1-based;
    /// @a bandDataType maps a 1-based band number to its GDAL data type.
    static std::string inputDTypeMismatch( const ModelInfo &model, const std::vector<int> &bands,
                                           const std::function<int( int )> &bandDataType );

    /// Every declared output.tensor_names entry must exist in the loaded
    /// graph. Skipped (returns empty) when nothing is declared or when the
    /// runtime cannot enumerate outputs (empty @a graphOutputNames).
    static std::string missingOutputTensor( const ModelInfo &model,
                                            const std::vector<std::string> &graphOutputNames );

    /// The writer emits float32 — a non-CV_32F output tensor would be
    /// bit-cast into garbage on disk (#690).
    static std::string outputTypeMismatch( int outputCvType, const std::string &tensorName );

    /// The raster head writes one channel per class: the declared classes
    /// count must match the probability tensor's channel count.
    static std::string classesChannelMismatch( const ModelInfo &model, int outputChannels,
                                               const std::string &tensorName );

    /// True when the pending batch holds at least one tile and every tile has
    /// zero valid (finite in all bands) core pixels — the forward pass can be
    /// skipped and NoData written directly (#705).
    static bool batchIsAllNoData( const std::vector<int> &validPixelCounts );

  private:
    ModelInfo m_model;
    ModelRuntimePtr m_runtime;
};

} // namespace sicnu::operators::runtime
