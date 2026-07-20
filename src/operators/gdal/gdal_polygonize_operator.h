/***************************************************************************
 * gdal_polygonize_operator.h  —  Raster → polygon vectorization
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::gdal {

/**
 * gdal:polygonize
 *
 * Convert a single-band integer/float raster (class map or segment labels)
 * into polygons via GDALPolygonize. Writes ESRI Shapefile by default.
 *
 * Parameters:
 *   input      (string, required)  Label / class raster
 *   output     (string, required)  Output vector path (.shp / .gpkg)
 *   band       (int, optional)     1-based band (default 1)
 *   field      (string, optional)  Attribute field name (default "DN")
 *   connected8 (bool, optional)    8-connected (default true)
 *
 * Returns: output, features
 */
class GdalPolygonizeOperator : public RSOperator {
public:
    std::string name() const override { return "gdal:polygonize"; }
    std::string displayName() const override { return "GDAL Polygonize"; }
    std::string group() const override { return "gdal-vectorize"; }
    std::string description() const override {
        return "Convert a single-band label/class raster into polygons (GDALPolygonize).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::gdal
