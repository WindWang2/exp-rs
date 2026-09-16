/***************************************************************************
 * rs_terrain_flow_operator.cpp — Milestone F (fill / D8 / accumulation)
 ***************************************************************************/
#include "rs_terrain_flow_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/terrain_hydrology.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "rs_terrain_guard.h"

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "fill",
                                              "flow_direction",
                                              "flow_accumulation",
                                              "watershed",
                                              "flat_resolve",
                                              "flow_direction_inf",
                                              "stream_network",
                                              "outlets" };
constexpr std::size_t kMaxListedOutlets = 1000;
constexpr std::size_t kMaxListedSegments = 200;

} // anonymous namespace

Json::Value RsTerrainFlowOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "DEM raster" );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["product"] = makeEnumParam( "product", "Hydrology product", s_products, "flow_accumulation" );
    props["pour_points"] = makeStringParam( "pour_points",
        "Watershed outlets as 'col,row' pairs (zero-based pixels), separated by ';' or spaces; "
        "required for product=watershed. Output labels are 1-based in the given order, 0 = outside every basin.", "" );
    props["threshold"] = makeNumberParam( "threshold",
        "stream_network: minimum self-inclusive accumulation for a stream cell", 50.0 );
    props["include_segments"] = makeEnumParam( "include_segments",
        "stream_network: list Strahler link polylines (map coordinates) in the result",
        { "false", "true" }, "false" );
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
                    "flat_resolve adds epsilon-gradient flat resolution; "
                    "flow_direction_inf is D∞ (Tarboton) in degrees clockwise "
                    "from north; stream_network thresholds accumulation and "
                    "assigns Strahler orders; outlets lists D8 direction-0 "
                    "cells. Full-frame kernel contract (O(N log N)).";
    meta["gpu"] = false;
    meta["purpose"] = "Prepare depression-free surfaces and drainage networks for watershed analysis.";
    meta["prerequisites"].append( "Projected DEM recommended; routing is cell-based (orthogonal 1, diagonal sqrt(2))." );
    meta["workflowHints"].append( "Run 'fill' first, then flow products on a filled surface; threshold accumulation for stream networks." );
    meta["workflowHints"].append( "Use flat_resolve before flow_direction_inf so filled flats drain instead of reporting -1." );
    meta["limitations"].append( "stream_network/directions run on the filled surface of this run; D∞ accumulation uses the single steepest-facet receiver (no fraction splitting, documented follow-up)." );
    meta["limitations"].append( "Full-frame memory: the DEM and two working frames are resident; the estimate states the linear bound." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    return meta;
}

