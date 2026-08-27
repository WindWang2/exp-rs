/***************************************************************************
 * rs_change_streaming.cpp  —  Shared tile-streaming change-detection kernel
 ***************************************************************************/
#include "rs_change_streaming.h"

#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/math_utils.h"

#include <QFile>
#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

namespace {

constexpr int kTileDim = 256;
constexpr int kMaskHistogramBins = 65536;

struct DatasetFileGuard
{
    GDALDatasetH ds = nullptr;
    std::string path;
    bool committed = false;

    ~DatasetFileGuard()
    {
        if ( ds )
        {
            GDALClose( ds );
            ds = nullptr;
        }
        if ( !committed && !path.empty() )
        {
            QFile::remove( QString::fromStdString( path ) );
        }
    }
};

ChangeDetection::MorphOp morphOpFromName( const std::string &name )
{
    if ( name == "erode" ) return ChangeDetection::MorphOp::Erode;
    if ( name == "dilate" ) return ChangeDetection::MorphOp::Dilate;
    if ( name == "open" ) return ChangeDetection::MorphOp::Open;
    if ( name == "close" ) return ChangeDetection::MorphOp::Close;
    return ChangeDetection::MorphOp::None;
}

/**
 * Reads one tile of both datasets into band-interleaved-by-pixel buffers
 * (bip[p * bandCount + band]). For single-band metrics bandCount == 1 and only
 * the configured before/after bands are read. Called only with in-extent
 * windows — edge tiles are clamped to the remaining width/height by the
 * caller. Returns false on any failed band read.
 */
bool readTileBip( const GdalDatasetWrapper &beforeDs, const GdalDatasetWrapper &afterDs,
                  int bandCount, int beforeBand, int afterBand,
                  int xOff, int yOff, int w, int h,
                  std::vector<float> &beforeBip, std::vector<float> &afterBip,
                  std::vector<float> &bandScratch )
{
    const size_t tilePixels = static_cast<size_t>( w ) * h;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for ( int b = 0; b < bandCount; ++b )
    {
        const int bb = ( bandCount == 1 ) ? beforeBand : ( b + 1 );
        const int ab = ( bandCount == 1 ) ? afterBand : ( b + 1 );
        if ( !beforeDs.readBandWindow( bb, xOff, yOff, w, h, bandScratch.data() ) )
            return false;
        {
            bool hasNd = false;
            double nd = beforeDs.bandNoDataValue( bb, &hasNd );
            if ( hasNd && std::isfinite( nd ) ) {
                // Float-space compare: matches large sentinels (-3.4e38) exactly
                // where a double-space absolute tolerance never would (#444).
                const float ndF = static_cast<float>( nd );
                for ( size_t p = 0; p < tilePixels; ++p ) {
                    float v = bandScratch[p];
                    if ( !std::isfinite( v ) || v == ndF )
                        bandScratch[p] = nan;
                }
            } else if ( hasNd && !std::isfinite( nd ) ) {
                for ( size_t p = 0; p < tilePixels; ++p )
                    if ( std::isnan( bandScratch[p] ) ) bandScratch[p] = nan;
            }
        }
        for ( size_t p = 0; p < tilePixels; ++p )
            beforeBip[p * static_cast<size_t>( bandCount ) + static_cast<size_t>( b )] = bandScratch[p];
        if ( !afterDs.readBandWindow( ab, xOff, yOff, w, h, bandScratch.data() ) )
            return false;
        {
            bool hasNd = false;
            double nd = afterDs.bandNoDataValue( ab, &hasNd );
            if ( hasNd && std::isfinite( nd ) ) {
                // Float-space compare: matches large sentinels (-3.4e38) exactly
                // where a double-space absolute tolerance never would (#444).
                const float ndF = static_cast<float>( nd );
                for ( size_t p = 0; p < tilePixels; ++p ) {
                    float v = bandScratch[p];
                    if ( !std::isfinite( v ) || v == ndF )
                        bandScratch[p] = nan;
                }
            } else if ( hasNd && !std::isfinite( nd ) ) {
                for ( size_t p = 0; p < tilePixels; ++p )
                    if ( std::isnan( bandScratch[p] ) ) bandScratch[p] = nan;
            }
        }
        for ( size_t p = 0; p < tilePixels; ++p )
            afterBip[p * static_cast<size_t>( bandCount ) + static_cast<size_t>( b )] = bandScratch[p];
    }
    return true;
}

/**
 * Derives a binary change mask from the magnitude raster written to
 * @a magPath: streaming histogram + Welford stats → threshold strategy →
 * full-resolution mask → morphological cleanup → connected-component filter →
 * Byte output with change metadata. Shared by every metric's mask path so the
 * threshold/cleanup semantics live in exactly one place.
 *
 * @return the effective threshold and the changed/evaluated pixel counts
 *         computed from the in-memory mask (255 = NoData, 1 = changed).
 */
MaskDerivation writeMaskFromMagnitude( const std::string &magPath, const GdalDatasetWrapper &beforeDs,
                                       int width, int height, const ChangeStreamingOptions &opts,
                                       const StreamingMagnitudeStats &magStats,
                                       RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    std::vector<float> tileBuf( maxTilePixels );

    // Re-open the temp magnitude raster read-only and build the histogram
    // (min/max are final after the write pass, so binning is exact).
    GdalDatasetWrapper magDs;
    if ( !magDs.open( QString::fromStdString( magPath ) ) )
    {
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to reopen magnitude raster for masking" );
    }
    // In the threshold path magPath IS the original input, whose declared
    // NoData (e.g. -9999) must map to the mask's NoData (255), not compare
    // against the threshold as a value (#612). The magnitude raster's own
    // NoData is NaN, so this conversion is a no-op there.
    bool magHasNodata = false;
    const float magNodataF = [&]() {
        const double nd = magDs.bandNoDataValue( 1, &magHasNodata );
        return ( magHasNodata && std::isfinite( nd ) ) ? static_cast<float>( nd )
                                                       : std::numeric_limits<float>::quiet_NaN();
    }();
    const auto normalizeTile = [&]( std::vector<float> &buf, size_t n ) {
        if ( !magHasNodata || !std::isfinite( magNodataF ) )
            return;
        for ( size_t p = 0; p < n; ++p )
            if ( buf[p] == magNodataF )
                buf[p] = std::numeric_limits<float>::quiet_NaN();
    };
    std::vector<double> hist( static_cast<size_t>( kMaskHistogramBins ), 0.0 );
    size_t histFinite = 0;
    const double magRange = magStats.maxVal - magStats.minVal;
    if ( magStats.validCount > 0 && magRange > 0.0 )
    {
        for ( int y = 0; y < height; y += tile )
        {
            const int h = std::min( tile, height - y );
            for ( int x = 0; x < width; x += tile )
            {
                const int w = std::min( tile, width - x );
                const size_t n = static_cast<size_t>( w ) * h;
                context.throwIfCancelled();
                if ( !magDs.readBandWindow( 1, x, y, w, h, tileBuf.data() ) )
                {
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read magnitude tile" );
                }
                normalizeTile( tileBuf, n );
                for ( size_t p = 0; p < n; ++p )
                {
                    const double v = tileBuf[p];
                    if ( !std::isfinite( v ) )
                        continue;
                    ++histFinite;
                    int bin = static_cast<int>( ( v - magStats.minVal ) / magRange
                                                * ( kMaskHistogramBins - 1 ) );
                    bin = std::clamp( bin, 0, kMaskHistogramBins - 1 );
                    hist[static_cast<size_t>( bin )] += 1.0;
                }
            }
        }
    }

    float thresholdUsed = opts.threshold;
    // When every finite magnitude is identical (range == 0) there is nothing
    // to bin; otsu/percentile reduce to that single value.
    const bool invariant = magStats.validCount > 0 && magRange <= 0.0;
    if ( opts.thresholdMethod == "otsu" )
    {
        if ( invariant )
        {
            thresholdUsed = static_cast<float>( magStats.minVal );
        }
        else
        {
            float t = opts.threshold;
            if ( ChangeDetection::otsuThresholdFromHistogram( magStats.minVal, magStats.maxVal,
                                                              hist, histFinite, &t ) )
                thresholdUsed = t;
        }
    }
    else if ( opts.thresholdMethod == "percentile" )
    {
        if ( invariant )
        {
            thresholdUsed = static_cast<float>( magStats.minVal );
        }
        else
        {
            float t = opts.threshold;
            if ( ChangeDetection::percentileThresholdFromHistogram( magStats.minVal, magStats.maxVal,
                                                                    hist, histFinite,
                                                                    opts.percentile, &t ) )
                thresholdUsed = t;
        }
    }
    else if ( opts.thresholdMethod == "statistical" )
    {
        if ( magStats.validCount >= 2 && magStats.stddev() > 0.0 )
        {
            thresholdUsed = static_cast<float>(
                magStats.mean + opts.statisticalK * magStats.stddev() );
        }
        else
        {
            context.logWarning(
                "statistical threshold: not enough varying finite values; "
                "falling back to the manual threshold" );
        }
    }

    // Full-resolution mask (the mask path's pre-existing behavior): threshold
    // the magnitude tile-by-tile into a byte mask.
    const size_t pixelCount = static_cast<size_t>( width ) * height;
    std::vector<uint8_t> mask( pixelCount, 0 );
    std::vector<uint8_t> tileMask( maxTilePixels );
    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !magDs.readBandWindow( 1, x, y, w, h, tileBuf.data() ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read magnitude tile" );
            }
            normalizeTile( tileBuf, n );
            if ( !ChangeDetection::changeMask( tileBuf.data(), tileMask.data(), n, thresholdUsed ) )
            {
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "Change mask computation failed" );
            }
            for ( int dy = 0; dy < h; ++dy )
            {
                std::copy_n( tileMask.data() + static_cast<size_t>( dy ) * w, w,
                             mask.data() + static_cast<size_t>( y + dy ) * width + x );
            }
        }
    }
    magDs.close();

    ChangeDetection::morphologicalCleanup( mask.data(), width, height,
                                           opts.cleanupIterations,
                                           morphOpFromName( opts.cleanup ) );
    if ( opts.minAreaPixels > 0
         && !ChangeDetection::connectedComponentFilter( mask.data(), width, height,
                                                        static_cast<size_t>( opts.minAreaPixels ) ) )
    {
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Connected-component filter failed" );
    }

    context.reportProgress( 0.9, "Writing change mask" );
    QString maskErr;
    GDALDatasetH maskDs = createOutputTiff( QString::fromStdString( opts.outputPath ), width, height,
                                            1, static_cast<int>( GDT_Byte ),
                                            beforeDs.geoTransform(), beforeDs.projection(), &maskErr );
    if ( !maskDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create change mask: " + maskErr.toStdString() );
    }
    DatasetFileGuard maskGuard{ maskDs, opts.outputPath, false };
    GDALRasterBandH maskBand = GDALGetRasterBand( maskDs, 1 );
    if ( maskBand )
    {
        GDALSetRasterNoDataValue( maskBand, 255.0 );
    }
    const CPLErr writeErr = GDALRasterIO( maskBand, GF_Write, 0, 0, width, height,
                                          mask.data(), width, height, GDT_Byte, 0, 0 );
    GDALSetMetadataItem( maskDs, "SICNU_CHANGE_METHOD", opts.methodLabel.c_str(), nullptr );
    GDALSetMetadataItem( maskDs, "SICNU_CHANGE_THRESHOLD",
                         QString::number( thresholdUsed, 'g', 10 ).toUtf8().constData(), nullptr );
    if ( opts.minAreaPixels > 0 )
    {
        GDALSetMetadataItem( maskDs, "SICNU_CHANGE_MIN_AREA",
                             QByteArray::number( opts.minAreaPixels ).constData(), nullptr );
    }
    GDALClose( maskDs );
    maskGuard.ds = nullptr;
    if ( writeErr != CE_None )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to write change mask: " + opts.outputPath );
    }
    maskGuard.committed = true;

    // Changed-pixel statistics from the in-memory mask (255 = NoData).
    MaskDerivation derived;
    derived.thresholdUsed = thresholdUsed;
    for ( const uint8_t v : mask )
    {
        if ( v == 255 )
            continue;
        ++derived.evaluated;
        if ( v == 1 )
            ++derived.changed;
    }
    return derived;
}

} // anonymous namespace

