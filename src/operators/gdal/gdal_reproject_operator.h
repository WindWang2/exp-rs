/***************************************************************************
 * gdal_reproject_operator.h  —  GDAL raster reprojection RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::gdal {

/**
 * gdal:reproject — reproject a raster to a target CRS via GDALWarp.
 *
 * JSON parameters:
 *   - input            : input raster path (mandatory)
 *   - output           : output raster path (mandatory)
 *   - dstCrs           : target CRS (EPSG:xxxx or WKT) (mandatory)
 *   - srcCrs           : override source CRS (optional)
 *   - resampling       : nearest|bilinear|cubic|cubicspline|lanczos (default: bilinear)
 *   - targetResolution : output pixel size in target CRS units (optional)
 *   - reference        : reference raster whose grid (CRS, pixel size, extent)
 *                        the output aligns to (optional; overrides dstCrs and
 *                        targetResolution — grid harmonization)
 *   - nodata           : output no-data value (optional)
 */
class GdalReprojectOperator : public RSOperator {
public:
    std::string name() const override { return "gdal:reproject"; }
    std::string displayName() const override { return "GDAL Reproject Raster"; }
    std::string group() const override { return "gdal-geometry"; }
    std::string description() const override {
        return "Reproject a raster dataset to a target CRS using GDALWarp.";
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
