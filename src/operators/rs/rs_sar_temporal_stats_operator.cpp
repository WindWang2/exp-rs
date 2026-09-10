/***************************************************************************
 * rs_sar_temporal_stats_operator.cpp — multi-date SAR statistics
 * (Scientific Processing 8.0, package B). Streams tile-by-tile across all
 * scenes; per-pixel aggregates live in sar_temporal.h.
 ***************************************************************************/
#include "rs_sar_temporal_stats_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_temporal.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

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
constexpr int kProductBands = 11;

const std::vector<std::string> s_bands = {
    "mean_db", "mean_linear", "std_dev_linear", "cv", "min_db", "max_db",
    "argmax_date", "baseline_db", "max_log_deviation_db", "changed_dates", "valid_count"
};

} // anonymous namespace

Json::Value RsSarTemporalStatsOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    Json::Value inputs = makeStringParam( "inputs", "Scene paths, N >= 2, identical grids", "" );
    inputs["type"] = "array";
    inputs["items"] = Json::Value( Json::objectValue );
    inputs["items"]["type"] = "string";
    inputs["required"] = true;
    props["inputs"] = inputs;
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based band used from every scene", 1.0 );
    Json::Value domain = makeEnumParam( "inputDomain", "Radiometric domain resolution",
                                        { "declared", "linear_power", "db" }, "declared" );
    props["inputDomain"] = domain;
    props["changeThresholdDb"] = makeNumberParam( "changeThresholdDb",
                                                  "Robust-change counting threshold in dB", 6.0 );
    props["minValid"] = makeNumberParam( "minValid", "Minimum valid observations per pixel", 2.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "inputs", "output" } );
    return root;
}

Json::Value RsSarTemporalStatsOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "temporal" );
    meta["tags"].append( "statistics" );
    meta["tags"].append( "change" );
    meta["task"] = "sar-temporal";
    meta["gpu"] = false;
    meta["purpose"] = "Summarize N co-registered SAR scenes per pixel: mean and "
                      "dispersion of the backscatter, extreme dates, and "
                      "speckle-robust log-domain change against the median "
                      "baseline.";
    meta["prerequisites"].append( "Scenes must be co-registered on an identical grid (no hidden resampling)." );
    meta["prerequisites"].append( "Calibrate scenes first (rs:sar_calibrate): statistics of raw DN are not physical." );
    meta["limitations"].append( "Incoherent analysis only — no interferometric coherence." );
    meta["limitations"].append( "Radiometric normalization between scenes (incidence/season) is the caller's responsibility." );
    meta["limitations"].append( "Valid samples are finite and strictly positive; nonpositive power is NoData." );
    return meta;
}

