/***************************************************************************
 * rs_mosaic_operator.h  —  Multi-raster mosaic RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:mosaic — mosaic multiple co-registered single-band rasters into one.
 *
 * Inputs must share CRS and roughly compatible pixel size. Band 1 of each
 * input is read as float and merged via Mosaic::merge (first-valid wins).
 *
 * Parameters:
 *   inputs  (array of string, required) Input raster paths
 *   output  (string, required)          Output GeoTIFF path
 */
class RsMosaicOperator : public RSOperator {
public:
    std::string name() const override { return "rs:mosaic"; }
    std::string displayName() const override { return "Raster Mosaic"; }
    std::string group() const override { return "composition"; }
    std::string description() const override {
        return "Mosaic multiple rasters (band 1) into a single GeoTIFF covering their union extent.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
