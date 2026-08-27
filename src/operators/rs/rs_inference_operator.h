/***************************************************************************
 * rs_inference_operator.h  —  On-device ONNX inference RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Runs ONNX model inference on a raster's bands via the model runtime layer
 * (OpenCV `cv::dnn` provider; zero new dependencies). The operator is the
 * product surface of the runtime stack:
 *
 *   ModelCatalog (manifest v2 contracts, readiness)
 *     → ModelRuntimeRegistry (cached session, backend/device selection)
 *     → TileInferenceEngine (bounded tiled inference with halo/overlap,
 *        manifest preprocessing/postprocessing, streaming output)
 *
 * The model may be given as a direct ONNX path or as a catalog name (see
 * spatial:list_models). Catalog models are gated on real readiness (artifact
 * present, checksum verified, runtime/hardware compatible) before loading.
 *
 * Parameters:
 *   input  (string, required) Input raster path
 *   model  (string, required) ONNX path or ModelCatalog name
 *   output (string, required) Output raster path (.tif)
 *   bands  (array,  optional) 1-based band numbers to feed (default: all bands)
 *
 * Returns JSON object with:
 *   output   (string) Output raster path
 *   backend  (string) e.g. "opencv_dnn"
 *   device   (string) "cpu" | "cuda" (what actually ran)
 *   model    (string) Resolved model name or path
 *   outBands (int)    Model output channel count written
 *   width    (int)    Output raster width (matches input)
 *   height   (int)    Output raster height (matches input)
 *   tileSize (int)    Core tile edge used (px)
 *   tiles    (int)    Tiles processed
 */
class RsInferenceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:infer"; }
    std::string displayName() const override { return "On-Device Inference (ONNX)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run an ONNX model on a raster via the model runtime (tiled, bounded memory, manifest-driven pre/post-processing).";
    }

    /// Tiled engine: per-tile working set, never the whole raster.
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
