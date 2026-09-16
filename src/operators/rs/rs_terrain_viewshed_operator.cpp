/***************************************************************************
 * rs_terrain_viewshed_operator.cpp — see the header for contracts.
 ***************************************************************************/
#include "rs_terrain_viewshed_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_viewshed.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <gdal.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "rs_terrain_guard.h"

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "viewshed", "cumulative" };
constexpr std::size_t kMaxObservers = 64;

std::vector<std::pair<int, int>> parseCellPairs( const std::string &text,
                                                 const std::string &paramName )
{
    std::vector<std::pair<int, int>> points;
    std::string token;
    auto flush = [&]() {
        if ( token.empty() )
            return;
        const auto comma = token.find( ',' );
        if ( comma == std::string::npos )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   paramName + " entries must be 'col,row' pairs, got '"
                                       + token + "'" );
        try
        {
            const int col = std::stoi( token.substr( 0, comma ) );
            const int row = std::stoi( token.substr( comma + 1 ) );
            points.emplace_back( col, row );
        }
        catch ( const std::exception & )
        {
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   paramName + " entries must be integer 'col,row' "
                                              "pairs, got '"
                                       + token + "'" );
        }
        token.clear();
    };
    for ( const char c : text )
    {
        if ( c == ';' || c == ' ' )
            flush();
        else
            token.push_back( c );
    }
    flush();
    return points;
}

} // anonymous namespace

Json::Value RsTerrainViewshedOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "DEM raster (projected CRS recommended)" );
    props["output"] = makeOutputParam( "output", "Output visibility raster", "tif" );
    props["product"] = makeEnumParam( "product", "Visibility product", s_products, "viewshed" );
    props["observer"] = makeStringParam( "observer",
        "Single observer as 'col,row' (zero-based pixels); required for product=viewshed", "" );
    props["observers"] = makeStringParam( "observers",
        "Observers as 'col,row' pairs separated by ';' or spaces (max 64); "
        "required for product=cumulative", "" );
    props["observer_height"] = makeNumberParam( "observer_height",
        "Observer eye height above the DEM surface (metres)", 1.7 );
    props["target_height"] = makeNumberParam( "target_height",
        "Target height above the DEM surface (metres)", 0.0 );
    props["radius"] = makeNumberParam( "radius",
        "Analysis radius in map units; 0 = full frame", 0.0 );
    props["curvature"] = makeEnumParam( "curvature",
        "Apply Earth-curvature/refraction correction (requires projected CRS)",
        { "false", "true" }, "false" );
    props["refraction_k"] = makeNumberParam( "refraction_k",
        "Atmospheric refraction coefficient k (0.13 standard)", 0.13 );
    props["nodata"] = makeNumberParam( "nodata",
        "DEM NoData value; undeclared bands keep NaN as the missing marker", -9999.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output visibility raster" );
    outputs["product"] = makeStringParam( "product", "Computed product", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "product" } );
    return root;
}

Json::Value RsTerrainViewshedOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "terrain" );
    meta["tags"].append( "visibility" );
    meta["tags"].append( "viewshed" );
    meta["task"] = "visibility";
    meta["notes"] = "Ring-sweep R3-family viewshed (deterministic, permissive "
                    "merge). NoData cells are opaque: rays stop at them. "
                    "Curvature correction lowers elevations by "
                    "(1-k)*d^2/(2R) and is exact per LOS pair.";
    meta["gpu"] = false;
    meta["purpose"] = "Intervisibility, broadcast/watchtower siting, "
                      "multi-observer coverage over terrain.";
    meta["prerequisites"].append( "Projected (metric) DEM recommended; curvature "
                                  "correction refuses geographic CRS." );
    meta["workflowHints"].append( "Combine with rs:terrain_analysis slope to "
                                  "explain hidden areas; use cumulative for "
                                  "watchtower networks." );
    meta["limitations"].append( "Full-frame memory (~22 bytes/cell); capped by "
                                "SICNU_TERRAIN_MAX_CELLS (2^28 cells default)." );
    meta["limitations"].append( "Permissive R3 merge may slightly overestimate "
                                "visibility on convex ridgelines." );
    meta["limitations"].append( "Observers on NoData are refused; rays stop at "
                                "NoData cells." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    return meta;
}

Json::Value RsTerrainViewshedOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    // Working set ≈ 22 bytes/cell (za + ring + dist + horizon + mask + CSR);
    // stated per 4096² as the documented scale anchor.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        22ULL * 4096ULL * 4096ULL );
    return est;
}

Json::Value RsTerrainViewshedOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                  static_cast<std::uint64_t>( std::max( 1, probe.height() ) ),
                  22ULL } );
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