Json::Value runChangeStreaming( const GdalDatasetWrapper &beforeDs,
                                const GdalDatasetWrapper &afterDs,
                                int width, int height, ChangeMetric metric,
                                const ChangeStreamingOptions &opts,
                                RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const bool multiBand = ( metric == ChangeMetric::Cva || metric == ChangeMetric::Mad );
    const int bandCount = multiBand ? beforeDs.bandCount() : 1;

    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    const size_t B = static_cast<size_t>( bandCount );
    std::vector<float> beforeBip( maxTilePixels * B );
    std::vector<float> afterBip( maxTilePixels * B );
    std::vector<float> bandScratch( maxTilePixels );
    std::vector<float> tileOut( maxTilePixels );
    const float nan = std::numeric_limits<float>::quiet_NaN();

    const size_t pixelCount = static_cast<size_t>( width ) * height;
    if ( opts.makeMask && pixelCount > static_cast<size_t>( std::numeric_limits<std::int32_t>::max() ) )
    {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "mask path requires a full-resolution mask; raster too large "
            "(would exceed 2^31 pixels)" );
    }

    // Tile iteration shared by the read-only passes.
    auto forEachTile = [&]( const auto &fn ) {
        for ( int y = 0; y < height; y += tile )
        {
            const int h = std::min( tile, height - y );
            for ( int x = 0; x < width; x += tile )
            {
                const int w = std::min( tile, width - x );
                context.throwIfCancelled();
                if ( !readTileBip( beforeDs, afterDs, bandCount, opts.beforeBand, opts.afterBand,
                                   x, y, w, h, beforeBip, afterBip, bandScratch ) )
                {
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read input tile at (" +
                                               std::to_string( x ) + ", " + std::to_string( y ) + ")" );
                }
                fn( x, y, w, h, static_cast<size_t>( w ) * h );
            }
        }
    };

    QString calcError;

    // --- MAD passes 1 & 2: covariance accumulation -------------------------
    ChangeDetection::MadStreamingState madState;
    if ( metric == ChangeMetric::Mad )
    {
        forEachTile( [&]( int, int, int, int, size_t n ) {
            if ( !ChangeDetection::madAccumulateSums( beforeBip.data(), afterBip.data(),
                                                      n, bandCount, &madState ) )
            {
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "MAD sum accumulation failed" );
            }
        } );
        if ( !ChangeDetection::madFinalizeMeans( &madState, &calcError ) )
        {
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "MAD computation failed: " + calcError.toStdString() );
        }
        context.reportProgress( 0.5, "Computing MAD statistics" );

        forEachTile( [&]( int, int, int, int, size_t n ) {
            if ( !ChangeDetection::madAccumulateCentered( beforeBip.data(), afterBip.data(),
                                                          n, bandCount, &madState ) )
            {
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "MAD covariance accumulation failed" );
            }
        } );
        if ( !ChangeDetection::madFinalize( &madState, &calcError ) )
        {
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "MAD computation failed: " + calcError.toStdString() );
        }
        context.reportProgress( 0.6, "MAD coefficients ready" );
    }

    // --- Magnitude write pass ----------------------------------------------
    const std::string magPath = opts.makeMask ? context.tempPath( ".tif" ) : opts.outputPath;
    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( magPath ), width, height,
                                           1, static_cast<int>( GDT_Float32 ),
                                           beforeDs.geoTransform(), beforeDs.projection(), &outErr );
    if ( !outDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create change magnitude raster: " +
                                   outErr.toStdString() );
    }
    DatasetFileGuard magGuard{ outDs, magPath, false };
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    if ( outBand )
        GDALSetRasterNoDataValue( outBand, std::numeric_limits<double>::quiet_NaN() );

    StreamingMagnitudeStats magStats;
    context.reportProgress( ( metric == ChangeMetric::Mad ) ? 0.7 : 0.5,
                            "Computing " + opts.methodLabel + " magnitude" );

    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !readTileBip( beforeDs, afterDs, bandCount, opts.beforeBand, opts.afterBand,
                               x, y, w, h, beforeBip, afterBip, bandScratch ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read input tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }

            switch ( metric )
            {
                case ChangeMetric::Mad:
                    ChangeDetection::madTransformTile( beforeBip.data(), afterBip.data(),
                                                       n, bandCount, madState, tileOut.data() );
                    break;
                case ChangeMetric::Cva:
                {
                    // CVA magnitude: a NaN delta in any band propagates to a
                    // NaN pixel; otherwise sqrt(sum of squared deltas).
                    for ( size_t p = 0; p < n; ++p )
                    {
                        double sumSq = 0.0;
                        bool hasNan = false;
                        for ( int b = 0; b < bandCount; ++b )
                        {
                            const float d = afterBip[p * B + static_cast<size_t>( b )]
                                          - beforeBip[p * B + static_cast<size_t>( b )];
                            if ( std::isnan( d ) )
                            {
                                hasNan = true;
                                break;
                            }
                            sumSq += static_cast<double>( d ) * static_cast<double>( d );
                        }
                        tileOut[p] = hasNan ? nan : static_cast<float>( std::sqrt( sumSq ) );
                    }
                    break;
                }
                case ChangeMetric::Difference:
                    for ( size_t p = 0; p < n; ++p )
                    {
                        const float d = afterBip[p] - beforeBip[p];
                        tileOut[p] = std::isnan( d ) ? nan : d;
                    }
                    break;
                case ChangeMetric::AbsoluteDifference:
                    // Legacy facade "difference" semantics: |after - before|
                    // (non-negative change magnitude; a NaN delta propagates).
                    for ( size_t p = 0; p < n; ++p )
                    {
                        const float d = afterBip[p] - beforeBip[p];
                        tileOut[p] = std::isnan( d ) ? nan : std::fabs( d );
                    }
                    break;
                case ChangeMetric::NormalizedDifference:
                    for ( size_t p = 0; p < n; ++p )
                    {
                        tileOut[p] = MathUtils::safeDiv( afterBip[p] - beforeBip[p],
                                                         afterBip[p] + beforeBip[p] );
                    }
                    break;
                case ChangeMetric::Ratio:
                    for ( size_t p = 0; p < n; ++p )
                    {
                        tileOut[p] = ( beforeBip[p] == 0.0f )
                                       ? nan
                                       : afterBip[p] / beforeBip[p];
                    }
                    break;
            }

            if ( GDALRasterIO( outBand, GF_Write, x, y, w, h, tileOut.data(),
                               w, h, GDT_Float32, 0, 0 ) != CE_None )
            {
                throw RSOperatorError( ErrorCode::FileNotWritable,
                                       "Failed to write change magnitude tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            for ( size_t p = 0; p < n; ++p )
                magStats.add( tileOut[p] );
        }
    }
    GDALClose( outDs );
    magGuard.ds = nullptr;
    magGuard.committed = true;

    // --- Non-mask path: the magnitude raster is the output. ----------------
    if ( !opts.makeMask )
    {
        Json::Value result( Json::objectValue );
        result["output"] = opts.outputPath;
        result["method"] = opts.methodLabel;
        result["mean"] = static_cast<float>( magStats.mean );
        result["stddev"] = static_cast<float>( magStats.stddev() );
        context.reportProgress( 1.0, "Change detection complete" );
        return result;
    }

    // --- Mask path: threshold from the streaming stats, then the mask. -----
    context.reportProgress( 0.8, "Computing change threshold" );
    MaskDerivation derived;
    try
    {
        derived = writeMaskFromMagnitude( magPath, beforeDs, width, height, opts, magStats, context );
        QFile::remove( QString::fromStdString( magPath ) );
    }
    catch ( ... )
    {
        QFile::remove( QString::fromStdString( magPath ) );
        throw;
    }

    Json::Value result( Json::objectValue );
    result["output"] = opts.outputPath;
    result["method"] = opts.methodLabel;
    result["thresholdUsed"] = derived.thresholdUsed;
    result["changedPixels"] = static_cast<Json::UInt64>( derived.changed );
    result["totalPixels"] = static_cast<Json::UInt64>( derived.evaluated );
    result["changedPercent"] = derived.evaluated == 0
        ? 0.0
        : 100.0 * static_cast<double>( derived.changed ) / static_cast<double>( derived.evaluated );
    result["mean"] = static_cast<float>( magStats.mean );
    result["stddev"] = static_cast<float>( magStats.stddev() );
    if ( beforeDs.hasGeoTransform() )
    {
        const auto gt = beforeDs.geoTransform();
        const double pixelArea = std::abs( gt[1] * gt[5] );
        if ( pixelArea > 0.0 )
            result["changedArea"] = static_cast<double>( derived.changed ) * pixelArea;
    }
    context.reportProgress( 1.0, "Change detection complete" );
    return result;
}

