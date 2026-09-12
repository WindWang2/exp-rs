// src/operators/runtime/model_execution_service.h — THE model execution seam.
//
// Platform 4.0 convergence point: resolve → contract gates → runtime
// readiness → session acquire → engine run → result payload. Every surface
// that executes a raster model (rs:infer, rs:segment, rs:detect,
// rs:embedding, and through them CLI / Workflow / TaskCenter / MCP / Pi / GUI
// task helpers) goes through runModelInference(); there is no second path and
// no per-model copy of tile/preprocess/postprocess logic.
//
// Compatibility contract (pinned by tests): the raster result payload keys
// (output/backend/device/model/outBands/width/height/tileSize/tiles/
// tilesSkippedNoData), the error-code mapping (readiness → FileNotFound /
// InvalidInputData, runtime verdict → InvalidInputData, load →
// ComputationError) and the cancellation behavior are inherited verbatim from
// the historical rs:infer implementation.
#pragma once

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// One model execution request (all surfaces map their parameters onto this).
struct ModelExecutionRequest
{
  std::string inputPath;
  std::string modelReference;   ///< weight file path OR catalog stable id
  std::string outputPath;       ///< raster (.tif) or vector (.gpkg/.geojson/.shp)
  std::vector<int> bands;       ///< 1-based; empty = all
  std::string deviceToken;      ///< "" = manifest runtime.device (auto semantics)
  TtaMode tta = TtaMode::None;
  int batchSizeOverride = 0;    ///< batchCap (a cap, never an upgrade)
  bool asDetection = false;     ///< vector output via DetectionTileEngine
  RasterOutputMode outputMode = RasterOutputMode::Probability; ///< manifest default unless overridden
  double confOverride = -1.0;   ///< detection confidence gate override (<0 = manifest)
  double nmsIouOverride = -1.0; ///< detection NMS IoU override (<0 = manifest)
  // --- Platform 9.0 tile blending -------------------------------------------
  TileBlend blend = TileBlend::Unset; ///< Unset = the manifest's tiling.blend
  // --- Platform 7.0 multimodal / temporal surface ---------------------------
  /// Named multi-input feeds (empty = the historical single-input path over
  /// @p inputPath). When declared, feeds map onto the manifest inputs[]
  /// contracts and @p inputPath is ignored; temporal models provide one feed
  /// entry per declared input with one path per frame.
  std::vector<NamedRasterFeed> namedInputs;
};

/// Execution outcome: the compatibility payload plus full stats.
struct ModelExecutionResult
{
  Json::Value payload;                 ///< result keys consumed by CLI/MCP/workflow/GUI
  TileInferenceStats rasterStats;
  DetectionTileStats detectionStats;
  std::string identityTag;             ///< "id@version" of the resolved model
  std::string contentDigest;           ///< session-identity anchor (when available)
  std::string backend;
  std::string device;
};

/// Resolve a model reference to a contract (exported from the historical
/// rs:infer helper so estimateExecution paths share one resolver).
/// Direct file references build an ad-hoc contract; catalog names go through
/// the full readiness pipeline.
ModelInfo resolveModelReference( const std::string &modelReference, std::string *errorDetail );

/// Execute one model run end-to-end. Throws RSOperatorError on any failure;
/// never leaves a partial output behind (atomic publication in the engines).
ModelExecutionResult runModelInference( const ModelExecutionRequest &request,
                                        RSOperatorContext &context );

} // namespace sicnu::operators::runtime
