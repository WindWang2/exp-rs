/***************************************************************************
 * rs_terrain_landform_operator.cpp — see the header for contracts.
 ***************************************************************************/
#include "rs_terrain_landform_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_landform.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <gdal.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "rs_terrain_guard.h"

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "tpi_multiscale", "landform_class",
                                              "geomorphon" };
constexpr std::size_t kMaxScales = 16;

std::vector<int> parseRadii( const std::string &text )
{
    std::vector<int> radii;
    std::string token;
    auto flush = [&]() {
        if ( token.empty() )
            return;
        try
        {
            const int r = std::stoi( token );
            if ( r <= 0 )
                throw std::invalid_argument( "non-positive" );
            radii.push_back( r );
        }
        catch ( const std::exception & )
        {
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "radii entries must be positive integers, got '"
                                       + token + "'" );
        }
        token.clear();
    };
    for ( const char c : text )
    {
        if ( c == ',' || c == ';' || c == ' ' )
            flush();
        else
            token.push_back( c );
    }
    flush();
    return radii;
}

} // anonymous namespace

Json::Value RsTerrainLandformOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "DEM raster (projected CRS recommended)" );
    props["output"] = makeOutputParam( "output", "Output raster", "tif" );
    props["product"] = makeEnumParam( "product", "Landform product", s_products,
                                      "geomorphon" );
    props["radii"] = makeStringParam( "radii",
        "tpi_multiscale: window radii in cells, separated by ',' (max 16)", "3,8,21" );
    props["inner_radius"] = makeNumberParam( "inner_radius",
        "landform_class: inner TPI radius in cells", 3.0 );
    props["outer_radius"] = makeNumberParam( "outer_radius",
        "landform_class: outer TPI radius in cells", 15.0 );
    props["flat_slope_deg"] = makeNumberParam( "flat_slope_deg",
        "landform_class: slope below this (degrees) maps to plains", 5.0 );
    props["search_radius"] = makeNumberParam( "search_radius",
        "geomorphon: search radius in cells", 20.0 );
    props["flat_radius"] = makeNumberParam( "flat_radius",
        "geomorphon: cells skipped before the flatness comparison", 2.0 );
    props["flat_thresh_deg"] = makeNumberParam( "flat_thresh_deg",
        "geomorphon: |angle| below this (degrees) counts as flat", 2.0 );
    props["nodata"] = makeNumberParam( "nodata",
        "DEM NoData value; undeclared bands keep NaN as the missing marker", -9999.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster" );
    outputs["product"] = makeStringParam( "product", "Computed product", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "product" } );
    return root;
}

Json::Value RsTerrainLandformOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "terrain" );
    meta["tags"].append( "landform" );
    meta["tags"].append( "geomorphometry" );
    meta["task"] = "landform_classification";
    meta["notes"] = "Multiscale TPI uses square (2r+1)² windows (centre "
                    "excluded). Weiss classes: 0 plains, 1 valley, 2 lower "
                    "slope, 3 middle slope, 4 upper slope, 5 peak. Geomorphon "
                    "form classes: 0 flat, 1 peak, 2 pit, 3 ridge, 4 valley, "
                    "5 slope, 6 other (v1 subset of the 10-class J&S table).";
    meta["gpu"] = false;
    meta["purpose"] = "Landform segmentation and terrain-position mapping.";
    meta["prerequisites"].append( "Projected (metric) DEM recommended; radii are "
                                  "in cells." );
    meta["workflowHints"].append( "Start with geomorphon for a form map, then "
                                  "tpi_multiscale to tune scale pairs for "
                                  "landform_class." );
    meta["limitations"].append( "Full-frame memory: peak ≈ 24 + 8·scales bytes/"
                                "cell (integral images + retained per-scale TPI); "
                                "capped by SICNU_TERRAIN_MAX_CELLS." );
    meta["limitations"].append( "Square TPI windows; geomorphon uses nearest-cell "
                                "line-of-sight scans." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    return meta;
}

Json::Value RsTerrainLandformOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 48ULL * 4096ULL * 4096ULL );
    return est;
}

Json::Value RsTerrainLandformOperator::estimateExecution( const Json::Value &params ) const {
    // Peak working set: transient integral-image triple (~24 B/cell) plus the
    // retained per-scale TPI/stdTPI pair (8 B/cell × scale count) — the
    // scales dominate, so the estimate must track the parsed radii.
    std::uint64_t bytesPerCell = 48ULL; // default "3,8,21" anchor
    if ( params.isObject() && params.isMember( "radii" ) && params["radii"].isString() )
    {
        try
        {
            const std::string text = params["radii"].asString();
            std::size_t count = 1;
            for ( const char c : text )
                if ( c == ',' || c == ';' || c == ' ' )
                    ++count;
            count = std::clamp( count, std::size_t{ 1 }, kMaxScales );
            bytesPerCell = 24ULL + 8ULL * count;
        }
        catch ( ... )
        {
            // fall through to the anchor
        }
    }
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                  static_cast<std::uint64_t>( std::max( 1, probe.height() ) ),
                  bytesPerCell } );
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
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        bytesPerCell * 4096ULL * 4096ULL );
    return est;
}

