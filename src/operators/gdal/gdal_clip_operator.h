/***************************************************************************
 * gdal_clip_operator.h  —  GDAL raster clip RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::gdal {

/**
 * gdal:clip — clip a raster by vector cutline and/or geographic extent.
 *
 * At least one of cutline or extent must be provided.
 *
 * JSON parameters:
 *   - input            : input raster path (mandatory)
 *   - output           : output raster path (mandatory)
 *   - cutline          : vector dataset path used as cutline (optional)
 *   - cropToCutline    : bool, crop extent to cutline (default: true)
 *   - extent           : [xmin, ymin, xmax, ymax] in source CRS (optional)
 *   - resampling       : nearest|bilinear|cubic|cubicspline|lanczos (default: nearest)
 *   - nodata           : output no-data value (optional)
 */
class GdalClipOperator : public RSOperator {
public:
    std::string name() const override { return "gdal:clip"; }
    std::string displayName() const override { return "GDAL Clip Raster"; }
    std::string group() const override { return "gdal-geometry"; }
    std::string description() const override {
        return "Clip a raster by vector cutline and/or rectangular extent using GDALWarp.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        // In-process GDAL manages its own tiling (GDALWarp streams internally).
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::gdal
