/***************************************************************************
 * rs_sar_temporal_events_operator.cpp — multi-temporal SAR event dating
 * (Advanced SAR / PolSAR / InSAR 10.0, package D). Streams tile-by-tile
 * across all scenes (the rs:sar_temporal_stats twin pattern), applies the
 * sar_temporal_events kernel per pixel, and writes the 9-band product with
 * declared date semantics in the result JSON and output metadata.
 ***************************************************************************/
#include "rs_sar_temporal_events_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_temporal.h"
#include "processing/algorithms/sar/sar_temporal_events.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>
#include <QStringList>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTileDim = 256;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr int kProductBands = 9;

const std::vector<std::string> s_bands = {
    "event_flag",  "event_count", "first_event_index", "last_event_index",
    "first_event_days", "last_event_days", "max_deviation_db", "argmax_days", "valid_count"
};

} // anonymous namespace

Json::Value RsSarTemporalEventsOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    Json::Value inputs = makeStringParam( "inputs", "Scene paths, N >= 2, identical grids", "" );
    inputs["type"] = "array";
    inputs["items"] = Json::Value( Json::objectValue );
    inputs["items"]["type"] = "string";
    inputs["required"] = true;
    props["inputs"] = inputs;
    Json::Value dates = makeStringParam( "dates",
                                         "Acquisition dates (ISO 8601 UTC), one per scene, "
                                         "ascending; overrides per-scene "
                                         "SICNU_SAR_ACQUISITION_UTC metadata",
                                         "" );
    dates["type"] = "array";
    dates["items"] = Json::Value( Json::objectValue );
    dates["items"]["type"] = "string";
    props["dates"] = dates;
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based band used from every scene", 1.0 );
    Json::Value domain = makeEnumParam( "inputDomain", "Radiometric domain resolution",
                                        { "declared", "linear_power", "db" }, "declared" );
    props["inputDomain"] = domain;
    props["changeThresholdDb"] = makeNumberParam( "changeThresholdDb",
                                                  "Event threshold in dB vs the median baseline",
                                                  6.0 );
    props["minValid"] = makeNumberParam( "minValid", "Minimum valid observations per pixel",
                                         2.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["dates"] = makeStringParam( "dates", "Resolved acquisition dates (ISO 8601)", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "inputs", "output" } );
    return root;
}

Json::Value RsSarTemporalEventsOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "temporal" );
    meta["tags"].append( "change" );
    meta["task"] = "sar-temporal";
    meta["gpu"] = false;
    meta["purpose"] = "Answer WHEN a change happened: per-pixel event dating on real "
                      "acquisition dates with irregular revisit intervals.";
    meta["prerequisites"].append( "Scenes co-registered on an identical grid; acquisition "
                                  "dates via the dates parameter or per-scene "
                                  "SICNU_SAR_ACQUISITION_UTC metadata (missing dates are "
                                  "refused — no index-only products)." );
    meta["prerequisites"].append( "Calibrate scenes first (rs:sar_calibrate)." );
    meta["limitations"].append( "Incoherent amplitude analysis only — no interferometric "
                                "phase." );
    meta["limitations"].append( "Dates must be UTC ISO 8601 and strictly ascending." );
    meta["limitations"].append( "Pixels under minValid observations are NaN everywhere "
                                "except valid_count." );
    return meta;
}

Json::Value RsSarTemporalEventsOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        ( 4ULL + kProductBands ) * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsSarTemporalEventsOperator::estimateExecution( const Json::Value &params ) const {
    std::uint64_t scenes = 4;
    if ( params.isObject() && params.isMember( "inputs" ) && params["inputs"].isArray() )
        scenes = params["inputs"].size();
    Json::Value est = executionEstimate();
    est["estimatedRamBytes"] = Json::Value::UInt64(
        ( scenes + kProductBands ) * kTileDim * kTileDim * sizeof( float ) );
    est["basis"] = "dynamic";
    return est;
}