MaskDerivation thresholdRasterToMask( const std::string &inputPath,
                                      const ChangeStreamingOptions &opts,
                                      RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    ensureGdalInit();

    GdalDatasetWrapper inputDs;
    if ( !inputDs.open( QString::fromStdString( inputPath ) ) )
    {
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open input raster: " + inputPath );
    }
    const int width = inputDs.width();
    const int height = inputDs.height();
    if ( inputDs.bandCount() < 1 )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Threshold input raster has no bands: " + inputPath );
    }
    if ( inputDs.bandCount() > 1 )
    {
        context.logWarning( "Threshold input has " + std::to_string( inputDs.bandCount() )
                            + " bands; only band 1 is used." );
    }

    // The mask derivation materializes a full-resolution byte mask (the
    // cleanup/MMU path's pre-existing behavior) — reject rasters that would
    // exceed 2^31 pixels instead of exhausting memory.
    const size_t pixelCount = static_cast<size_t>( width ) * height;
    if ( pixelCount > static_cast<size_t>( std::numeric_limits<std::int32_t>::max() ) )
    {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "Threshold mask path requires a full-resolution mask; raster too "
            "large (would exceed 2^31 pixels)" );
    }

    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    std::vector<float> tileBuf( maxTilePixels );

    StreamingMagnitudeStats magStats;
    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !inputDs.readBandWindow( 1, x, y, w, h, tileBuf.data() ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read input tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            {
                bool hasNd = false;
                double nd = inputDs.bandNoDataValue( 1, &hasNd );
                if ( hasNd && std::isfinite( nd ) ) {
                    const float ndF = static_cast<float>( nd );
                    for ( size_t p = 0; p < n; ++p ) {
                        float v = tileBuf[p];
                        if ( !std::isfinite( v ) || v == ndF )
                            tileBuf[p] = std::numeric_limits<float>::quiet_NaN();
                    }
                }
            }
            for ( size_t p = 0; p < n; ++p )
                magStats.add( tileBuf[p] );
        }
    }

    return writeMaskFromMagnitude( inputPath, inputDs, width, height, opts, magStats, context );
}

} // namespace sicnu::operators::rs
