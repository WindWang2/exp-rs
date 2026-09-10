/***************************************************************************
 * rs_raster_vector.cpp — see rs_raster_vector.h
 ***************************************************************************/
#include "rs_raster_vector.h"

#include "operators/framework/rs_operator_error.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/vector/vector_reader.h"
#include "processing/gdal/gdal_dataset_wrapper.h" // ensureGdalInit

#include <gdal_alg.h>
#include <ogr_geometry.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

FeatureCache::~FeatureCache()
{
    for ( CachedFeature &f : features )
    {
        if ( f.geometry != nullptr )
            OGR_G_DestroyGeometry( f.geometry );
    }
}

FeatureCache loadFeatureCache( const FeatureLoadSpec &spec, const RasterGrid &grid,
                               const sicnu::geo::Crs &targetCrs )
{
    auto reader = sicnu::geo::VectorReader::open( spec.vectorPath.toStdString(),
                                                  spec.layerSelector.toStdString() );
    // Declared transform into the grid's CRS (foundation axis-order policy);
    // a missing layer CRS or missing transform path is a typed refusal.
    reader.setTargetCrs( targetCrs );

    std::set<std::string> projection;
    if ( !spec.valueField.isEmpty() )
        projection.insert( spec.valueField.toStdString() );
    if ( !spec.zoneField.isEmpty() )
        projection.insert( spec.zoneField.toStdString() );
    if ( !projection.empty() )
        reader.setAttributeProjection( { projection.begin(), projection.end() } );

    FeatureCache cache;
    std::vector<sicnu::geo::VectorFeature> batch;
    while ( reader.nextBatch( batch, 1024 ) )
    {
        for ( sicnu::geo::VectorFeature &feature : batch )
        {
            CachedFeature cf;
            cf.fid = feature.fid;

            // Zone key: the declared field (string verbatim, numeric in a
            // deterministic format) or the FID.
            if ( !spec.zoneField.isEmpty() )
            {
                const std::string field = spec.zoneField.toStdString();
                if ( !feature.attributes.isMember( field ) )
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "Feature FID " + std::to_string( feature.fid )
                            + " lacks the zone field '" + field + "'" );
                const Json::Value &v = feature.attributes[field];
                if ( v.isNumeric() )
                    cf.zoneKey = formatDouble( v.asDouble() );
                else if ( v.isString() )
                    cf.zoneKey = v.asString();
                else
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "Feature FID " + std::to_string( feature.fid )
                            + " has a non-scalar zone field '" + field + "'" );
            }
            else
            {
                cf.zoneKey = std::to_string( feature.fid );
            }

            // Burn value: declared numeric attribute or the constant.
            if ( !spec.valueField.isEmpty() )
            {
                const std::string field = spec.valueField.toStdString();
                if ( !feature.attributes.isMember( field ) )
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "Feature FID " + std::to_string( feature.fid )
                            + " lacks the burn field '" + field + "'" );
                const Json::Value &v = feature.attributes[field];
                if ( !v.isNumeric() )
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "Feature FID " + std::to_string( feature.fid )
                            + " has a non-numeric burn field '" + field
                            + "' - values are never coerced" );
                cf.burnValue = v.asDouble();
            }
            else
            {
                cf.burnValue = spec.constantValue;
            }

            if ( !feature.geometryWkt.empty() )
            {
                cf.geometry = geometryFromWkt( feature.geometryWkt );
                if ( cf.geometry == nullptr )
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "Feature FID " + std::to_string( feature.fid )
                            + " carries malformed geometry WKT" );
                double minX, minY, maxX, maxY;
                geometryEnvelope( cf.geometry, &minX, &minY, &maxX, &maxY );
                cf.intersects = boundsFromGeo( grid, minX, minY, maxX, maxY, &cf.bounds );
                if ( !cf.intersects )
                    ++cache.outsideGrid;
                cache.bytes += static_cast<size_t>( std::max( 0, OGR_G_WkbSize( cf.geometry ) ) );
            }
            else
            {
                ++cache.geometryless;
            }

            if ( cache.bytes > kFeatureCacheBytes )
                throw RSOperatorError(
                    ErrorCode::InvalidInputData,
                    "Vector feature set exceeds the " + std::to_string( kFeatureCacheBytes )
                        + "-byte cache budget - subset the dataset (spatial or attribute "
                          "filter) before rasterizing/statistics" );
            cache.features.push_back( std::move( cf ) );
        }
    }
    return cache;
}


std::array<double, 4> RasterGrid::windowGeoBounds( int xOff, int yOff, int winW, int winH ) const
{
    // North-up grid: gt[2] == gt[4] == 0 (checked by the operators).
    const double minX = gt[0] + xOff * gt[1];
    const double maxX = gt[0] + ( xOff + winW ) * gt[1];
    const double maxY = gt[3] + yOff * gt[5];
    const double minY = gt[3] + ( yOff + winH ) * gt[5];
    return { minX, minY, maxX, maxY };
}

