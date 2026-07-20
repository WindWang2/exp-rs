/***************************************************************************
 * rs_pca_operator.h  —  Principal Component Analysis RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:pca — Principal Component Analysis on a multi-band raster.
 *
 * Wraps ImageEnhancement::processPcaFile (no GUI).
 *
 * Parameters:
 *   input          (string, required) Input multi-band raster path
 *   output         (string, required) Output PCA components GeoTIFF
 *   numComponents  (int, optional)    Number of components (default: all bands)
 */
class RsPcaOperator : public RSOperator {
public:
    std::string name() const override { return "rs:pca"; }
    std::string displayName() const override { return "Principal Component Analysis"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Compute PCA components of a multi-band raster and write them as a GeoTIFF.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
