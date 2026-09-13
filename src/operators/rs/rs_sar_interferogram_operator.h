/***************************************************************************
 * rs_sar_interferogram_operator.h — interferogram + coherence
 * (Advanced SAR / PolSAR / InSAR 10.0, package C)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_interferogram — base interferometric product of a co-registered
 * complex SLC pair under the SAME-GRID contract (D-005; #929 semantics).
 *
 * Pair preflight (typed refusals, never approximated):
 *   GRID_MISMATCH          different CRS / resolution / origin / extent
 *                          (remedy: rs:sar_coregister or external alignment)
 *   COMPLEX_BANDS_REQUIRED non-CFloat32 master/slave bands
 *   DIMENSION_MISMATCH     same grid fields but different raster sizes
 *
 * Products:
 *   output           CFloat32 single-band complex interferogram
 *                    s1·conj(s2) (wrapped phase = arg, amplitude = |s1 s2|)
 *   coherenceOutput  optional Float32 coherence raster in [0, 1]
 *                    (windowCoherence over coherenceWindow; DECISIONS D-010
 *                    — NaN where no jointly valid pair exists)
 *
 * flattenRamp (D-006): none | linear | quadratic — a robust streaming
 * polynomial fit of the wrapped interferometric phase (PhaseRampFitter,
 * IQR-clipped, O(1) memory) subtracted from the written interferogram's
 * phase. This is an honest low-order flat-earth approximation, NOT
 * topographic phase removal from DEM/orbit.
 */
class RsSarInterferogramOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_interferogram"; }
    std::string displayName() const override { return "SAR Interferogram"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Interferogram (s1·conj(s2)) and optional coherence from a "
               "co-registered complex SLC pair on the same grid, with an "
               "optional robust flat-earth ramp removal.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
