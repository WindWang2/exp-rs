/***************************************************************************
 * gdal_orthorectification_operator.h  —  GDAL RPC/GCP orthorectification
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::gdal {

/**
 * GDAL orthorectification operator (gdal:orthorectification).
 *
 * Performs orthorectification on a raster that carries RPC (Rational
 * Polynomial Coefficients) or GCP (Ground Control Points) metadata. When a
 * DEM is supplied, elevation-dependent RPC correction is applied through
 * GDAL's `RPC_DEM` transformer option. Without a DEM, a constant elevation
 * fallback is used (default 0 m).
 *
 * Internally the operator calls GDAL's `GDALWarp` C API, which is the same
 * engine used by the `gdalwarp` command line tool. This avoids spawning an
 * external process and gives direct progress/cancellation control.
 *
 * JSON parameters:
 *   - input            : input raster path (mandatory)
 *   - output           : output raster path (mandatory)
 *   - dem              : DEM raster path (optional)
 *   - dstCrs           : target CRS as EPSG code or WKT (optional)
 *   - resampling       : nearest|bilinear|cubic|cubicspline|lanczos (default: bilinear)
 *   - targetResolution : output pixel size in CRS units (optional)
 *   - nodata           : output no-data value (optional)
 *   - height           : constant elevation fallback in meters (default: 0)
 */
class GdalOrthorectificationOperator : public RSOperator {
public:
    std::string name() const override { return "gdal:orthorectification"; }
    std::string displayName() const override { return "GDAL Orthorectification"; }
    std::string group() const override { return "gdal-geometry"; }
    std::string description() const override {
        return "Orthorectify a raster with RPC/GCP metadata using GDAL (optionally with a DEM).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::gdal
