/***************************************************************************
 * rs_temporal_monitor_operator.cpp — Milestone E (CUSUM / EWMA / seasonal MK)
 *
 * Structure mirrors rs:temporal_anomaly (collection preflight, QA masking,
 * band-role resolution, tile loop, 3-band NaN output, atomic guard); the
 * per-pixel statistics come from temporal_monitoring.h.
 ***************************************************************************/
#include "rs_temporal_monitor_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_monitoring.h"
#include "processing/algorithms/temporal/temporal_stats.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/algorithms/temporal/temporal_time.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDate>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using namespace sicnu::temporal;

namespace {

const char *kBandNames[3] = { "statistic", "extreme", "aux" };

constexpr int kDefaultTileSize = 256;

int seasonOfMonth( const QString &dateString )
{
    // Qt::ISODate "YYYY-MM-DD"; month = the middle field (0 = invalid time).
    if ( dateString.size() < 7 )
        return 0;
    bool ok = false;
    const int month = dateString.mid( 5, 2 ).toInt( &ok );
    return ( ok && month >= 1 && month <= 12 ) ? month : 0;
}

} // anonymous namespace

Json::Value RsTemporalMonitorOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["collection"] = makeStringParam( "collection", "Temporal collection id or workspace path" );
    props["output"] = makeOutputParam( "output", "3-band monitoring raster", "tif" );
    props["method"] = makeEnumParam( "method", "Monitoring method",
                                     { "cusum", "ewma", "seasonal_mk" }, "cusum" );
    props["lambda"] = makeNumberParam( "lambda", "EWMA smoothing factor in (0, 1]", 0.3 );
    props["drift"] = makeNumberParam( "drift", "CUSUM drift allowance in sigma units", 0.0 );
    props["band_role"] = makeStringParam( "band_role", "Semantic band role to analyse", "" );
    props["band"] = makeIntegerParam( "band", "1-based band override", 0 );
    props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Apply the collection's QA masking", true );
    props["min_observations"] = makeIntegerParam( "min_observations", "Minimum valid observations for a defined statistic", 3 );
    props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size", kDefaultTileSize );
    props["max_pairwork"] = makeIntegerParam( "max_pairwork", "seasonal_mk guard: refuse when scenes^2*tilePixels exceeds this pair budget", 200000000 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "" );
    outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes analysed", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "collection", "output", "method" } );
    return root;
}

Json::Value RsTemporalMonitorOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "temporal" );
    meta["tags"].append( "cusum" );
    meta["tags"].append( "ewma" );
    meta["tags"].append( "mann-kendall" );
    meta["task"] = "temporal-monitoring";
    meta["notes"] = "CUSUM/EWMA standardize against the full-series per-pixel "
                    "statistics (retrospective monitoring). Seasonal MK uses "
                    "calendar-month seasons with the tie-corrected variance.";
    meta["gpu"] = false;
    meta["purpose"] = "Detect gradual or persistent temporal change beyond one-off anomalies.";
    meta["prerequisites"].append( "Common grid, acquisition times, consistent radiometric state (temporal preflight)." );
    meta["workflowHints"].append( "Follow with rs:threshold_raster on the extreme band to flag monitored pixels." );
    meta["limitations"].append( "seasonal_mk is O(sum n_m^2) pairs per pixel; the max_pairwork guard refuses unbounded collections instead of degrading." );
    meta["limitations"].append( "argmax bands are 0-based scene indices; scene dates travel in the collection metadata." );
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    meta["largeRasterSafe"] = true;
    return meta;
}

