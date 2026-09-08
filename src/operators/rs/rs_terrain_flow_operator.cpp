/***************************************************************************
 * rs_terrain_flow_operator.cpp — Milestone F (fill / D8 / accumulation)
 ***************************************************************************/
#include "rs_terrain_flow_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

#include <QString>

#include <gdal.h>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "fill", "flow_direction", "flow_accumulation" };

} // anonymous namespace

Json::Value RsTerrainFlowOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "DEM raster" );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["product"] = makeEnumParam( "product", "Hydrology product", s_products, "flow_accumulation" );
    props["nodata"] = makeNumberParam( "nodata", "DEM NoData value; undeclared bands keep NaN as the missing marker", -9999.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["product"] = makeStringParam( "product", "Computed product", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "product" } );
    return root;
}

Json::Value RsTerrainFlowOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "terrain" );
    meta["tags"].append( "hydrology" );
    meta["tags"].append( "flow" );
    meta["task"] = "hydrology";
    meta["notes"] = "Priority-flood filling (NoData cells are barriers, never "
                    "routed across); D8 codes are the ESRI powers of two with "
                    "0 = sink; accumulation counts include the cell itself. "
                    "Full-frame kernel contract (O(N log N)).";
    meta["gpu"] = false;
    meta["purpose"] = "Prepare depression-free surfaces and drainage networks for watershed analysis.";
    meta["prerequisites"].append( "Projected DEM recommended; routing is cell-based (orthogonal 1, diagonal sqrt(2))." );
    meta["workflowHints"].append( "Run 'fill' first, then flow products on a filled surface; threshold accumulation for stream networks." );
    meta["limitations"].append( "Filled flats are sinks (direction 0); no flat-resolution routing is attempted (documented debt for a future epsilon-gradient variant)." );
    meta["limitations"].append( "Full-frame memory: the DEM and two working frames are resident; the estimate states the linear bound." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    return meta;
}

Json::Value RsTerrainFlowOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    // 3 full frames (input read + filled + directions) + accumulation, floats;
    // stated per 4096² as the documented scale anchor.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        4ULL * 4096ULL * 4096ULL * sizeof( float ) );
    return est;
}

Json::Value RsTerrainFlowOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                  static_cast<std::uint64_t>( std::max( 1, probe.height() ) ),
                  4ULL, static_cast<std::uint64_t>( sizeof( float ) ) } );
            if ( ram )
            {
                Json::Value est( Json::objectValue );
                est["tileWidth"] = 0;
                est["tileHeight"] = 0;
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
                return est;
            }
        }
    }
    return executionEstimate();
}

Json::Value RsTerrainFlowOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string product = getEnum( params, "product", s_products, "flow_accumulation" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open DEM raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM raster is empty: " + inputPath );
    const size_t n = static_cast<size_t>( width ) * height;

    // Missing marker: declared sentinel, else NaN (undeclared bands normalize
    // sentinel checks away).
    bool hasNodata = false;
    double nodataD = 0.0;
    if ( params.isMember( "nodata" ) && params["nodata"].isNumeric() )
    {
        hasNodata = true;
        nodataD = params["nodata"].asDouble();
    }
    else
    {
        nodataD = ds.bandNoDataValue( 1, &hasNodata );
        if ( hasNodata && !std::isfinite( nodataD ) )
            hasNodata = false;
    }
    const float nodata = hasNodata ? static_cast<float>( nodataD )
                                   : std::numeric_limits<float>::quiet_NaN();

    std::vector<float> dem( n );
    if ( !ds.readBandWindow( 1, 0, 0, width, height, dem.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read DEM" );
    context.throwIfCancelled();

    std::vector<float> filled( n );
    if ( product == "fill" || product == "flow_direction" || product == "flow_accumulation" )
    {
        context.reportProgress( 0.1, "Filling depressions" );
        if ( !TerrainFlow::fillDepressions( dem.data(), filled.data(), width, height, nodata ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Depression filling failed" );
    }

    std::vector<float> dir;
    if ( product == "flow_direction" || product == "flow_accumulation" )
    {
        context.reportProgress( 0.5, "Computing D8 directions" );
        dir.resize( n );
        if ( !TerrainFlow::flowDirections( filled.data(), dir.data(), width, height, nodata ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Flow direction routing failed" );
    }

    std::vector<float> productData;
    if ( product == "fill" )
        productData = std::move( filled );
    else if ( product == "flow_direction" )
        productData = std::move( dir );
    else
    {
        context.reportProgress( 0.8, "Accumulating drainage" );
        productData.assign( n, 0.0f );
        if ( !TerrainFlow::flowAccumulation( dir.data(), productData.data(), width, height ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Flow accumulation failed" );
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( hasNodata ? static_cast<double>( nodata )
                                  : std::numeric_limits<double>::quiet_NaN() );
    if ( !out.writeTile( 1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                         productData.data() ) )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
    }

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["product"] = product;
    result["width"] = width;
    result["height"] = height;
    context.reportProgress( 1.0, "Terrain flow complete" );
    return result;
}

} // namespace sicnu::operators::rs
