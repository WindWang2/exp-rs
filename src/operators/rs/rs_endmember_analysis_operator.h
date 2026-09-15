/***************************************************************************
 * rs_endmember_analysis_operator.h  —  endmember set analysis
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Analysis over an extracted endmember table (e.g. rs:endmember_extraction's
 * `endmembersOut` artifact): redundancy reduction by spectral-angle
 * clustering, an optional pairwise SAM angle matrix, and optional sensor
 * projection. The output is a DERIVED exp-rs:spectral-table that inherits
 * the input's license story (measured inputs keep their license/citation).
 *
 * Parameters:
 *   endmembersRef   (string, required) Input exp-rs:spectral-table path
 *   output          (string, required) Output table artifact path
 *   mergeAngleDegrees (number, default 2.0) cluster merge threshold (0 = off)
 *   ppiCounts       (int array, optional) per-row purity ranking for
 *                   representative selection (absent = all tie)
 *   angleMatrix     (bool, default false) embed the pairwise SAM matrix
 *                   (radians) in the result JSON (input rows <= 64)
 *   sensor          (string, optional) sensor id (data/spectral/sensors.json);
 *                   projects the REDUCED set onto the sensor grid
 *   requireFullCoverage (bool, default false) refuse projection when any
 *                   reduced endmember lacks source coverage
 *
 * Returns JSON with output/input counts, cluster map, digests, license echo.
 */
class RsEndmemberAnalysisOperator : public RSOperator {
public:
    std::string name() const override { return "rs:endmember_analysis"; }
    std::string displayName() const override { return "Endmember Analysis"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Reduce an endmember set to non-redundant representatives "
               "(spectral-angle clustering), report the SAM matrix, and "
               "optionally project onto a sensor grid — as a provenance-"
               "carrying spectral table.";
    }

    RSOperatorMemoryPolicy memoryPolicy() const override {
        // Table-resident: curated endmember sets are O(100) spectra.
        return RSOperatorMemoryPolicy::FullRaster;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