Json::Value RsSarTemporalStatsOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    // One tile window per scene + per-pixel scratch: O(tile × scenes).
    est["estimatedRamBytes"] = Json::Value::UInt64(
        8ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsSarTemporalStatsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    if ( !params.isMember( "inputs" ) || !params["inputs"].isArray() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "inputs must be a JSON array of scene paths" );
    std::vector<std::string> paths;
    for ( const auto &entry : params["inputs"] )
    {
        if ( !entry.isString() )
            throw RSOperatorError( ErrorCode::InvalidParameter, "inputs entries must be strings" );
        paths.push_back( entry.asString() );
    }
    if ( paths.size() < 2 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "sar_temporal_stats needs at least 2 scenes (got "
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
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Scenes are not co-registered: " + path + " is "
                    + std::to_string( ds->width() ) + "x" + std::to_string( ds->height() )
                    + " but the first scene is " + std::to_string( width ) + "x"
                    + std::to_string( height ) );
        scenes.push_back( std::move( ds ) );
    }

    // Domain resolution (the dualpol rule): the declared SICNU_SAR_DOMAIN
    // wins; an explicit inputDomain overrides; undeclared falls back to
    // linear power. Mixed declarations are typed refusals — statistics over
    // a silently mixed stack would be meaningless.
    bool inputIsDb = false;
    if ( domainMode == "db" )
    {
        inputIsDb = true;
    }
    else if ( domainMode == "linear_power" )
    {
        inputIsDb = false;
    }
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
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Scenes declare mixed SICNU_SAR_DOMAIN values - pass inputDomain "
                "explicitly only when the mixed declarations are wrong" );
        inputIsDb = anyDeclared && allDb;
    }

    // Per-scene declared sentinels (compared in float space, #444): a
    // positive sentinel (e.g. 65535) must drop out of the aggregates like
    // any other invalid sample, not silently count as backscatter.
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
    std::vector<float> outPlane( static_cast<size_t>( kTileDim ) * kTileDim );
    sicnu::sar::SarTemporalStats stats;

    const int totalTiles = stream.tileCount();
    int tileIndex = 0;
    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float * ) {
        context.throwIfCancelled();
        const size_t tileN = static_cast<size_t>( tile.width ) * tile.height;
        for ( size_t s = 0; s < nScenes; ++s )
        {
            tiles[s].resize( tileN );
            if ( !scenes[s]->readBandWindow( band, tile.xOffset, tile.yOffset,
                                             tile.width, tile.height, tiles[s].data() ) )
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
            if ( !sicnu::sar::sarTemporalStats( series.data(), static_cast<int>( nScenes ),
                                                &stats, changeThresholdDb ) )
            {
                // No valid observation: everything NaN except valid_count.
                for ( int b = 0; b < kProductBands; ++b )
                    outPlane[b * tileN + i] =
                        ( b == static_cast<int>( s_bands.size() ) - 1 ) ? 0.0f : kNaN;
                continue;
            }
            const bool enough = stats.validCount >= minValid;
            for ( int b = 0; b < kProductBands; ++b )
            {
                float value = kNaN;
                if ( b == 10 )
                    value = static_cast<float>( stats.validCount );
                else if ( enough )
                {
                    switch ( b )
                    {
                        case 0: value = static_cast<float>( stats.meanDb ); break;
                        case 1: value = static_cast<float>( stats.meanLinear ); break;
                        case 2: value = static_cast<float>( stats.stdDevLinear ); break;
                        case 3: value = static_cast<float>( stats.cv ); break;
                        case 4: value = static_cast<float>( sicnu::sar::linearToDb( stats.minLinear ) ); break;
                        case 5: value = static_cast<float>( sicnu::sar::linearToDb( stats.maxLinear ) ); break;
                        case 6: value = static_cast<float>( stats.argmaxDate ); break;
                        case 7: value = static_cast<float>( stats.baselineDb ); break;
                        case 8: value = static_cast<float>( stats.maxLogDeviationDb ); break;
                        case 9: value = static_cast<float>( stats.changedDates ); break;
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
        context.reportProgress( 0.95 * ( ++tileIndex ) / totalTiles,
                                "SAR temporal statistics" );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/compute SAR temporal statistics" );
    }

    out.setMetadataItem( QLatin1String( sicnu::sar::kModalityKey ), QLatin1String( "sar" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_TEMPORAL_SCENES" ),
                         QString::number( nScenes ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_TEMPORAL_BANDS" ),
                         QString::fromStdString(
                             [] {
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
    result["width"] = width;
    result["height"] = height;
    result["bands"] = kProductBands;
    result["bandOrder"] = "mean_db,mean_linear,std_dev_linear,cv,min_db,max_db,"
                          "argmax_date,baseline_db,max_log_deviation_db,changed_dates,"
                          "valid_count";
    result["inputDomainResolved"] = inputIsDb ? "db" : "linear_power";
    result["changeThresholdDb"] = changeThresholdDb;
    result["minValid"] = minValid;
    context.reportProgress( 1.0, "SAR temporal statistics complete" );
    return result;
}

} // namespace sicnu::operators::rs
