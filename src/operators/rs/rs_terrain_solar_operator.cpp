/***************************************************************************
 * rs_terrain_solar_operator.cpp — see the header for contracts.
 ***************************************************************************/
#include "rs_terrain_solar_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/terrain_solar.h"
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

const std::vector<std::string> s_products = { "shadow_duration", "hillshade_series" };
constexpr std::size_t kMaxTrackSamples = 1024;

std::vector<TerrainSolar::SunSample> parseSunTrack( const std::string &text )
{
    std::vector<TerrainSolar::SunSample> track;
    std::string token;
    auto flush = [&]() {
        if ( token.empty() )
            return;
        // "az,elev[,weight]"
        std::vector<double> values;
        std::string piece;
        auto flushPiece = [&]() {
            if ( piece.empty() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "sun_track entries must be 'az,elev[,weight]', got '"
                                           + token + "'" );
            try
            {
                values.push_back( std::stod( piece ) );
            }
            catch ( const std::exception & )
            {
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "sun_track entries must be numeric "
                                       "'az,elev[,weight]', got '" + token + "'" );
            }
            piece.clear();
        };
        for ( const char c : token )
        {
            if ( c == ',' )
                flushPiece();
            else
                piece.push_back( c );
        }
        flushPiece();
        if ( values.size() < 2 || values.size() > 3 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "sun_track entries must be 'az,elev[,weight]', got '"
                                       + token + "'" );
        TerrainSolar::SunSample s;
        s.azimuthDeg = values[0];
        s.elevationDeg = values[1];
        s.weight = values.size() == 3 ? values[2] : 1.0;
        track.push_back( s );
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
    return track;
}

} // anonymous namespace

Json::Value RsTerrainSolarOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "DEM raster (projected CRS recommended)" );
    props["output"] = makeOutputParam( "output", "Output raster", "tif" );
    props["product"] = makeEnumParam( "product", "Solar terrain product", s_products,
                                      "shadow_duration" );
    props["sun_track"] = makeStringParam( "sun_track",
        "Sun positions as 'azimuth,elevation[,weight]' (degrees, weight optional), "
        "separated by ';' or spaces. Takes precedence over generation.", "" );
    props["day_of_year"] = makeNumberParam( "day_of_year",
        "Day of year (1–365) for generated tracks", 81.0 );
    props["latitude"] = makeNumberParam( "latitude",
        "Latitude in degrees for generated tracks", 40.0 );
    props["start_hour"] = makeNumberParam( "start_hour",
        "Local SOLAR time start hour for generated tracks", 6.0 );
    props["end_hour"] = makeNumberParam( "end_hour",
        "Local SOLAR time end hour for generated tracks", 18.0 );
    props["step_hours"] = makeNumberParam( "step_hours",
        "Sampling step in hours for generated tracks", 1.0 );
    props["nodata"] = makeNumberParam( "nodata",
        "DEM NoData value; undeclared bands keep NaN as the missing marker", -9999.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster" );
    outputs["product"] = makeStringParam( "product", "Computed product", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "product" } );
    return root;
}

Json::Value RsTerrainSolarOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "terrain" );
    meta["tags"].append( "solar" );
    meta["tags"].append( "shadow" );
    meta["task"] = "solar_terrain";
    meta["notes"] = "Parallel-ray shadow model (exact per sun position; "
                    "azimuths quantized to 1° sectors). Generated tracks use "
                    "the low-precision solar position (±1°) from local SOLAR "
                    "time — supply civil time only via your own track. "
                    "Curvature is not applied to shadows (v1).";
    meta["gpu"] = false;
    meta["purpose"] = "Shadow-duration mapping, site solar exposure, "
                      "hillshade animation frames.";
    meta["prerequisites"].append( "Projected (metric) DEM recommended; cell size "
                                  "comes from the geotransform." );
    meta["workflowHints"].append( "Use shadow_duration for exposure rasters and "
                                  "hillshade_series for time-of-day animation." );
    meta["limitations"].append( "Full-frame memory (~16 bytes/cell for shadow "
                                "duration); capped by SICNU_TERRAIN_MAX_CELLS." );
    meta["limitations"].append( "No curvature/refraction on shadows (v1); "
                                "quantized 1° azimuth sectors." );
    meta["limitations"].append( "Track length capped at 1024 samples." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    return meta;
}

Json::Value RsTerrainSolarOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 16ULL * 4096ULL * 4096ULL );
    return est;
}

Json::Value RsTerrainSolarOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                  static_cast<std::uint64_t>( std::max( 1, probe.height() ) ),
                  16ULL } );
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

