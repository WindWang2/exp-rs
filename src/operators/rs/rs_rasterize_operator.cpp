/***************************************************************************
 * rs_rasterize_operator.cpp — vector → raster burn through the shared
 * windowed rasterization seam (Scientific Processing 8.0, package D).
 ***************************************************************************/
#include "rs_rasterize_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_raster_vector.h"
#include "geospatial/crs/crs_policy.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kWindowDim = 256;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

} // anonymous namespace

Json::Value RsRasterizeOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Reference raster — donates the output grid, CRS and geotransform" );
    props["vector"] = makeVectorParam( "vector", "Vector dataset whose geometries are burned" );
    props["layer"] = makeStringParam( "layer", "Layer name or 0-based index (default first layer)", "" );
    props["field"] = makeStringParam( "field", "Numeric attribute to burn (default: constant value)", "" );
    props["value"] = makeNumberParam( "value", "Constant burn value when no field is given", 1.0 );
    props["allTouched"] = makeBooleanParam( "allTouched", "Burn every touched pixel instead of pixel-center selection", false );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "vector", "output" } );
    return root;
}

Json::Value RsRasterizeOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "raster-vector" );
    meta["tags"].append( "rasterize" );
    meta["tags"].append( "burn" );
    meta["task"] = "raster-vector";
    meta["gpu"] = false;
    meta["purpose"] = "Convert vector zones/annotations into a raster mask or "
                      "attribute grid on an existing reference grid.";
    meta["prerequisites"].append( "Reference raster carries a CRS and a north-up geotransform." );
    meta["prerequisites"].append( "Vector features carry geometry; zone/attribute fields are numeric when used as burn values." );
    meta["limitations"].append( "Overlapping geometries: last feature wins (input order)." );
    meta["limitations"].append( "Unburned pixels are NaN (declared NoData) — not zero." );
    meta["limitations"].append( "The feature set is byte-budgeted (256 MiB); subset larger vectors first." );
    return meta;
}

Json::Value RsRasterizeOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kWindowDim;
    est["tileHeight"] = kWindowDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        4ULL * kWindowDim * kWindowDim * sizeof( float ) );
    return est;
}

