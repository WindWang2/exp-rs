/***************************************************************************
 * rs_obia_features_operator.h  —  full per-segment OBIA feature extraction
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_features
 *
 * Full per-segment feature statistics over an existing segment label raster
 * (issue #663: the OBIA GUI's feature step, previously an in-app kernel
 * call, exposed as an operator). Delegates to the analysis-layer
 * RsSegmentFeatures — the same kernel the GUI and rs:obia_hierarchy use:
 * per-band mean/stddev/min/max + GLCM (contrast, correlation, energy,
 * homogeneity) + shape (area, perimeter, shapeIndex, compactness,
 * rectangularity, aspectRatio).
 *
 * The CSV is the interchange contract for interactive sessions and chained
 * workflows: one row per segment id, fixed column order —
 *   segment_id, area, perimeter, shape_index, compactness, rectangularity,
 *   aspect_ratio, then per band b (1-based, in `bands` order):
 *   mean_b, stddev_b, min_b, max_b, glcm_contrast_b, glcm_correlation_b,
 *   glcm_energy_b, glcm_homogeneity_b
 * Values are written with 17 significant digits (exact double round-trip).
 *
 * Parameters:
 *   input   (string, required) — source raster (spectral bands)
 *   labels  (string, required) — segment label raster (UInt32, 0 = nodata;
 *                                e.g. the output of rs:obia_segment)
 *   output  (string, required) — destination CSV path
 *   bands   (array int, optional) — 1-based band indices (default: all)
 *
 * Returns: output, segments (rows written), bands, features (columns per row
 * excluding segment_id)
 */
class RsObiaFeaturesOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_features"; }
    std::string displayName() const override { return "OBIA Object Features"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Extract per-object spectral, GLCM texture and shape features to CSV.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