Json::Value RsTerrainFlowOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    // Up to 6 full float frames (input, filled, directions, accumulation,
    // network mask/orders) for the stream_network product; stated per 4096²
    // as the documented scale anchor.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        6ULL * 4096ULL * 4096ULL * sizeof( float ) );
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
                  6ULL, static_cast<std::uint64_t>( sizeof( float ) ) } );
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
    if ( terrainExceedsCellBudget( width, height ) )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "DEM exceeds the full-frame cell budget (" + std::to_string( terrainMaxCells() )
                + " cells); raise SICNU_TERRAIN_MAX_CELLS or tile the analysis" );
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
    if ( product == "fill" || product == "flow_direction" || product == "flow_accumulation"
         || product == "watershed" || product == "flat_resolve"
         || product == "flow_direction_inf" || product == "stream_network"
         || product == "outlets" )
    {
        context.reportProgress( 0.1, "Filling depressions" );
        if ( !TerrainFlow::fillDepressions( dem.data(), filled.data(), width, height, nodata ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Depression filling failed" );
    }

    std::vector<float> dir;
    if ( product == "flow_direction" || product == "flow_accumulation"
         || product == "watershed" || product == "stream_network" || product == "outlets" )
    {
        context.reportProgress( 0.5, "Computing D8 directions" );
        dir.resize( n );
        if ( !TerrainFlow::flowDirections( filled.data(), dir.data(), width, height, nodata ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Flow direction routing failed" );
    }

    std::vector<float> productData;
    Json::Value result( Json::objectValue );
    if ( product == "fill" )
        productData = std::move( filled );
    else if ( product == "flow_direction" )
        productData = std::move( dir );
    else if ( product == "watershed" )
    {
        // Parse pour points ('col,row' pairs separated by ';' or spaces).
        const std::string pourParam =
            params.isMember( "pour_points" ) && params["pour_points"].isString()
                ? params["pour_points"].asString()
                : std::string();
        if ( pourParam.empty() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "product=watershed requires 'pour_points' "
                                   "('col,row' pairs separated by ';' or spaces)" );
        std::vector<std::pair<int, int>> pourPoints;
        std::string token;
        auto flushToken = [&]() {
            if ( token.empty() )
                return;
            const auto comma = token.find( ',' );
            if ( comma == std::string::npos )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "pour_points entries must be 'col,row' pairs, got '" + token + "'" );
            try
            {
                const int col = std::stoi( token.substr( 0, comma ) );
                const int row = std::stoi( token.substr( comma + 1 ) );
                pourPoints.emplace_back( col, row );
            }
            catch ( const std::exception & )
            {
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "pour_points entries must be integer 'col,row' pairs, got '"
                                           + token + "'" );
            }
            token.clear();
        };
        for ( const char c : pourParam )
        {
            if ( c == ';' || c == ' ' )
                flushToken();
            else
                token.push_back( c );
        }
        flushToken();
        if ( pourPoints.empty() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "pour_points parsed to no outlets" );
        context.reportProgress( 0.8, "Delineating watersheds" );
        productData.assign( n, 0.0f );
        if ( !TerrainFlow::watershedLabels( dir.data(), width, height, pourPoints,
                                            &productData ) )
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Watershed delineation failed (pour point out of range?)" );
    }
    else if ( product == "flat_resolve" || product == "flow_direction_inf" )
    {
        // Epsilon-gradient flat resolution over the filled surface, then
        // either the resolved surface or D∞ directions on it.
        context.reportProgress( 0.4, "Resolving flats" );
        std::vector<float> resolved( n );
        TerrainHydrology::FlatResolutionReport report;
        if ( !TerrainHydrology::resolveFlats( filled.data(), resolved.data(), width,
                                              height, nodata, &report,
                                              [&context] { return context.isCancelled(); } ) )
        {
            context.throwIfCancelled(); // cancelled() hooks exit false first
            throw RSOperatorError( ErrorCode::ComputationError, "Flat resolution failed" );
        }
        result["flatEpsilon"] = report.epsilon;
        result["raisedCells"] = static_cast<Json::UInt64>( report.raisedCells );
        if ( product == "flat_resolve" )
        {
            productData = std::move( resolved );
        }
        else
        {
            context.reportProgress( 0.7, "Computing D-infinity directions" );
            std::vector<float> angles( n );
            if ( !TerrainHydrology::flowDirectionInf( resolved.data(), angles.data(), width,
                                                      height, nodata,
                                                      [&context] { return context.isCancelled(); } ) )
            {
                context.throwIfCancelled(); // cancelled() hooks exit false first
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "D-infinity routing failed" );
            }
            Json::UInt64 undecided = 0;
            for ( std::size_t i = 0; i < n; ++i )
            {
                const float a = angles[i];
                // Count only true -1 cells (pits / un-resolved flats);
                // NoData passthrough must not inflate the stat.
                undecided += ( a < 0.0f && a != nodata && !std::isnan( a ) ) ? 1 : 0;
            }
            result["undecidedCells"] = undecided;
            productData = std::move( angles );
        }
    }
    else if ( product == "stream_network" )
    {
        const double threshold = getDouble( params, "threshold", 50.0 );
        if ( !std::isfinite( threshold ) || threshold < 1.0 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "threshold must be a finite number ≥ 1 "
                                   "(accumulation counts are self-inclusive)" );
        context.reportProgress( 0.7, "Accumulating drainage" );
        std::vector<float> acc( n, 0.0f );
        if ( !TerrainFlow::flowAccumulation( dir.data(), acc.data(), width, height,
                                             filled.data(), nodata ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Flow accumulation failed" );
        context.reportProgress( 0.8, "Extracting stream network" );
        TerrainHydrology::StreamNetwork net;
        if ( !TerrainHydrology::streamNetwork( dir.data(), acc.data(), width, height,
                                               filled.data(), nodata,
                                               static_cast<float>( threshold ), &net ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Stream network failed" );
        productData.assign( n, 0.0f );
        // NoData cells carry the sentinel (#783 spirit): masked areas must
        // stay distinguishable from genuine non-stream cells.
        if ( hasNodata )
            for ( std::size_t i = 0; i < n; ++i )
                if ( ( filled[i] == nodata || std::isnan( filled[i] ) ) )
                    productData[i] = nodata;
        Json::UInt64 streamCells = 0;
        int maxOrder = 0;
        for ( std::size_t i = 0; i < n; ++i )
        {
            if ( !net.isStream[i] )
                continue;
            ++streamCells;
            maxOrder = std::max( maxOrder, static_cast<int>( net.strahler[i] ) );
            productData[i] = static_cast<float>( net.strahler[i] );
        }
        result["streamCells"] = streamCells;
        result["maxStrahlerOrder"] = maxOrder;
        result["threshold"] = threshold;

        if ( getEnum( params, "include_segments", { "false", "true" }, "false" ) == "true" )
        {
            std::vector<TerrainHydrology::StreamSegment> segments;
            if ( !TerrainHydrology::streamSegments( dir.data(), width, height, nodata, net,
                                                    &segments ) )
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "Stream segment extraction failed" );
            const std::array<double, 6> gt = ds.geoTransform();
            Json::Value segList( Json::arrayValue );
            const std::size_t listed = std::min( segments.size(), kMaxListedSegments );
            for ( std::size_t s = 0; s < listed; ++s )
            {
                Json::Value seg( Json::objectValue );
                seg["order"] = segments[s].order;
                Json::Value line( Json::arrayValue );
                for ( const auto &cell : segments[s].cells )
                {
                    Json::Value xy( Json::arrayValue );
                    // GDAL pixel-centre mapping (includes rotation terms).
                    xy.append( gt[0] + ( cell.first + 0.5 ) * gt[1]
                               + ( cell.second + 0.5 ) * gt[2] );
                    xy.append( gt[3] + ( cell.first + 0.5 ) * gt[4]
                               + ( cell.second + 0.5 ) * gt[5] );
                    line.append( xy );
                }
                seg["line"] = line;
                segList.append( seg );
            }
            result["segmentCount"] = static_cast<Json::UInt64>( segments.size() );
            result["segmentsTruncated"] = segments.size() > kMaxListedSegments;
            result["segments"] = segList;
        }
    }
    else if ( product == "outlets" )
    {
        context.reportProgress( 0.8, "Detecting outlets" );
        productData.assign( n, 0.0f );
        if ( hasNodata )
            for ( std::size_t i = 0; i < n; ++i )
                if ( ( filled[i] == nodata || std::isnan( filled[i] ) ) )
                    productData[i] = nodata;
        const auto outlets = TerrainHydrology::detectOutlets( filled.data(), dir.data(),
                                                              width, height, nodata );
        Json::Value outletList( Json::arrayValue );
        const std::size_t listed = std::min( outlets.size(), kMaxListedOutlets );
        for ( std::size_t k = 0; k < outlets.size(); ++k )
        {
            productData[static_cast<std::size_t>( outlets[k].row ) * width
                        + outlets[k].col] = 1.0f;
            if ( k < listed )
            {
                Json::Value o( Json::objectValue );
                o["col"] = outlets[k].col;
                o["row"] = outlets[k].row;
                o["atRim"] = outlets[k].atRim;
                outletList.append( o );
            }
        }
        result["outletCount"] = static_cast<Json::UInt64>( outlets.size() );
        result["outletsTruncated"] = outlets.size() > kMaxListedOutlets;
        result["outlets"] = outletList;
    }
    else
    {
        context.reportProgress( 0.8, "Accumulating drainage" );
        productData.assign( n, 0.0f );
        // Pass the filled DEM + sentinel so NoData cells stay NoData in the
        // accumulation instead of reporting a phantom 1.0 (#783).
        if ( !TerrainFlow::flowAccumulation( dir.data(), productData.data(), width, height,
                                             filled.data(), nodata ) )
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

    result["output"] = outputPath;
    result["product"] = product;
    result["width"] = width;
    result["height"] = height;
    context.reportProgress( 1.0, "Terrain flow complete" );
    return result;
}

} // namespace sicnu::operators::rs
