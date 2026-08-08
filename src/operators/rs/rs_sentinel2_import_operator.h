/***************************************************************************
 * rs_sentinel2_import_operator.h  —  Import Sentinel-2 SAFE → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Discovers a Sentinel-2 SAFE product and stacks bands at a chosen resolution
 * into a multi-band GeoTIFF.
 *
 * Parameters:
 *   input       (string, required)  Path to .SAFE dir or MTD_MSIL*.xml
 *   output      (string, required)  Output multi-band GeoTIFF
 *   resolution  (enum, optional)    "10m" | "20m" | "60m" (default "10m")
 *   bands       (array, optional)   e.g. ["B2","B3","B4","B8"]; default by resolution
 *
 * Returns: output, productId, spacecraft, resolution, bandCount, bands[], width, height
 */
class RsSentinel2ImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sentinel2_import"; }
    std::string displayName() const override { return "Sentinel-2 Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a Sentinel-2 SAFE product into a multi-band GeoTIFF.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
