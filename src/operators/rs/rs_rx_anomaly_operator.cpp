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
    // MultiPassStreaming: a 256x256 tile is resident per pass (3 passes: mean,
    // covariance, score) plus the bands*bands covariance (~negligible) and the
    // bands*mean vector. Peak RAM is O(tile + bands^2), independent of raster
    // size — a dramatic reduction from the prior full-raster materialization.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 1048576; // ~1 MiB (one tile + bands*bands state)
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

    // Pass 1: mean.
    int tilesSeen = 0;
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            SpectralAnomaly::accumulateMean( bip, tilePixels, bandCount, &stats );
            context.reportProgress( ( ++tilesSeen ) * perTileProgress * 0.33, "Background mean" );
            return true;
        } ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (mean pass)" );
    SpectralAnomaly::finalizeMean( &stats );
    context.throwIfCancelled();

    // Pass 2: covariance.
    tilesSeen = 0;
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            SpectralAnomaly::accumulateCovariance( bip, tilePixels, bandCount, &stats );
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
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            tileScores.assign( tilePixels, 0.0f );
            for ( size_t p = 0; p < tilePixels; ++p )
            {
                const float s = SpectralAnomaly::rxScore(
                    bip + p * static_cast<size_t>( bandCount ), stats.mean, invCov, bandCount,
                    &rxScratch );
                tileScores[p] = s;
                sumScores += s;
                if ( s > maxScore )
                    maxScore = s;
            }
            scoredPixels += tilePixels;
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