Json::Value RsSarTemporalEventsOperator::run( const Json::Value &params,
                                              RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    if ( !params.isMember( "inputs" ) || !params["inputs"].isArray() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "inputs must be a JSON array of scene paths" );
    std::vector<std::string> paths;
    for ( const auto &entry : params["inputs"] )
    {
        if ( !entry.isString() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "inputs entries must be strings" );
        paths.push_back( entry.asString() );
    }
    if ( paths.size() < 2 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "sar_temporal_events needs at least 2 scenes (got "
                                   + std::to_string( paths.size() ) + ")" );
    const std::string outputPath = requireString( params, "output" );
    const int band = static_cast<int>( getDouble( params, "band", 1.0 ) );
    const std::string domainMode = getEnum( params, "inputDomain",
                                            { "declared", "linear_power", "db" }, "declared" );
    const double changeThresholdDb = getDouble( params, "changeThresholdDb", 6.0 );
    const int minValid = static_cast<int>( getDouble( params, "minValid", 2.0 ) );
    if ( !( changeThresholdDb >= 0.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "changeThresholdDb must be >= 0" );
    if ( minValid < 1 )
        throw RSOperatorError( ErrorCode::InvalidParameter, "minValid must be >= 1" );

    ensureGdalInit();

    const size_t nScenes = paths.size();
    std::vector<std::unique_ptr<GdalDatasetWrapper>> scenes;
    scenes.reserve( nScenes );
    int width = 0;
    int height = 0;
    for ( const std::string &path : paths )
    {
        auto ds = std::make_unique<GdalDatasetWrapper>();
        if ( !ds->open( QString::fromStdString( path ) ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to open scene: " + path );
        if ( band < 1 || band > ds->bandCount() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "band must be in [1, " + std::to_string( ds->bandCount() )
                                       + "] for scene " + path );
        if ( width == 0 )
        {
            width = ds->width();
            height = ds->height();
            if ( width <= 0 || height <= 0 )
                throw RSOperatorError( ErrorCode::InvalidInputData, "Scene is empty: " + path );
        }
        else if ( ds->width() != width || ds->height() != height )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Scenes are not co-registered: " + path + " is "
                                       + std::to_string( ds->width() ) + "x"
                                       + std::to_string( ds->height() ) );
        else
        {
            const auto gt0 = scenes.front()->geoTransform();
            const auto gt = ds->geoTransform();
            for ( int k = 0; k < 6; ++k )
                if ( std::fabs( gt[k] - gt0[k] ) > 1e-9 )
                    throw RSOperatorError( ErrorCode::InvalidInputData,
                                           "Scenes are not co-registered: " + path
                                               + " carries a different geotransform" );
            if ( ds->projection() != scenes[0]->projection() )
                throw RSOperatorError( ErrorCode::InvalidInputData,
                                       "Scenes are not co-registered: " + path
                                           + " carries a different CRS" );
        }
        scenes.push_back( std::move( ds ) );
    }

    // ---- Acquisition dates: explicit parameter wins; per-scene metadata
    // fills the rest; missing/invalid are typed refusals (S-1 close).
    std::vector<QString> dateStrings( nScenes );
    std::vector<double> epochSeconds( nScenes, 0.0 );
    bool anyExplicit = false;
    if ( params.isMember( "dates" ) )
    {
        if ( !params["dates"].isArray() )
            throw RSOperatorError( ErrorCode::InvalidParameter, "dates must be an array" );
        if ( params["dates"].size() != static_cast<Json::ArrayIndex>( nScenes ) )
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "dates must have one entry per scene (" + std::to_string( nScenes ) + ")" );
        for ( Json::ArrayIndex i = 0; i < params["dates"].size(); ++i )
        {
            if ( !params["dates"][i].isString() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "dates entries must be strings" );
            dateStrings[i] = QString::fromStdString( params["dates"][i].asString() );
            anyExplicit = true;
        }
    }
    for ( size_t i = 0; i < nScenes; ++i )
    {
        if ( anyExplicit && !dateStrings[i].isEmpty() )
            continue; // explicit entry wins for this scene
        const char *declared = GDALGetMetadataItem(
            static_cast<GDALDatasetH>( scenes[i]->dataset() ), sicnu::sar::kAcquisitionUtcKey, nullptr );
        if ( declared != nullptr )
            dateStrings[i] = QString::fromUtf8( declared );
    }
    for ( size_t i = 0; i < nScenes; ++i )
    {
        if ( dateStrings[i].isEmpty() )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "ACQUISITION_DATES_MISSING: scene " + std::to_string( i ) + " ("
                    + paths[i]
                    + ") declares no SICNU_SAR_ACQUISITION_UTC and no explicit dates entry "
                      "was given — event dating refuses to emit index-only products" );
        QString error;
        if ( !sicnu::sar::parseAcquisitionUtc( dateStrings[i], &epochSeconds[i], &error ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "invalid acquisition date for scene " + std::to_string( i )
                                       + ": " + error.toStdString() );
        if ( i > 0 && !( epochSeconds[i] > epochSeconds[i - 1] ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "DATES_NOT_ASCENDING: scene " + std::to_string( i )
                                       + " (" + dateStrings[i].toStdString()
                                       + ") is not strictly after its predecessor" );
    }
    std::vector<double> dayOffsets( nScenes, 0.0 );
    for ( size_t i = 0; i < nScenes; ++i )
        dayOffsets[i] = sicnu::sar::daysSince( epochSeconds[i], epochSeconds[0] );

    // ---- Domain resolution (the shared dualpol/temporal rule).
    bool inputIsDb = false;
    if ( domainMode == "db" )
        inputIsDb = true;
    else if ( domainMode == "linear_power" )
        inputIsDb = false;
    else
    {
        bool anyDeclared = false;
        bool allDb = true;
        bool allLinear = true;
        for ( const auto &ds : scenes )
        {
            const char *declared = GDALGetMetadataItem(
                static_cast<GDALDatasetH>( ds->dataset() ), sicnu::sar::kDomainKey, nullptr );
            if ( declared == nullptr )
                continue;
            anyDeclared = true;
            const QString d = QString::fromUtf8( declared );
            if ( d == QLatin1String( "db" ) )
                allLinear = false;
            else if ( d == QLatin1String( "linear_power" ) )
                allDb = false;
            else
                throw RSOperatorError( ErrorCode::InvalidInputData,
                                       "Scene declares an unknown SICNU_SAR_DOMAIN: "
                                           + d.toStdString() );
        }
        if ( anyDeclared && !allDb && !allLinear )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Scenes declare mixed SICNU_SAR_DOMAIN values - pass "
                                   "inputDomain explicitly only when the mixed declarations "
                                   "are wrong" );
        inputIsDb = anyDeclared && allDb;
    }

    // Per-scene declared sentinels (float-space comparison, #444/#803 rule).
    std::vector<bool> sceneHasSentinel( nScenes, false );
    std::vector<float> sceneSentinel( nScenes, 0.0f );
    for ( size_t s = 0; s < nScenes; ++s )
    {
        bool has = false;
        const double nodata = scenes[s]->bandNoDataValue( band, &has );
        if ( has && std::isfinite( nodata ) )
        {
            sceneHasSentinel[s] = true;
            sceneSentinel[s] = static_cast<float>( nodata );
        }
    }

    GdalBlockStream stream( *scenes[0], band, kTileDim, kTileDim, 0 );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height,
                             kProductBands, GDT_Float32, scenes[0]->geoTransform(),
                             scenes[0]->projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    std::vector<std::vector<float>> tiles( nScenes );
    std::vector<double> series( nScenes );
    std::vector<float> outPlane( static_cast<size_t>( kProductBands ) * kTileDim * kTileDim );
    sicnu::sar::TemporalEventResult events;

    const int totalTiles = stream.tileCount();
    int tileIndex = 0;
    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float * ) {
        context.throwIfCancelled();
        const size_t tileN = static_cast<size_t>( tile.width ) * tile.height;
        for ( size_t s = 0; s < nScenes; ++s )
        {
            tiles[s].resize( tileN );
            if ( !scenes[s]->readBandWindow( band, tile.xOffset, tile.yOffset, tile.width,
                                             tile.height, tiles[s].data() ) )
                return false;
        }
        for ( size_t i = 0; i < tileN; ++i )
        {
            for ( size_t s = 0; s < nScenes; ++s )
            {
                const float v = tiles[s][i];
                const bool sentinel = sceneHasSentinel[s] && v == sceneSentinel[s];
                if ( std::isfinite( v ) && !sentinel )
                    series[s] = inputIsDb ? sicnu::sar::dbToLinear( v ) : v;
                else
                    series[s] = std::numeric_limits<double>::quiet_NaN();
            }
            if ( !sicnu::sar::sarTemporalEvents( series.data(), dayOffsets.data(),
                                                 static_cast<int>( nScenes ), changeThresholdDb,
                                                 &events ) )
            {
                for ( int b = 0; b < kProductBands; ++b )
                    outPlane[static_cast<size_t>( b ) * tileN + i] =
                        ( b == kProductBands - 1 ) ? 0.0f : kNaN;
                continue;
            }
            const bool enough = events.validCount >= minValid;
            for ( int b = 0; b < kProductBands; ++b )
            {
                float value = kNaN;
                if ( b == kProductBands - 1 )
                    value = static_cast<float>( events.validCount );
                else if ( enough )
                {
                    switch ( b )
                    {
                        case 0: value = events.event ? 1.0f : 0.0f; break;
                        case 1: value = static_cast<float>( events.eventCount ); break;
                        case 2: value = static_cast<float>( events.firstEventIndex ); break;
                        case 3: value = static_cast<float>( events.lastEventIndex ); break;
                        case 4: value = static_cast<float>( events.firstEventDays ); break;
                        case 5: value = static_cast<float>( events.lastEventDays ); break;
                        case 6: value = static_cast<float>( events.maxDeviationDb ); break;
                        case 7: value = static_cast<float>( events.argmaxDays ); break;
                    }
                }
                outPlane[static_cast<size_t>( b ) * tileN + i] = value;
            }
        }
        for ( int b = 0; b < kProductBands; ++b )
        {
            if ( !out.writeTile( b + 1, tile, outPlane.data() + static_cast<size_t>( b ) * tileN ) )
                return false;
        }
        context.reportProgress( 0.95 * ( ++tileIndex ) / totalTiles, "SAR temporal events" );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to stream/compute SAR temporal events" );
    }

    out.setMetadataItem( QLatin1String( sicnu::sar::kModalityKey ), QLatin1String( "sar" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_TEMPORAL_SCENES" ), QString::number( nScenes ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_TEMPORAL_EVENT_BANDS" ),
                         QString::fromStdString( [] {
                             std::string joined;
                             for ( size_t i = 0; i < s_bands.size(); ++i )
                             {
                                 if ( i )
                                     joined += ",";
                                 joined += s_bands[i];
                             }
                             return joined;
                         }() ) );
    out.setMetadataItem( QLatin1String( sicnu::sar::kDomainKey ),
                         QLatin1String( "linear_power" ) );

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["scenes"] = Json::Value::UInt64( nScenes );
    result["bandOrder"] = "event_flag,event_count,first_event_index,last_event_index,"
                          "first_event_days,last_event_days,max_deviation_db,argmax_days,"
                          "valid_count";
    result["timeSemantics"] = "index bands are 0-based scene indices; *_days bands are "
                              "floating days since scene 0 (irregular intervals supported)";
    result["inputDomainResolved"] = inputIsDb ? "db" : "linear_power";
    result["changeThresholdDb"] = changeThresholdDb;
    result["minValid"] = minValid;
    Json::Value datesJson( Json::arrayValue );
    for ( size_t i = 0; i < nScenes; ++i )
        datesJson.append( dateStrings[i].toStdString() );
    result["dates"] = datesJson;
    context.reportProgress( 1.0, "SAR temporal events complete" );
    return result;
}

} // namespace sicnu::operators::rs
