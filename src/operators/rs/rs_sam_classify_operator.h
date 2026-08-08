/***************************************************************************
 * rs_sam_classify_operator.h  —  Spectral Angle Mapper classification
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sam_classify
 *
 * Supervised spectral-angle classification. Each pixel is labelled to the
 * reference spectrum whose angular distance is smallest. Pure-C++ kernel
 * (no OpenCV dependency) over a multi-band raster + a JSON/text reference
 * spectra table.
 *
 * Parameters:
 *   input    (string, required)  multi-band raster to classify
 *   output   (string, required)  classified single-band raster (class id)
 *   refs     (array,  required)  reference spectra; each entry is an array of
 *                                floats of length == band count
 *   bands    (array,  optional)  1-based band subset to use (default: all)
 *   angleOut (string, optional)  if set, write the per-pixel best angle raster
 */
class RsSamClassifyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sam_classify"; }
    std::string displayName() const override { return "Spectral Angle Mapper (SAM) Classification"; }
    std::string group() const override { return "classification"; }
    std::string description() const override {
        return "Classify multi-band imagery by spectral angle to reference spectra.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        // Single-pass streaming: per-pixel kernel over a BIP tile window.
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
