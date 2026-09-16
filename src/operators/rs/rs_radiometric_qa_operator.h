/***************************************************************************
 * rs_radiometric_qa_operator.h — radiometric-physics-11 (F/G)
 *
 * Streams a reflectance raster into a per-pixel QA flag raster (one uint16
 * band per input band, RadiometricQa vocabulary) plus a summary report.
 * Optional inputs: a binary cloud/shadow mask raster (QaMask convention,
 * same grid, refused on mismatch) and a QA_RADSAT-style saturation band
 * inside the input stack. Flags are additive across sources; output band 0
 * value = clean pixel.
 ***************************************************************************/
#pragma once
#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsRadiometricQaOperator : public RSOperator {
public:
    std::string name() const override { return "rs:radiometric_qa"; }
    std::string displayName() const override { return "Radiometric QA Flags"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Derive per-pixel radiometric QA flags (saturation, negative, over-range, "
               "non-finite, cloud/shadow/snow propagation, QA_RADSAT saturation bits) for each "
               "band of a reflectance raster, written as uint16 flag bands with a summary "
               "report. Flags union across sources; 0 = clean pixel.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run(const Json::Value &params, RSOperatorContext &context) override;
};

} // namespace sicnu::operators::rs