Json::Value RsTerrainSolarOperator::run( const Json::Value &params,
                                         RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string product = getEnum( params, "product", s_products, "shadow_duration" );

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

    // Build the sun track: explicit parameter wins, else generate.
    std::vector<TerrainSolar::SunSample> track;
    const std::string trackParam = params.isMember( "sun_track" ) && params["sun_track"].isString()
                                       ? params["sun_track"].asString()
                                       : std::string();
    if ( !trackParam.empty() )
    {
        track = parseSunTrack( trackParam );
    }
    else
    {
        const int day = static_cast<int>( getDouble( params, "day_of_year", 81.0 ) );
        const double latitude = getDouble( params, "latitude", 40.0 );
        double startHour = getDouble( params, "start_hour", 6.0 );
        double endHour = getDouble( params, "end_hour", 18.0 );
        const double stepHours = getDouble( params, "step_hours", 1.0 );
        if ( stepHours <= 0.0 || stepHours > 24.0 || startHour < 0.0 || endHour > 24.0
             || startHour > endHour )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "invalid solar-time window (start ≤ end, step > 0)" );
        for ( double hour = startHour; hour <= endHour + 1e-9; hour += stepHours )
        {
            TerrainSolar::SunSample s;
            double az = 0.0;
            double elev = 0.0;
            if ( !TerrainSolar::sunPositionDeg( day, hour, latitude, &az, &elev ) )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "solar position generation failed for hour "
                                           + std::to_string( hour ) );
            s.azimuthDeg = az;
            s.elevationDeg = elev;
            s.weight = stepHours; // uniform cadence: weight = duration share
            track.push_back( s );
        }
    }
    if ( track.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "sun track is empty (all samples night or unparsable)" );
    if ( track.size() > kMaxTrackSamples )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "sun track exceeds " + std::to_string( kMaxTrackSamples )
                                   + " samples (got " + std::to_string( track.size() )
                                   + ")" );

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
    result["trackSamples"] = static_cast<Json::UInt64>( track.size() );

    const auto cancelledHook = [&context] { return context.isCancelled(); };
    context.reportProgress( 0.3, "Computing " + product );

    if ( product == "shadow_duration" )
    {
        TerrainSolar::ShadowDurationResult shadow;
        if ( !TerrainSolar::shadowDuration( dem.data(), width, height, nodata, cellSizeX,
                                            cellSizeY, track, &shadow, cancelledHook ) )
        {
            context.throwIfCancelled(); // cancelled() hooks exit false first
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Shadow duration failed (empty daylight track?)" );
        }
        context.reportProgress( 0.85, "Writing output raster" );
        GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                                 GDT_Float32, ds.geoTransform(), ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create output raster: " + outputPath );
        out.setNoDataValue( hasNodata ? static_cast<double>( nodata )
                                      : std::numeric_limits<double>::quiet_NaN() );
        if ( !out.writeTile(
                 1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                 shadow.shadowFraction.data() ) )
        {
            out.abandon();
            throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
        }
        QString closeError;
        if ( !out.closeWithError( &closeError ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to finalize output: " + closeError.toStdString() );
        result["weightSum"] = shadow.weightSum;
        result["daylightSamples"] = static_cast<Json::UInt64>( shadow.sampleCount );
        result["sectorsUsed"] = static_cast<Json::UInt64>( shadow.sectorsUsed );
    }
    else
    {
        // hillshade_series: one band per track sample via the existing
        // TerrainAnalysis::hillshade kernel.
        GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height,
                                 static_cast<int>( track.size() ), GDT_Float32,
                                 ds.geoTransform(), ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create output raster: " + outputPath );
        out.setNoDataValue( hasNodata ? static_cast<double>( nodata )
                                      : std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> band( n );
        for ( std::size_t k = 0; k < track.size(); ++k )
        {
            context.throwIfCancelled();
            if ( !TerrainAnalysis::hillshade( dem.data(), band.data(), width, height,
                                              cellSizeX, cellSizeY, nodata,
                                              static_cast<float>( track[k].azimuthDeg ),
                                              static_cast<float>( track[k].elevationDeg ) ) )
            {
                out.abandon();
                context.throwIfCancelled();
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "Hillshade failed for track sample "
                                           + std::to_string( k ) );
            }
            if ( !out.writeTile(
                     static_cast<int>( k ) + 1,
                     GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                     band.data() ) )
            {
                out.abandon();
                throw RSOperatorError( ErrorCode::GdalError, "Failed to write output" );
            }
            context.reportProgress(
                0.3 + 0.55 * ( static_cast<double>( k ) + 1.0 )
                          / static_cast<double>( track.size() ),
                "Hillshade band " + std::to_string( k + 1 ) + "/" +
                    std::to_string( track.size() ) );
        }
        QString closeError;
        if ( !out.closeWithError( &closeError ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to finalize output: " + closeError.toStdString() );
    }

    context.reportProgress( 1.0, "Terrain solar complete" );
    return result;
}

} // namespace sicnu::operators::rs