Json::Value RsTerrainLandformOperator::run( const Json::Value &params,
                                            RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string product = getEnum( params, "product", s_products, "geomorphon" );

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

    const std::array<double, 6> gt = ds.geoTransform();
    const double cellSizeX = std::abs( gt[1] );
    const double cellSizeY = std::abs( gt[5] );
    if ( cellSizeX <= 0.0 || cellSizeY <= 0.0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM has a degenerate geotransform" );

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

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["product"] = product;
    result["width"] = width;
    result["height"] = height;

    const auto cancelledHook = [&context] { return context.isCancelled(); };
    context.reportProgress( 0.3, "Computing " + product );

    // Parse the scale set first so the output is created once with the right
    // band count.
    std::vector<int> radii;
    if ( product == "tpi_multiscale" )
    {
        const std::string radiiParam =
            params.isMember( "radii" ) && params["radii"].isString()
                ? params["radii"].asString()
                : std::string( "3,8,21" );
        radii = parseRadii( radiiParam );
        if ( radii.empty() )
            throw RSOperatorError( ErrorCode::InvalidParameter, "radii parsed to nothing" );
        if ( radii.size() > kMaxScales )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "at most " + std::to_string( kMaxScales )
                                       + " scales supported" );
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height,
                             product == "tpi_multiscale" ? static_cast<int>( radii.size() )
                                                         : 1,
                             GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setNoDataValue( hasNodata ? static_cast<double>( nodata )
                                  : std::numeric_limits<double>::quiet_NaN() );

    if ( product == "tpi_multiscale" )
    {
        std::vector<TerrainLandform::MultiscaleTPI> scales;
        if ( !TerrainLandform::tpiMultiscale( dem.data(), width, height, nodata, radii,
                                              &scales, cancelledHook ) )
        {
            out.abandon();
            context.throwIfCancelled(); // cancelled() hooks exit false first
            throw RSOperatorError( ErrorCode::ComputationError, "Multiscale TPI failed" );
        }
        for ( std::size_t s = 0; s < scales.size(); ++s )
        {
            if ( !out.writeTile(
                     static_cast<int>( s ) + 1,
                     GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                     scales[s].tpi.data() ) )
            {
                out.abandon();
                throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
            }
        }
        QString closeError;
        if ( !out.closeWithError( &closeError ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to finalize output: " + closeError.toStdString() );
        Json::Value radiiJson( Json::arrayValue );
        for ( const int r : radii )
            radiiJson.append( r );
        result["radii"] = radiiJson;
    }
    else
    {
        std::vector<float> productData( n, 0.0f );
        if ( product == "landform_class" )
        {
            TerrainLandform::LandformClassParams p;
            const int inner = static_cast<int>( getDouble( params, "inner_radius", 3.0 ) );
            const int outer = static_cast<int>( getDouble( params, "outer_radius", 15.0 ) );
            if ( inner <= 0 || outer <= 0 )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "inner_radius/outer_radius must be positive" );
            const double flatSlope = getDouble( params, "flat_slope_deg", 5.0 );
            if ( !std::isfinite( flatSlope ) || flatSlope < 0.0 || flatSlope >= 90.0 )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "flat_slope_deg must be in [0, 90)" );
            p.innerRadiusCells = inner;
            p.outerRadiusCells = outer;
            p.flatSlopeDeg = flatSlope;
            std::vector<std::uint8_t> classes;
            if ( !TerrainLandform::landformClasses( dem.data(), width, height, nodata,
                                                    cellSizeX, cellSizeY, p, &classes,
                                                    cancelledHook ) )
            {
                out.abandon();
                context.throwIfCancelled(); // cancelled() hooks exit false first
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "Landform classification failed" );
            }
            Json::Value histogram( Json::objectValue );
            for ( const std::uint8_t c : classes )
            {
                if ( c == 255 )
                    continue;
                const std::string key = std::to_string( static_cast<int>( c ) );
                histogram[key] = histogram[key].asUInt64() + 1;
            }
            result["classHistogram"] = histogram;
            for ( std::size_t i = 0; i < n; ++i )
                productData[i] = classes[i] == 255 ? nodata : static_cast<float>( classes[i] );
        }
        else // geomorphon
        {
            TerrainLandform::GeomorphonParams p;
            const int search = static_cast<int>( getDouble( params, "search_radius", 20.0 ) );
            const int flat = static_cast<int>( getDouble( params, "flat_radius", 2.0 ) );
            const double thresh = getDouble( params, "flat_thresh_deg", 2.0 );
            if ( search <= 0 || flat < 0 || !std::isfinite( thresh ) || thresh < 0.0
                 || thresh >= 90.0 )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "invalid geomorphon parameters" );
            p.searchRadiusCells = search;
            p.flatRadiusCells = flat;
            p.flatThreshDeg = thresh;
            TerrainLandform::GeomorphonResult geom;
            if ( !TerrainLandform::geomorphon( dem.data(), width, height, nodata, cellSizeX,
                                               cellSizeY, p, &geom, cancelledHook ) )
            {
                out.abandon();
                context.throwIfCancelled(); // cancelled() hooks exit false first
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "Geomorphon failed" );
            }
            Json::Value histogram( Json::objectValue );
            for ( const std::uint8_t f : geom.form )
            {
                if ( f == 255 )
                    continue;
                const std::string key = std::to_string( static_cast<int>( f ) );
                histogram[key] = histogram[key].asUInt64() + 1;
            }
            result["formHistogram"] = histogram;
            for ( std::size_t i = 0; i < n; ++i )
                productData[i] = geom.form[i] == 255 ? nodata : static_cast<float>( geom.form[i] );
        }
        if ( !out.writeTile(
                 1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                 productData.data() ) )
        {
            out.abandon();
            throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
        }
        QString closeError;
        if ( !out.closeWithError( &closeError ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to finalize output: " + closeError.toStdString() );
    }

    context.reportProgress( 1.0, "Terrain landform complete" );
    return result;
}

} // namespace sicnu::operators::rs