Json::Value RsTerrainViewshedOperator::run( const Json::Value &params,
                                            RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string product = getEnum( params, "product", s_products, "viewshed" );

    const bool wantCurvature = getEnum( params, "curvature", { "false", "true" }, "false" )
                               == "true";
    double refractionK = getDouble( params, "refraction_k", 0.13 );
    if ( !std::isfinite( refractionK ) || refractionK < 0.0 || refractionK >= 1.0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "refraction_k must be in [0, 1)" );
    const double observerHeight = getDouble( params, "observer_height", 1.7 );
    const double targetHeight = getDouble( params, "target_height", 0.0 );
    const double radius = getDouble( params, "radius", 0.0 );
    if ( !std::isfinite( observerHeight ) || observerHeight < 0.0
         || !std::isfinite( targetHeight ) || targetHeight < 0.0
         || !std::isfinite( radius ) || radius < 0.0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "heights and radius must be finite and non-negative" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open DEM raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM raster is empty: " + inputPath );
    if ( terrainExceedsCellBudget( width, height ) )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "DEM exceeds the full-frame cell budget (" + std::to_string( terrainMaxCells() )
                + " cells); raise SICNU_TERRAIN_MAX_CELLS or tile the analysis" );

    // Cell size from the geotransform; refuse curvature on geographic CRS.
    const std::array<double, 6> gt = ds.geoTransform();
    const double cellSizeX = std::abs( gt[1] );
    const double cellSizeY = std::abs( gt[5] );
    if ( cellSizeX <= 0.0 || cellSizeY <= 0.0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM has a degenerate geotransform" );
    const QString projection = ds.projection();
    if ( wantCurvature && projection.contains( "GEOGCS" ) && !projection.contains( "PROJCS" ) )
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "curvature correction requires a projected (metric) CRS; the input is geographic" );
    const double curvatureFactor =
        wantCurvature ? ( 1.0 - refractionK ) / ( 2.0 * 6371000.0 ) : 0.0;

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

    const size_t n = static_cast<size_t>( width ) * height;
    std::vector<float> dem( n );
    if ( !ds.readBandWindow( 1, 0, 0, width, height, dem.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read DEM" );
    context.throwIfCancelled();

    // Collect observers.
    std::vector<TerrainVisibility::ViewshedParams> observers;
    if ( product == "viewshed" )
    {
        const std::string observerParam = params.isMember( "observer" )
                                              && params["observer"].isString()
                                          ? params["observer"].asString()
                                          : std::string();
        if ( observerParam.empty() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "product=viewshed requires 'observer' ('col,row')" );
        for ( const auto &[col, row] : parseCellPairs( observerParam, "observer" ) )
        {
            TerrainVisibility::ViewshedParams p;
            p.obsCol = col;
            p.obsRow = row;
            p.observerHeight = observerHeight;
            p.targetHeight = targetHeight;
            p.radius = radius;
            p.curvatureFactor = curvatureFactor;
            observers.push_back( p );
        }
    }
    else
    {
        const std::string observersParam = params.isMember( "observers" )
                                               && params["observers"].isString()
                                           ? params["observers"].asString()
                                           : std::string();
        if ( observersParam.empty() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "product=cumulative requires 'observers' "
                                   "('col,row' pairs separated by ';' or spaces)" );
        for ( const auto &[col, row] : parseCellPairs( observersParam, "observers" ) )
        {
            TerrainVisibility::ViewshedParams p;
            p.obsCol = col;
            p.obsRow = row;
            p.observerHeight = observerHeight;
            p.targetHeight = targetHeight;
            p.radius = radius;
            p.curvatureFactor = curvatureFactor;
            observers.push_back( p );
        }
        if ( observers.size() > kMaxObservers )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "at most " + std::to_string( kMaxObservers )
                                       + " observers are supported (got "
                                       + std::to_string( observers.size() ) + ")" );
    }
    if ( observers.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "parsed to no observers" );

    const auto cancelledHook = [&context] { return context.isCancelled(); };
    context.reportProgress( 0.3, "Computing viewshed" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["product"] = product;
    result["width"] = width;
    result["height"] = height;
    result["observerCount"] = static_cast<Json::UInt64>( observers.size() );
    result["curvatureFactor"] = curvatureFactor;

    const GDALDataType outType = product == "viewshed" ? GDT_Byte : GDT_UInt16;
    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, outType,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );

    bool writeOk = false;
    if ( product == "viewshed" )
    {
        std::vector<std::uint8_t> visible( n );
        if ( !TerrainVisibility::viewshedR3( dem.data(), width, height, nodata, cellSizeX,
                                             cellSizeY, observers.front(), &visible,
                                             255, cancelledHook ) )
        {
            out.abandon();
            context.throwIfCancelled(); // cancelled() hooks exit false first
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Viewshed failed (observer outside the grid or "
                                   "on NoData?)" );
        }
        Json::UInt64 visibleCount = 0;
        for ( const std::uint8_t v : visible )
            visibleCount += v ? 1 : 0;
        result["visibleCells"] = visibleCount;
        result["visibleFraction"] =
            static_cast<double>( visibleCount ) / static_cast<double>( n );
        out.setNoDataValue( 255.0 );
        writeOk = out.writeTileRaw(
            1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
            visible.data(), GDT_Byte );
    }
    else
    {
        std::vector<std::uint16_t> counts( n );
        if ( !TerrainVisibility::cumulativeViewshed( dem.data(), width, height, nodata,
                                                     cellSizeX, cellSizeY, observers,
                                                     &counts, 65535, cancelledHook ) )
        {
            out.abandon();
            context.throwIfCancelled(); // cancelled() hooks exit false first
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Cumulative viewshed failed (observer outside the "
                                   "grid or on NoData?)" );
        }
        Json::UInt64 anyVisible = 0;
        for ( const std::uint16_t v : counts )
            anyVisible += v > 0 ? 1 : 0;
        result["cellsWithAnyObserver"] = anyVisible;
        out.setNoDataValue( 65535.0 );
        writeOk = out.writeTileRaw(
            1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
            counts.data(), GDT_UInt16 );
    }
    if ( !writeOk )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
    }
    context.reportProgress( 0.9, "Writing output raster" );
    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    context.reportProgress( 1.0, "Terrain viewshed complete" );
    return result;
}

} // namespace sicnu::operators::rs
