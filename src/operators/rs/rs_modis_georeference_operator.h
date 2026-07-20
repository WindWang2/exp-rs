/***************************************************************************
 * rs_modis_georeference_operator.h  —  MODIS sinusoidal / reproject georef
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Assign NASA MODIS sinusoidal tile geotransform/CRS to an unreferenced
 * MODIS raster (or force overwrite), then optionally warp to a destination CRS.
 *
 * Parameters:
 *   input      (string, required)  Input raster (often from rs:modis_import)
 *   output     (string, required)  Output GeoTIFF
 *   dstCrs     (string, optional)  Target CRS (default EPSG:4326); empty = sinusoidal only
 *   tileH      (int, optional)     Horizontal tile 0–35 (else parse hXXvYY from filename)
 *   tileV      (int, optional)     Vertical tile 0–17
 *   resampling (string, optional)  nearest|bilinear|cubic|lanczos (default bilinear)
 *
 * Returns: output, tileH, tileV, dstCrs
 */
class RsModisGeoreferenceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:modis_georeference"; }
    std::string displayName() const override { return "MODIS Georeference"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Assign MODIS sinusoidal tile georeference and optionally reproject "
               "(e.g. to EPSG:4326).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
