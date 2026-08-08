/***************************************************************************
 * rs_inference_operator.h  —  On-device ONNX inference RSOperator (tracer bullet)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Runs ONNX model inference on a raster's bands via OpenCV's `cv::dnn` backend
 * (zero new dependencies: OpenCV is already vendored in-process). This is the
 * Edge-AI tracer bullet — a narrow, end-to-end slice that proves pure-C++
 * inference walks the existing RSOperator chain: raster in → model load →
 * forward pass → raster out, with JSON parameters/results per the operator
 * contract (ADR 0012), so it auto-registers and is visible to the agent surface.
 *
 * The operator is model-agnostic: it feeds the selected bands as a 4-D NCHW
 * blob (1, bandCount, height, width) to whatever model is supplied and writes
 * the model's first output, reshaped back to the raster grid, as a single-band
 * GeoTIFF copying the input's georeferencing. It deliberately does no
 * pre/post-processing (normalization, mask→polygon, NMS) — those are out of
 * scope for this slice and land as separate operators once real models drive them.
 *
 * Parameters:
 *   input  (string, required) Input raster path
 *   model  (string, required) Path to an ONNX model readable by cv::dnn
 *   output (string, required) Output raster path (.tif)
 *   bands  (array,  optional) 1-based band numbers to feed (default: all bands)
 *
 * Returns JSON object with:
 *   output    (string) Output raster path
 *   backend   (string) "opencv_dnn"
 *   outBands  (int)    Number of bands written (the model's output channel count)
 *   width     (int)    Output raster width (matches input)
 *   height    (int)    Output raster height (matches input)
 */
class RsInferenceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:infer"; }
    std::string displayName() const override { return "On-Device Inference (ONNX)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run an ONNX model on a raster with the cv::dnn backend and write the output raster.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
