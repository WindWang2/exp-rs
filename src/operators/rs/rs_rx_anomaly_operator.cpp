/***************************************************************************
 * rs_rx_anomaly_operator.cpp  —  Reed-Xiaoli anomaly detection RSOperator
 ***************************************************************************/
#include "rs_rx_anomaly_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_anomaly.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsRxAnomalyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output RX score raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "RX score GeoTIFF", "tif");
    outputs["mean"] = makeNumberParam("mean", "Mean RX score", 0.0);
    outputs["max"] = makeNumberParam("max", "Maximum RX score", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsRxAnomalyOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("rx");
    meta["tags"].append("anomaly");
    meta["tags"].append("hyperspectral");
    meta["purpose"] = "Detect pixels that deviate from the scene background "
                     "by Mahalanobis distance (unsupervised).";
    meta["workflowHints"].append("Input should be calibrated reflectance/radiance; "
                                 "run before target detection / screening.");
    meta["limitations"].append("Global RX uses the whole scene as background; "
                               "for local background use a windowed variant.");
    return meta;
}

Json::Value RsRxAnomalyOperator::executionEstimate() const {
    // MultiPassStreaming: a 256x256 BIP tile is resident per pass (3 passes:
    // mean, covariance, score) plus the bands*bands covariance and bands*mean
    // vector. Peak RAM is O(tilePixels*bands*sizeof(float) + bands^2), NOT a
    // fixed constant — the tile window scales with the raster's band count
    // (a 224-band hyperspectral tile is ~59 MiB vs ~1 MiB at 4 bands). The
    // scheduler treats this as an estimate (RSS watermark is the backstop);
    // the nominal 30 bands keeps the declared value honest for multispectral.
    constexpr double kTileW = 256, kTileH = 256;
    constexpr double kNominalBands = 30; // documented nominal; real cost is O(bands)
    const double tileBytes = kTileW * kTileH * kNominalBands * sizeof( float );
    const double stateBytes = kNominalBands * kNominalBands * sizeof( double ); // covariance
    Json::Value est( Json::objectValue );
    est["tileWidth"] = static_cast<Json::Int64>( kTileW );
    est["tileHeight"] = static_cast<Json::Int64>( kTileH );
    est["estimatedRamBytes"] =
        static_cast<Json::UInt64>( tileBytes + stateBytes ); // ~7.9 MiB @ 30 bands
    return est;
}

Json::Value RsRxAnomalyOperator::run(const Json::Value& params,
                                     RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "RX detection requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    // Three-pass streaming RX (perf goal §2c): the whole raster is never
    // resident. Pass 1 accumulates the mean, pass 2 the covariance, pass 3
    // scores per-tile and streams output. The stats accumulate in the same
    // per-pixel order as the legacy kernel, so results match within FP rounding
    // (verified against the full-raster kernel with an Approx tolerance).
    constexpr int kTile = 256;
    GdalMultibandBlockStream stream( ds, bandCount, kTile, kTile );
    const int totalTiles = stream.tileCount();
    const double perTileProgress = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    SpectralAnomaly::BackgroundStats stats;

    // Per-band NoData sentinels: the band's declared NoData when present, else
    // the -9999 processing-stack convention. Pixels matching any band's NoData
    // (or non-finite) are excluded from the statistics and scored as NaN.
    std::vector<float> noDataPerBand( static_cast<size_t>( bandCount ), -9999.0f );
    for ( int b = 0; b < bandCount; ++b )
    {
        bool hasNoData = false;
        const double nd = ds.bandNoDataValue( b + 1, &hasNoData );
        if ( hasNoData )
            noDataPerBand[static_cast<size_t>( b )] = static_cast<float>( nd );
    }

    // Pass 1: mean. Invalid pixels (non-finite or band NoData) are skipped so
    // the statistics reflect valid pixels only.
    int tilesSeen = 0;
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            SpectralAnomaly::accumulateMean( bip, tilePixels, bandCount, &stats, true,
                                             noDataPerBand.data() );
            context.reportProgress( ( ++tilesSeen ) * perTileProgress * 0.33, "Background mean" );
            return true;
        } ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (mean pass)" );
    if ( stats.count == 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                              "No valid pixels found for RX anomaly detection" );
    SpectralAnomaly::finalizeMean( &stats );
    context.throwIfCancelled();

    // Pass 2: covariance (same valid-pixel predicate).
    tilesSeen = 0;
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            SpectralAnomaly::accumulateCovariance( bip, tilePixels, bandCount, &stats, true,
                                                   noDataPerBand.data() );
            context.reportProgress( 0.33 + ( ++tilesSeen ) * perTileProgress * 0.33,
                                    "Background covariance" );
            return true;
        } ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (covariance pass)" );
    SpectralAnomaly::finalizeCovariance( &stats );
    context.throwIfCancelled();

    std::vector<double> invCov;
    if ( !SpectralAnomaly::invertCovariance( stats.covariance, bandCount, &invCov ) )
        throw RSOperatorError( ErrorCode::ComputationError,
                              "Background covariance is singular" );

    // Pass 3: per-tile RX score, streamed to the output raster. Also accumulate
    // running mean/max of the scores for the result summary (matches the legacy
    // output contract).
    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                              "Failed to create RX output raster: " + outputPath );

    double sumScores = 0.0;
    double maxScore = 0.0;
    size_t scoredPixels = 0;
    tilesSeen = 0;
    std::vector<float> tileScores;
    std::vector<double> rxScratch; // reused across pixels (no per-pixel alloc)
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            tileScores.assign( tilePixels, 0.0f );
            for ( size_t p = 0; p < tilePixels; ++p )
            {
                // Invalid pixels (non-finite or matching a band's NoData)
                // propagate to the output as NaN — the raster's NoData — and are
                // excluded from the summary statistics.
                const float *spectrum = bip + p * static_cast<size_t>( bandCount );
                bool valid = true;
                for ( int b = 0; b < bandCount; ++b )
                {
                    if ( !std::isfinite( spectrum[b] )
                         || std::abs( spectrum[b] - noDataPerBand[static_cast<size_t>( b )] ) < 1e-3f )
                    {
                        valid = false;
                        break;
                    }
                }
                if ( !valid )
                {
                    tileScores[p] = std::numeric_limits<float>::quiet_NaN();
                    continue;
                }
                const float s = SpectralAnomaly::rxScore(
                    spectrum, stats.mean, invCov, bandCount, &rxScratch );
                tileScores[p] = s;
                sumScores += s;
                if ( s > maxScore )
                    maxScore = s;
                ++scoredPixels;
            }
            if ( !out.writeTile( 1, tile, tileScores.data() ) )
                return false;
            context.reportProgress( 0.66 + ( ++tilesSeen ) * perTileProgress * 0.34,
                                    "Scoring" );
            return true;
        } ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream output tiles (score pass)" );
    out.close();
    context.throwIfCancelled();

    const double mean = scoredPixels > 0 ? sumScores / static_cast<double>( scoredPixels ) : 0.0;

    ds.close();
    context.reportProgress( 1.0, "RX anomaly detection complete" );

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["mean"] = mean;
    result["max"] = maxScore;
    return result;
}

} // namespace sicnu::operators::rs