Json::Value RsRasterizeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string vectorPath = requireString( params, "vector" );
    const std::string outputPath = requireString( params, "output" );
    const std::string layer = getString( params, "layer", "" );
    const std::string field = getString( params, "field", "" );
    const double value = getDouble( params, "value", 1.0 );
    const bool allTouched = getBool( params, "allTouched", false );

    ensureGdalInit();

    GdalDatasetWrapper refDs;
    if ( !refDs.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open reference raster: " + inputPath );
    const int width = refDs.width();
    const int height = refDs.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Reference raster is empty: " + inputPath );
    if ( refDs.projection().isEmpty() || !refDs.hasGeoTransform() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Reference raster must carry a CRS and geotransform: " + inputPath );
    const std::array<double, 6> gt = refDs.geoTransform();
    if ( gt[2] != 0.0 || gt[4] != 0.0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Reference raster geotransform is rotated; the raster-vector "
                               "family requires north-up grids" );

    const sicnu::geo::Crs crs = [this, &refDs, &inputPath]() {
        try
        {
            return sicnu::geo::Crs::fromWkt( refDs.projection().toStdString() );
        }
        catch ( const sicnu::geo::GeoError &e )
        {
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Reference raster CRS is unusable: " + std::string( e.what() )
                                       + " (" + inputPath + ")" );
        }
    }();

    RasterGrid grid;
    grid.width = width;
    grid.height = height;
    grid.gt = gt;

    FeatureLoadSpec spec;
    spec.vectorPath = QString::fromStdString( vectorPath );
    spec.layerSelector = QString::fromStdString( layer );
    spec.valueField = QString::fromStdString( field );
    spec.constantValue = value;

    context.reportProgress( 0.05, "Loading vector features" );
    FeatureCache cache = loadFeatureCache( spec, grid, crs );

    // Output on the reference grid; unburned pixels stay NaN (declared).
    GdalDatasetWrapper out;
    QString createError;
    if ( !out.create( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                      gt, refDs.projection(), &createError ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath + " - "
                                   + createError.toStdString() );
    out.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );

    // Window sweep: windows that no cached feature touches keep NaN.
    std::uint64_t burned = 0;
    std::uint64_t windowsTouched = 0;
    const int windowsX = ( width + kWindowDim - 1 ) / kWindowDim;
    const int windowsY = ( height + kWindowDim - 1 ) / kWindowDim;
    const int totalWindows = windowsX * windowsY;
    int windowIndex = 0;
    for ( int wy = 0; wy < windowsY; ++wy )
    {
        for ( int wx = 0; wx < windowsX; ++wx )
        {
            context.throwIfCancelled();
            ++windowIndex;
            const int xOff = wx * kWindowDim;
            const int yOff = wy * kWindowDim;
            const int winW = std::min( kWindowDim, width - xOff );
            const int winH = std::min( kWindowDim, height - yOff );

            // Features whose pixel bounds intersect this window.
            std::vector<OGRGeometryH> geoms;
            std::vector<double> values;
            for ( const CachedFeature &f : cache.features )
            {
                if ( f.geometry == nullptr || !f.intersects )
                    continue;
                const bool overlap = f.bounds[0] < xOff + winW && f.bounds[2] > xOff
                                     && f.bounds[1] < yOff + winH && f.bounds[3] > yOff;
                if ( !overlap )
                    continue;
                geoms.push_back( f.geometry );
                values.push_back( f.burnValue );
            }
            if ( geoms.empty() )
            {
                context.reportProgress( 0.9 * windowIndex / totalWindows, "Rasterizing" );
                continue;
            }
            ++windowsTouched;

            GDALDatasetH mem = createMemWindow( winW, winH );
            if ( mem == nullptr )
            {
                out.close();
                throw RSOperatorError( ErrorCode::GdalError, "Failed to create the MEM rasterization window" );
            }
            const auto geo = grid.windowGeoBounds( xOff, yOff, winW, winH );
            if ( !rasterizeWindow( mem, 1, geoms, values, geo[0], geo[3], gt[1], gt[5], allTouched ) )
            {
                GDALClose( mem );
                out.close();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "GDALRasterizeGeometries failed on window ("
                                           + std::to_string( xOff ) + ", " + std::to_string( yOff )
                                           + ")" );
            }
            std::vector<float> pixels( static_cast<size_t>( winW ) * winH );
            if ( !readMemWindow( mem, 1, winW, winH, pixels.data() ) )
            {
                GDALClose( mem );
                out.close();
                throw RSOperatorError( ErrorCode::GdalError, "Failed to read back the rasterized window" );
            }
            GDALClose( mem );
            for ( float v : pixels )
                if ( std::isfinite( v ) )
                    ++burned;
            if ( !out.writeBandWindow( 1, xOff, yOff, winW, winH, pixels.data() ) )
            {
                out.close();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write output window (" + std::to_string( xOff )
                                           + ", " + std::to_string( yOff ) + ")" );
            }
            context.reportProgress( 0.9 * windowIndex / totalWindows, "Rasterizing" );
        }
    }

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    const std::uint64_t total = static_cast<std::uint64_t>( width ) * height;
    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["width"] = width;
    result["height"] = height;
    result["burnedPixels"] = Json::Value::UInt64( burned );
    result["totalPixels"] = Json::Value::UInt64( total );
    result["burnedFraction"] = total > 0 ? static_cast<double>( burned ) / total : 0.0;
    result["features"] = Json::Value::UInt64( cache.features.size() );
    result["geometrylessFeatures"] = Json::Value::UInt64( cache.geometryless );
    result["outsideGridFeatures"] = Json::Value::UInt64( cache.outsideGrid );
    result["allTouched"] = allTouched;
    context.reportProgress( 1.0, "Rasterize complete" );
    return result;
}

} // namespace sicnu::operators::rs
