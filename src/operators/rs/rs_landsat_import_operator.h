/***************************************************************************
 * rs_landsat_import_operator.h  —  Import Landsat MTL scene → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Discovers a Landsat Collection 1/2 scene (MTL + band GeoTIFFs) and stacks
 * selected optical bands into a single multi-band GeoTIFF for teaching / Agent use.
 *
 * Parameters:
 *   input   (string, required)  Path to *_MTL.txt or scene directory
 *   output  (string, required)  Output multi-band GeoTIFF
 *   bands   (array, optional)   Band names e.g. ["B2","B3","B4","B5"]; default OLI B1–B7
 *
 * Returns: output, productId, spacecraft, bandCount, bands[], width, height
 */
class RsLandsatImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:landsat_import"; }
    std::string displayName() const override { return "Landsat Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a Landsat scene (MTL + bands) into a multi-band GeoTIFF.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