bool boundsFromGeo( const RasterGrid &grid, double minX, double minY, double maxX, double maxY,
                    std::array<double, 4> *out )
{
    if ( out == nullptr || grid.width <= 0 || grid.height <= 0
         || !( std::isfinite( grid.gt[0] ) && std::isfinite( grid.gt[1] )
               && std::isfinite( grid.gt[3] ) && std::isfinite( grid.gt[5] )
               && grid.gt[1] != 0.0 && grid.gt[5] != 0.0 ) )
        return false;
    // Georeferenced envelope → continuous pixel coordinates (center
    // convention: pixel col spans [gt0 + col·px, gt0 + (col+1)·px)).
    const double col0 = ( minX - grid.gt[0] ) / grid.gt[1];
    const double col1 = ( maxX - grid.gt[0] ) / grid.gt[1];
    const double row0 = ( maxY - grid.gt[3] ) / grid.gt[5];
    const double row1 = ( minY - grid.gt[3] ) / grid.gt[5];
    const double minCol = std::min( col0, col1 );
    const double maxCol = std::max( col0, col1 );
    const double minRow = std::min( row0, row1 );
    const double maxRow = std::max( row0, row1 );
    // Misses the grid entirely?
    if ( maxCol < 0.0 || minCol > grid.width || maxRow < 0.0 || minRow > grid.height )
        return false;
    ( *out )[0] = std::max( 0.0, minCol );
    ( *out )[1] = std::max( 0.0, minRow );
    ( *out )[2] = std::min( static_cast<double>( grid.width ), maxCol );
    ( *out )[3] = std::min( static_cast<double>( grid.height ), maxRow );
    return true;
}

GDALDatasetH createMemWindow( int winW, int winH )
{
    ensureGdalInit();
    GDALDriverH mem = GDALGetDriverByName( "MEM" );
    if ( mem == nullptr )
        return nullptr;
    GDALDatasetH ds = GDALCreate( mem, "", winW, winH, 1, GDT_Float32, nullptr );
    if ( ds == nullptr )
        return nullptr;
    fillWindow( ds, 1, winW, winH, kNaN );
    return ds;
}

void fillWindow( GDALDatasetH dataset, int band, int winW, int winH, float value )
{
    if ( dataset == nullptr || winW <= 0 || winH <= 0 )
        return;
    std::vector<float> fill( static_cast<size_t>( winW ) * winH, value );
    GDALRasterIO( GDALGetRasterBand( dataset, band ), GF_Write, 0, 0, winW, winH,
                  fill.data(), winW, winH, GDT_Float32, 0, 0 );
}

bool rasterizeWindow( GDALDatasetH memDataset, int band,
                      const std::vector<OGRGeometryH> &geometries,
                      const std::vector<double> &burnValues,
                      double originX, double originY,
      double pixelSizeX, double pixelSizeY,
                      bool allTouched )
{
    if ( memDataset == nullptr || geometries.empty() || burnValues.size() != geometries.size() )
        return false;
    const double gt[6] = { originX, pixelSizeX, 0.0, originY, 0.0, pixelSizeY };
    GDALSetGeoTransform( memDataset, const_cast<double *>( gt ) );
    int bandList = band;
    char *options[2] = { nullptr, nullptr };
    char allTouchedOpt[] = "ALL_TOUCHED=YES";
    if ( allTouched )
        options[0] = allTouchedOpt;
    const CPLErr err = GDALRasterizeGeometries(
        memDataset, 1, &bandList,
        static_cast<int>( geometries.size() ), const_cast<OGRGeometryH *>( geometries.data() ),
        nullptr, nullptr, // identity transform: georeferenced coordinates
        const_cast<double *>( burnValues.data() ), options, nullptr, nullptr );
    return err == CE_None;
}

bool readMemWindow( GDALDatasetH dataset, int band, int winW, int winH, float *out )
{
    if ( dataset == nullptr || out == nullptr )
        return false;
    return GDALRasterIO( GDALGetRasterBand( dataset, band ), GF_Read, 0, 0, winW, winH,
                         out, winW, winH, GDT_Float32, 0, 0 )
           == CE_None;
}

OGRGeometryH geometryFromWkt( const std::string &wkt )
{
    if ( wkt.empty() )
        return nullptr;
    char *begin = const_cast<char *>( wkt.c_str() );
    OGRGeometryH geometry = nullptr;
    if ( OGR_G_CreateFromWkt( &begin, nullptr, &geometry ) != OGRERR_NONE )
        return nullptr;
    return geometry;
}

void geometryEnvelope( OGRGeometryH geometry, double *minX, double *minY,
                       double *maxX, double *maxY )
{
    OGREnvelope env;
    OGR_G_GetEnvelope( geometry, &env );
    *minX = env.MinX;
    *minY = env.MinY;
    *maxX = env.MaxX;
    *maxY = env.MaxY;
}

std::string formatDouble( double v )
{
    if ( std::isnan( v ) )
        return "nan";
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%.10g", v );
    return buf;
}

} // namespace sicnu::operators::rs
