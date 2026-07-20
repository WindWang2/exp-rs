/***************************************************************************
 * rs_obia_classify_operator.h  —  Object-based classification (teaching OBIA)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_classify
 *
 * End-to-end teaching OBIA pipeline (no OTB required):
 *   1. Segment (smooth → quantize → connected components → merge small)
 *   2. Extract per-segment mean spectral features
 *   3. Label segments by majority vote from training polygons
 *   4. Train SVM / NormalBayes on labeled segments
 *   5. Predict all segments and burn class IDs to output raster
 *
 * Parameters:
 *   input, training, output  (required)
 *   method: svm | normal_bayes (default svm)
 *   classField (default class_id)
 *   segmentMethod: grid (default) | quantize
 *   cellSize (grid mode, default 16)
 *   smoothKernel, quantizeBins, minRegionSize  (quantize mode)
 *   minLabelPixels (default 3)
 *   bands (optional 1-based)
 *
 * Returns: output, segments, labeledSegments, trainSamples, classes, method
 */
class RsObiaClassifyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_classify"; }
    std::string displayName() const override { return "OBIA Classification"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Segment image, train on ROI-labeled objects, classify all objects.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