Json::Value RsTemporalMonitorOperator::executionEstimate() const {
    return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 6, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalMonitorOperator::estimateExecution( const Json::Value &params ) const {
    const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
    return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, 9, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalMonitorOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    const std::string outputPath = requireString( params, "output" );
    const QString method = QString::fromStdString(
        getEnum( params, "method", { "cusum", "ewma", "seasonal_mk" }, "cusum" ) );
    const double lambda = getDouble( params, "lambda", 0.3 );
    const double drift = getDouble( params, "drift", 0.0 );
    const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
    const int minObservations = std::max( 3, getInt( params, "min_observations", 3 ) );
    const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
    const qint64 maxPairwork = static_cast<qint64>( getInt( params, "max_pairwork", 200000000 ) );
    const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
    const int bandOverride = getInt( params, "band", 0 );

    if ( method == QLatin1String( "ewma" ) && !( lambda > 0.0 && lambda <= 1.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "lambda must be in (0, 1], got " + std::to_string( lambda ) );

    auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
    const int sceneCount = prepared.collection.sceneCount();
    if ( sceneCount < minObservations )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "collection has " + std::to_string( sceneCount ) +
                                   " scene(s) < min_observations " + std::to_string( minObservations ) );

    // Season ids (calendar month) and chronological order for seasonal_mk.
    std::vector<int> seasonIds( static_cast<size_t>( sceneCount ), 0 );
    for ( int s = 0; s < sceneCount; ++s )
        seasonIds[static_cast<size_t>( s )] = seasonOfMonth( prepared.collection.scenes().at( s ).time.dateString() );

    // The per-tile scene stack (stack + validity ~= 5 bytes/pixel/scene) is
    // resident; keep the working set bounded and say so in the estimate.
    const qint64 perPixelBytes = 5LL * sceneCount;
    const qint64 maxWorkingSetBytes = 2LL * 1024 * 1024 * 1024;
    const int maxTile = static_cast<int>(
        std::sqrt( static_cast<double>( maxWorkingSetBytes ) /
                   static_cast<double>( std::max<qint64>( 1, perPixelBytes ) ) ) );
    int effectiveTile = std::clamp( tileSize, 16, 4096 );
    if ( effectiveTile > maxTile )
    {
        effectiveTile = std::max( 16, maxTile );
        context.logWarning( "tile_size reduced to " + std::to_string( effectiveTile ) +
                            " to keep the per-tile scene stack under the working-set bound "
                            "(5 bytes/pixel/scene x " + std::to_string( sceneCount ) + " scenes)" );
    }

    temporal::TemporalStreamOptions streamOptions;
    streamOptions.tileWidth = effectiveTile;
    streamOptions.tileHeight = effectiveTile;
    streamOptions.applyQaMasking = applyQaMasking;

    QString readerError;
    TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
    if ( !readerError.isEmpty() )
        throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

    std::vector<int> analysisBands( static_cast<size_t>( sceneCount ), 1 );
    bool anyFallback = false;
    for ( int s = 0; s < sceneCount; ++s )
    {
        bool fallback = false;
        const int band = reader.bandForRole( s, bandRole, bandOverride, &fallback );
        analysisBands[static_cast<size_t>( s )] = band > 0 ? band : 1;
        anyFallback = anyFallback || fallback;
    }
    if ( anyFallback )
        context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                            "pass 'band' or 'bands' to pin it." );

    const int width = reader.width();
    const int height = reader.height();

    // Pairwork guard for seasonal_mk (declared bound, typed refusal).
    if ( method == QLatin1String( "seasonal_mk" ) )
    {
        const qint64 pairsPerTile =
            static_cast<qint64>( sceneCount ) * sceneCount / 2 *
            static_cast<qint64>( std::min( tileSize, width ) ) *
            static_cast<qint64>( std::min( tileSize, height ) );
        if ( pairsPerTile > maxPairwork )
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "seasonal_mk pairwork " + std::to_string( pairsPerTile ) +
                    " exceeds max_pairwork " + std::to_string( maxPairwork ) +
                    " per tile; reduce tile_size or aggregate the collection first" );
    }

    context.reportProgress( 0.05, "Creating monitor output" );
    GdalDatasetWrapper out;
    QString outErr;
    temporal_output::TemporalOutputGuard guard;
    guard.manage( &out, QString::fromStdString( outputPath ) );
    if ( !out.create( QString::fromStdString( outputPath ), width, height, 3,
                      static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                      &outErr ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "failed to create output: " + outErr.toStdString() );
    for ( int b = 1; b <= 3; ++b )
    {
        out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
        GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                            kBandNames[b - 1] );
    }

    const int tiles = reader.totalTileCount();
    const size_t tilePixels = static_cast<size_t>( effectiveTile ) * effectiveTile;
    std::vector<float> sceneTile( tilePixels );
    // Full scene stack per tile: values + validity, then per-pixel stats.
    std::vector<float> stack( static_cast<size_t>( sceneCount ) * tilePixels );
    std::vector<uint8_t> valid( static_cast<size_t>( sceneCount ) * tilePixels, 0 );
    std::vector<temporal::stats::WelfordAccumulator> welford( tilePixels );
    std::vector<temporal::monitoring::CusumState> cusum( tilePixels );
    std::vector<temporal::monitoring::EwmaState> ewma( tilePixels );
    std::vector<double> times( static_cast<size_t>( sceneCount ), 0.0 );
    std::vector<double> values( static_cast<size_t>( sceneCount ), 0.0 );
    std::vector<uint8_t> validOne( static_cast<size_t>( sceneCount ), 0 );
    std::vector<float> bandBuf( tilePixels );

    int tileDone = 0;
    for ( int t = 0; t < tiles; ++t )
    {
        int x = 0, y = 0, w = 0, h = 0;
        reader.tileRect( t, &x, &y, &w, &h );
        const size_t pixels = static_cast<size_t>( w ) * h;
        for ( size_t i = 0; i < pixels; ++i )
        {
            welford[i] = temporal::stats::WelfordAccumulator{};
            cusum[i] = temporal::monitoring::CusumState{};
            ewma[i] = temporal::monitoring::EwmaState{};
        }

        // Read every scene's tile once; accumulate standardization stats.
        for ( int s = 0; s < sceneCount; ++s )
        {
            if ( !reader.readSceneBandTile( s, analysisBands[static_cast<size_t>( s )], t,
                                            sceneTile.data() ) )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "failed reading scene " +
                                           prepared.collection.scenes().at( s ).path.toStdString() );
            const size_t base = static_cast<size_t>( s ) * tilePixels;
            for ( size_t i = 0; i < pixels; ++i )
            {
                const float v = sceneTile[i];
                stack[base + i] = v;
                const bool okV = std::isfinite( v );
                valid[base + i] = okV ? 1 : 0;
                if ( okV )
                    welford[i].add( v );
            }
            context.throwIfCancelled();
        }

        // Per-pixel monitoring statistics.
        for ( int s = 0; s < sceneCount; ++s )
            times[static_cast<size_t>( s )] =
                prepared.collection.scenes().at( s ).time.daysSince(
                    prepared.collection.scenes().at( 0 ).time );

        for ( size_t i = 0; i < pixels; ++i )
        {
            const auto &acc = welford[i];
            const bool enough = acc.n >= static_cast<std::uint64_t>( minObservations );
            double mean = 0.0;
            double sd = 0.0;
            if ( enough && acc.n > 0 )
            {
                mean = acc.mean;
                sd = acc.sampleStddev();
            }
            for ( int s = 0; s < sceneCount; ++s )
            {
                const size_t idx = static_cast<size_t>( s ) * tilePixels + i;
                const bool okV = valid[idx] != 0;
                validOne[static_cast<size_t>( s )] = okV ? 1 : 0;
                values[static_cast<size_t>( s )] = stack[idx];
                if ( !okV || !enough )
                    continue;
                const double z = sd > 0.0 ? ( stack[idx] - mean ) / sd : 0.0;
                temporal::monitoring::cusumStep( &cusum[i], z, drift, s );
                temporal::monitoring::ewmaStep( &ewma[i], z, lambda, s );
            }
        }

        // Emit the method's three bands.
        if ( method == QLatin1String( "seasonal_mk" ) )
        {
            // One O(sum n_m^2) pass per pixel, results held for all 3 bands.
            std::vector<float> zBuf( pixels ), tauBuf( pixels ), seasonsBuf( pixels );
            for ( size_t i = 0; i < pixels; ++i )
            {
                // Fill this pixel's series before scoring it (values/validOne
                // are per-pixel scratch; the stats loop above leaves the LAST
                // pixel's series in them).
                for ( int s = 0; s < sceneCount; ++s )
                {
                    const size_t idx = static_cast<size_t>( s ) * tilePixels + i;
                    values[static_cast<size_t>( s )] = stack[idx];
                    validOne[static_cast<size_t>( s )] = valid[idx];
                }
                const auto mk = temporal::monitoring::seasonalMannKendall(
                    times.data(), values.data(), seasonIds.data(), validOne.data(), sceneCount );
                zBuf[i] = static_cast<float>( mk.z );
                tauBuf[i] = static_cast<float>( mk.tau );
                seasonsBuf[i] = static_cast<float>( mk.seasonsUsed );
            }
            if ( !out.writeBandWindow( 1, x, y, w, h, zBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 1" );
            if ( !out.writeBandWindow( 2, x, y, w, h, tauBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 2" );
            if ( !out.writeBandWindow( 3, x, y, w, h, seasonsBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 3" );
        }
        else
        {
            for ( size_t i = 0; i < pixels; ++i )
                bandBuf[i] = static_cast<float>( method == QLatin1String( "cusum" ) ? cusum[i].s
                                                                                    : ewma[i].z );
            if ( !out.writeBandWindow( 1, x, y, w, h, bandBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 1" );
            for ( size_t i = 0; i < pixels; ++i )
                bandBuf[i] = static_cast<float>( method == QLatin1String( "cusum" ) ? cusum[i].maxAbs
                                                                                    : ewma[i].maxAbs );
            if ( !out.writeBandWindow( 2, x, y, w, h, bandBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 2" );
            for ( size_t i = 0; i < pixels; ++i )
            {
                const int arg = method == QLatin1String( "cusum" ) ? cusum[i].argmax : ewma[i].argmax;
                bandBuf[i] = arg >= 0 ? static_cast<float>( arg ) : std::numeric_limits<float>::quiet_NaN();
            }
            if ( !out.writeBandWindow( 3, x, y, w, h, bandBuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError, "failed writing monitor band 3" );
        }

        ++tileDone;
        context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                                "Monitor tiles " + std::to_string( tileDone ) + "/" +
                                    std::to_string( tiles ) );
    }

    temporal_output::writeTemporalDatasetMetadata(
        out, prepared.collection, "rs:temporal_monitor",
        QStringLiteral( "method=%1" ).arg( method ) );

    QString closeErr;
    if ( !out.closeWithError( &closeErr ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "output flush failed (disk full?): " + closeErr.toStdString() );
    guard.commit();

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["method"] = method.toStdString();
    result["sceneCount"] = static_cast<Json::Int>( sceneCount );
    if ( !prepared.collection.timeRangeStartIso().isEmpty() )
    {
        result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
        result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
    }
    Json::Value memory( Json::objectValue );
    memory["tileWidth"] = effectiveTile;
    memory["tileHeight"] = effectiveTile;
    memory["workingSetEstimateBytes"] = Json::Value::UInt64(
        TemporalTileReader::estimateWorkingSetBytes( effectiveTile, effectiveTile, 3, 6 ) +
        static_cast<std::uint64_t>( perPixelBytes ) *
            static_cast<std::uint64_t>( effectiveTile ) * effectiveTile );
    result["memory"] = memory;
    context.reportProgress( 1.0, "Temporal monitor complete" );
    return result;
}

} // namespace sicnu::operators::rs
