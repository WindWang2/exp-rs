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
#include <ogr_srs_api.h>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

namespace {

constexpr int kTileDim = 256;
constexpr int kMaskHistogramBins = 65536;
constexpr double kDegToRad = 0.017453292519943295;

/// True when the WKT describes a geographic (lat/lon) CRS.
bool isGeographicCrs( const QString &wkt )
{
    if ( wkt.isEmpty() )
        return false;
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    if ( !srs )
        return false;
    const QByteArray wktBytes = wkt.toUtf8();
    char *wktPtr = const_cast<char *>( wktBytes.constData() );
    const bool geographic =
        ( OSRImportFromWkt( srs, &wktPtr ) == OGRERR_NONE && OSRIsGeographic( srs ) );
    OSRDestroySpatialReference( srs );
    return geographic;
}

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

// --- Streaming IR-MAD math -------------------------------------------------
//
// change_detection.cpp keeps the IR-MAD-specific numerics (sqrt-inverse, the
// per-iteration CCA, the Chi-square survival function) in its anonymous
// namespace, and the file is shared with the full-frame kernels, so the exact
// recipes are mirrored here. The streamed iteration performs the same
// accumulations in the same global pixel order as ChangeDetection::irMadChange,
// which makes the streaming result match the full-frame kernel bit-for-bit
// (verified by the operator tests against the kernel as oracle).

/// cv::SVD-based symmetric square-root inverse; eigenvalues <= 1e-12 are
/// zeroed. Same recipe as change_detection.cpp's madSqrtInv().
cv::Mat irMadSqrtInv( const cv::Mat &M )
{
    cv::Mat w, u, vt;
    cv::SVD::compute( M, w, u, vt );
    cv::Mat wInvSqrt = cv::Mat::zeros( M.rows, M.cols, CV_64F );
    for ( int i = 0; i < M.rows; ++i )
    {
        const double val = w.at<double>( i );
        wInvSqrt.at<double>( i, i ) = ( val > 1e-12 ) ? ( 1.0 / std::sqrt( val ) ) : 0.0;
    }
    return u * wInvSqrt * vt;
}

/// Chi-square survival function P(X_k > x); verbatim the change_detection.cpp
/// anonymous-namespace helper so streamed IR-MAD weights match the kernel's.
double chiSquareUpperCdf( double k, double x )
{
    if ( x <= 0.0 ) return 1.0;
    if ( k <= 0.0 ) return 0.0;
    const double a = k * 0.5;
    const double z = x * 0.5;
    if ( k == 2.0 )
    {
        return std::exp( -z );
    }
    if ( k == 1.0 )
    {
        return std::erfc( std::sqrt( z ) );
    }
    if ( z < a + 1.0 )
    {
        double sum = 1.0 / a;
        double term = 1.0 / a;
        for ( int n = 1; n < 100; ++n )
        {
            term *= z / ( a + n );
            sum += term;
            if ( term < sum * 1e-12 ) break;
        }
        double lower = sum * std::exp( -z + a * std::log( z ) - std::lgamma( a ) );
        return std::clamp( 1.0 - lower, 0.0, 1.0 );
    }
    else
    {
        double b = z + 1.0 - a;
        double c = 1.0 / 1e-30;
        double d = 1.0 / b;
        double h = d;
        for ( int n = 1; n < 100; ++n )
        {
            double an = -static_cast<double>( n ) * ( static_cast<double>( n ) - a );
            b += 2.0;
            d = an * d + b;
            if ( std::abs( d ) < 1e-30 ) d = 1e-30;
            c = b + an / c;
            if ( std::abs( c ) < 1e-30 ) c = 1e-30;
            d = 1.0 / d;
            double delta = d * c;
            h *= delta;
            if ( std::abs( delta - 1.0 ) < 1e-12 ) break;
        }
        double q = std::exp( -z + a * std::log( z ) - std::lgamma( a ) ) * h;
        return std::clamp( q, 0.0, 1.0 );
    }
}

/// Per-pixel IR-MAD Chi-square for one iteration's canonical variates:
/// Z = sum_k (u_k - v_k)^2 / varMad_k with u = A^T (x - meanX),
/// v = B^T (y - meanY). @a beforeBip / @a afterBip are BIP tiles and @p p the
/// in-tile pixel index; the caller has checked the pixel's validity. Same
/// operation order as ChangeDetection::irMadChange's transform loops.
double irMadPixelChiSquare( const float *beforeBip, const float *afterBip,
                            const cv::Mat &A, const cv::Mat &Bmat,
                            const std::vector<double> &meanX, const std::vector<double> &meanY,
                            const std::vector<double> &varMad,
                            int bandCount, size_t p )
{
    const size_t B = static_cast<size_t>( bandCount );
    double chiSquare = 0.0;
    for ( int k = 0; k < bandCount; ++k )
    {
        double uk = 0.0, vk = 0.0;
        for ( int a = 0; a < bandCount; ++a )
        {
            uk += A.at<double>( a, k ) * ( static_cast<double>( beforeBip[p * B + static_cast<size_t>( a )] ) - meanX[static_cast<size_t>( a )] );
            vk += Bmat.at<double>( a, k ) * ( static_cast<double>( afterBip[p * B + static_cast<size_t>( a )] ) - meanY[static_cast<size_t>( a )] );
        }
        const double m = uk - vk;
        chiSquare += ( m * m ) / varMad[static_cast<size_t>( k )];
    }
    return chiSquare;
}

/**
 * Reads one tile of both datasets into band-interleaved-by-pixel buffers
 * (bip[p * bandCount + band]). @p beforeBands / @p afterBands are parallel
 * 1-based band lists (same length); single-band metrics pass the configured
 * before/after band, multi-band metrics pass 1..bandCount on both. Called only
 * with in-extent windows — edge tiles are clamped to the remaining width/height
 * by the caller. Declared NoData sentinels and non-finite pixels are NaN-ized
 * (matching GdalDatasetWrapper::readBandMasked, #444/#679). Returns false on
 * any failed band read.
 */
bool readTileBip( const GdalDatasetWrapper &beforeDs, const GdalDatasetWrapper &afterDs,
                  const std::vector<int> &beforeBands, const std::vector<int> &afterBands,
                  int xOff, int yOff, int w, int h,
                  std::vector<float> &beforeBip, std::vector<float> &afterBip,
                  std::vector<float> &bandScratch )
{
    const int bandCount = static_cast<int>( beforeBands.size() );
    const size_t tilePixels = static_cast<size_t>( w ) * h;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for ( int b = 0; b < bandCount; ++b )
    {
        const int bb = beforeBands[static_cast<size_t>( b )];
        const int ab = afterBands[static_cast<size_t>( b )];
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
                // Infinite declared NoData (±inf sentinel): sweep EVERY
                // non-finite sample (inf sentinel and NaN alike) to NaN —
                // an isnan-only test would let ±inf pass through as a
                // "value" (#720).
                for ( size_t p = 0; p < tilePixels; ++p )
                    if ( !std::isfinite( bandScratch[p] ) ) bandScratch[p] = nan;
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
                // Infinite declared NoData (±inf sentinel): see before-copy
                // comment (#720).
                for ( size_t p = 0; p < tilePixels; ++p )
                    if ( !std::isfinite( bandScratch[p] ) ) bandScratch[p] = nan;
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

    // 1-based band lists for readTileBip: multi-band metrics interleave all
    // bands; single-band metrics only the configured before/after pair.
    std::vector<int> beforeBands, afterBands;
    beforeBands.reserve( static_cast<size_t>( bandCount ) );
    afterBands.reserve( static_cast<size_t>( bandCount ) );
    for ( int b = 0; b < bandCount; ++b )
    {
        beforeBands.push_back( multiBand ? b + 1 : opts.beforeBand );
        afterBands.push_back( multiBand ? b + 1 : opts.afterBand );
    }

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
                if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
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
            if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
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
                    // #700: NaN for before <= 0 — negative `before` (water
                    // after atmospheric correction) produced sign-flipped
                    // "ratios" that Otsu reads as huge change; the log-ratio
                    // metric clamps negatives, so stay consistent here.
                    for ( size_t p = 0; p < n; ++p )
                    {
                        tileOut[p] = ( beforeBip[p] <= 0.0f )
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
        double pixelArea = std::abs( gt[1] * gt[5] );
        // Geographic CRS: |gt[1]|·|gt[5]| counts square DEGREES, not m². Convert
        // with scene-centre arc lengths (Snyder) so changedArea is always m²,
        // matching the projected-CRS unit (#700).
        if ( pixelArea > 0.0 && isGeographicCrs( beforeDs.projection() ) )
        {
            const double phiDeg = gt[3] + ( height / 2.0 ) * gt[5];
            const double phiRad = phiDeg * kDegToRad;
            const double mPerDegLat =
                111132.92 - 559.82 * std::cos( 2 * phiRad ) + 1.175 * std::cos( 4 * phiRad );
            const double mPerDegLon =
                111412.84 * std::cos( phiRad ) - 93.5 * std::cos( 3 * phiRad );
            pixelArea = std::abs( gt[1] ) * mPerDegLon * std::abs( gt[5] ) * mPerDegLat;
        }
        if ( pixelArea > 0.0 )
        {
            result["changedArea"] = static_cast<double>( derived.changed ) * pixelArea;
            result["changedAreaUnit"] = "m2";
        }
    }
    context.reportProgress( 1.0, "Change detection complete" );
    return result;
}

// ---------------------------------------------------------------------------
// Streaming change atoms: rs:change_log_ratio / rs:change_cva_angle /
// rs:change_sam / rs:change_irmad. All four reuse readTileBip (masked 256x256
// BIP tile reads), StreamingMagnitudeStats and the guard-protected streamed
// GeoTIFF output; no full input or output frame is materialized (the one
// documented exception is IR-MAD's per-pixel weight frame, see the header).
// ---------------------------------------------------------------------------

Json::Value runLogRatioStreaming( const GdalDatasetWrapper &beforeDs,
                                  const GdalDatasetWrapper &afterDs,
                                  int width, int height,
                                  const ChangeAtomStreamingOptions &opts,
                                  RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    std::vector<float> beforeBip( maxTilePixels ), afterBip( maxTilePixels );
    std::vector<float> bandScratch( maxTilePixels ), tileOut( maxTilePixels );
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Matches ChangeDetection::logRatio's epsilon handling.
    const float epsilon = ( opts.epsilon > 0.0f ) ? opts.epsilon : 1e-4f;
    const std::vector<int> beforeBands{ opts.beforeBand };
    const std::vector<int> afterBands{ opts.afterBand };

    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( opts.outputPath ), width, height,
                                           1, static_cast<int>( GDT_Float32 ),
                                           beforeDs.geoTransform(), beforeDs.projection(), &outErr );
    if ( !outDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create log-ratio output raster: " +
                                   outErr.toStdString() );
    }
    DatasetFileGuard outGuard{ outDs, opts.outputPath, false };
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    if ( outBand )
        GDALSetRasterNoDataValue( outBand, std::numeric_limits<double>::quiet_NaN() );

    StreamingMagnitudeStats stats;
    context.reportProgress( 0.5, "Computing log-ratio change" );
    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
                               x, y, w, h, beforeBip, afterBip, bandScratch ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read input tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            // ChangeDetection::logRatio, per tile.
            for ( size_t p = 0; p < n; ++p )
            {
                const float b = beforeBip[p];
                const float a = afterBip[p];
                if ( !std::isfinite( b ) || !std::isfinite( a ) )
                {
                    tileOut[p] = nan;
                    continue;
                }
                const double valBefore =
                    std::max( static_cast<double>( b ), 0.0 ) + static_cast<double>( epsilon );
                const double valAfter =
                    std::max( static_cast<double>( a ), 0.0 ) + static_cast<double>( epsilon );
                tileOut[p] = static_cast<float>( std::log( valAfter ) - std::log( valBefore ) );
            }
            if ( GDALRasterIO( outBand, GF_Write, x, y, w, h, tileOut.data(),
                               w, h, GDT_Float32, 0, 0 ) != CE_None )
            {
                throw RSOperatorError( ErrorCode::FileNotWritable,
                                       "Failed to write log-ratio tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            for ( size_t p = 0; p < n; ++p )
                stats.add( tileOut[p] );
        }
    }
    GDALClose( outDs );
    outGuard.ds = nullptr;
    outGuard.committed = true;

    context.reportProgress( 1.0, "Log-ratio change complete" );
    Json::Value result( Json::objectValue );
    result["output"] = opts.outputPath;
    result["method"] = "log_ratio";
    result["width"] = width;
    result["height"] = height;
    result["mean"] = static_cast<float>( stats.mean );
    result["stddev"] = static_cast<float>( stats.stddev() );
    return result;
}

Json::Value runCvaAngleStreaming( const GdalDatasetWrapper &beforeDs,
                                  const GdalDatasetWrapper &afterDs,
                                  int width, int height,
                                  const ChangeAtomStreamingOptions &opts,
                                  RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    std::vector<float> beforeBip( maxTilePixels * 2 ), afterBip( maxTilePixels * 2 );
    std::vector<float> bandScratch( maxTilePixels );
    std::vector<float> b1( maxTilePixels ), b2( maxTilePixels );
    std::vector<float> a1( maxTilePixels ), a2( maxTilePixels );
    std::vector<float> magBuf( maxTilePixels ), tileOut( maxTilePixels );
    std::vector<uint8_t> quadBuf( maxTilePixels );
    const std::vector<int> beforeBands{ opts.beforeBand, opts.beforeBand2 };
    const std::vector<int> afterBands{ opts.afterBand, opts.afterBand2 };
    const bool quadrant = ( opts.mode == "quadrant" );

    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( opts.outputPath ), width, height,
                                           1, static_cast<int>( GDT_Float32 ),
                                           beforeDs.geoTransform(), beforeDs.projection(), &outErr );
    if ( !outDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create CVA angle output raster: " +
                                   outErr.toStdString() );
    }
    DatasetFileGuard outGuard{ outDs, opts.outputPath, false };
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    if ( outBand )
    {
        // Quadrant mode declares 255 as NoData (invalid pixels carry 255, like
        // the full-frame path); angle mode declares NaN.
        GDALSetRasterNoDataValue( outBand, quadrant ? 255.0
                                                    : std::numeric_limits<double>::quiet_NaN() );
    }

    context.reportProgress( 0.5, "Computing CVA angle" );
    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
                               x, y, w, h, beforeBip, afterBip, bandScratch ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read input tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            // De-interleave the 2-band BIP tile into the band-major buffers
            // the full-frame kernels expect.
            for ( size_t p = 0; p < n; ++p )
            {
                b1[p] = beforeBip[p * 2];
                b2[p] = beforeBip[p * 2 + 1];
                a1[p] = afterBip[p * 2];
                a2[p] = afterBip[p * 2 + 1];
            }
            QString err;
            if ( quadrant )
            {
                if ( !ChangeDetection::cvaQuadrant( b1.data(), b2.data(), a1.data(), a2.data(),
                                                    n, quadBuf.data(), &err ) )
                {
                    throw RSOperatorError( ErrorCode::ComputationError,
                                           "CVA quadrant calculation failed: " + err.toStdString() );
                }
                for ( size_t p = 0; p < n; ++p )
                    tileOut[p] = static_cast<float>( quadBuf[p] );
            }
            else
            {
                // The magnitude is a per-tile scratch byproduct; only the
                // angle is written.
                if ( !ChangeDetection::cvaMagnitudeAndAngle( b1.data(), b2.data(), a1.data(), a2.data(),
                                                             n, magBuf.data(), tileOut.data(), &err ) )
                {
                    throw RSOperatorError( ErrorCode::ComputationError,
                                           "CVA angle calculation failed: " + err.toStdString() );
                }
            }
            if ( GDALRasterIO( outBand, GF_Write, x, y, w, h, tileOut.data(),
                               w, h, GDT_Float32, 0, 0 ) != CE_None )
            {
                throw RSOperatorError( ErrorCode::FileNotWritable,
                                       "Failed to write CVA angle tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
        }
    }
    GDALClose( outDs );
    outGuard.ds = nullptr;
    outGuard.committed = true;

    context.reportProgress( 1.0, "CVA angle complete" );
    Json::Value result( Json::objectValue );
    result["output"] = opts.outputPath;
    result["method"] = "cva_angle";
    result["mode"] = opts.mode;
    result["width"] = width;
    result["height"] = height;
    return result;
}

Json::Value runSamStreaming( const GdalDatasetWrapper &beforeDs,
                             const GdalDatasetWrapper &afterDs,
                             int width, int height,
                             const ChangeAtomStreamingOptions &opts,
                             RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const int bandCount = beforeDs.bandCount();
    const size_t B = static_cast<size_t>( bandCount );
    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;
    std::vector<float> beforeBip( maxTilePixels * B ), afterBip( maxTilePixels * B );
    std::vector<float> bandScratch( maxTilePixels ), tileOut( maxTilePixels );
    // Per-tile band-major buffers + pointers for ChangeDetection::samChangeAngle.
    std::vector<std::vector<float>> beforeBandBufs( B ), afterBandBufs( B );
    for ( size_t b = 0; b < B; ++b )
    {
        beforeBandBufs[b].resize( maxTilePixels );
        afterBandBufs[b].resize( maxTilePixels );
    }
    std::vector<const float *> bPtrs( B ), aPtrs( B );
    std::vector<int> beforeBands, afterBands;
    beforeBands.reserve( B );
    afterBands.reserve( B );
    for ( size_t b = 0; b < B; ++b )
    {
        beforeBands.push_back( static_cast<int>( b ) + 1 );
        afterBands.push_back( static_cast<int>( b ) + 1 );
    }

    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( opts.outputPath ), width, height,
                                           1, static_cast<int>( GDT_Float32 ),
                                           beforeDs.geoTransform(), beforeDs.projection(), &outErr );
    if ( !outDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create SAM output raster: " +
                                   outErr.toStdString() );
    }
    DatasetFileGuard outGuard{ outDs, opts.outputPath, false };
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    if ( outBand )
        GDALSetRasterNoDataValue( outBand, std::numeric_limits<double>::quiet_NaN() );

    StreamingMagnitudeStats stats;
    context.reportProgress( 0.5, "Computing SAM change" );
    for ( int y = 0; y < height; y += tile )
    {
        const int h = std::min( tile, height - y );
        for ( int x = 0; x < width; x += tile )
        {
            const int w = std::min( tile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
                               x, y, w, h, beforeBip, afterBip, bandScratch ) )
            {
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read input tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            for ( size_t b = 0; b < B; ++b )
            {
                bPtrs[b] = beforeBandBufs[b].data();
                aPtrs[b] = afterBandBufs[b].data();
            }
            for ( size_t p = 0; p < n; ++p )
            {
                for ( size_t b = 0; b < B; ++b )
                {
                    beforeBandBufs[b][p] = beforeBip[p * B + b];
                    afterBandBufs[b][p] = afterBip[p * B + b];
                }
            }
            QString err;
            if ( !ChangeDetection::samChangeAngle( bPtrs.data(), aPtrs.data(), bandCount,
                                                   n, tileOut.data(), &err ) )
            {
                throw RSOperatorError( ErrorCode::ComputationError,
                                       "SAM computation failed: " + err.toStdString() );
            }
            if ( GDALRasterIO( outBand, GF_Write, x, y, w, h, tileOut.data(),
                               w, h, GDT_Float32, 0, 0 ) != CE_None )
            {
                throw RSOperatorError( ErrorCode::FileNotWritable,
                                       "Failed to write SAM tile at (" +
                                           std::to_string( x ) + ", " + std::to_string( y ) + ")" );
            }
            for ( size_t p = 0; p < n; ++p )
                stats.add( tileOut[p] );
        }
    }
    GDALClose( outDs );
    outGuard.ds = nullptr;
    outGuard.committed = true;

    context.reportProgress( 1.0, "SAM change complete" );
    Json::Value result( Json::objectValue );
    result["output"] = opts.outputPath;
    result["method"] = "sam";
    result["width"] = width;
    result["height"] = height;
    result["mean"] = static_cast<float>( stats.mean );
    result["stddev"] = static_cast<float>( stats.stddev() );
    return result;
}

Json::Value runIrMadStreaming( const GdalDatasetWrapper &beforeDs,
                               const GdalDatasetWrapper &afterDs,
                               int width, int height,
                               const ChangeAtomStreamingOptions &opts,
                               RSOperatorContext &context )
{
    constexpr int tile = kTileDim;
    const int bandCount = beforeDs.bandCount();
    const size_t B = static_cast<size_t>( bandCount );
    const size_t maxTilePixels = static_cast<size_t>( tile ) * tile;

    // Mirrors ChangeDetection::irMadChange's parameter handling.
    const int maxIterations = std::clamp( opts.maxIterations, 1, 100 );
    const double convThreshold = ( opts.convThreshold > 0.0 ) ? opts.convThreshold : 1e-4;

    std::vector<int> beforeBands, afterBands;
    beforeBands.reserve( B );
    afterBands.reserve( B );
    for ( size_t b = 0; b < B; ++b )
    {
        beforeBands.push_back( static_cast<int>( b ) + 1 );
        afterBands.push_back( static_cast<int>( b ) + 1 );
    }

    std::vector<float> beforeBip( maxTilePixels * B ), afterBip( maxTilePixels * B );
    std::vector<float> bandScratch( maxTilePixels ), tileOut( maxTilePixels );
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // The per-pixel Chi-square weights are the only state that survives
    // between reweighting iterations; they are carried as one full-resolution
    // Float64 frame (8 B/px). Everything else streams 256x256 BIP tiles —
    // documented honestly in the header (still ~2B+6 frames less than the
    // legacy full-frame path).
    std::vector<double> weights( static_cast<size_t>( width ) * height, 1.0 );
    std::vector<double> prevRho( B, 0.0 );
    cv::Mat AFinal, BFinal;
    std::vector<double> varMadFinal( B, 1.0 );
    std::vector<double> meanXFinal( B, 0.0 ), meanYFinal( B, 0.0 );
    bool haveSolution = false;

    const auto forEachTile = [&]( const auto &fn ) {
        for ( int y = 0; y < height; y += tile )
        {
            const int h = std::min( tile, height - y );
            for ( int x = 0; x < width; x += tile )
            {
                const int w = std::min( tile, width - x );
                context.throwIfCancelled();
                if ( !readTileBip( beforeDs, afterDs, beforeBands, afterBands,
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

    // A pixel participates in the fit iff every before/after band is finite
    // (invalid pixels are NaN after the masked read). Same predicate as the
    // kernel's validIndices.
    const auto pixelValid = [&]( size_t p ) {
        for ( size_t b = 0; b < B; ++b )
        {
            if ( !std::isfinite( beforeBip[p * B + b] ) || !std::isfinite( afterBip[p * B + b] ) )
                return false;
        }
        return true;
    };

    for ( int iter = 0; iter < maxIterations; ++iter )
    {
        // Pass A: weighted sums (same accumulation order as the kernel).
        double sumW = 0.0;
        double sumW2 = 0.0;
        std::vector<double> sumX( B, 0.0 ), sumY( B, 0.0 );
        size_t iterValid = 0;
        forEachTile( [&]( int xOff, int yOff, int w, int h, size_t ) {
            for ( int row = 0; row < h; ++row )
            {
                for ( int col = 0; col < w; ++col )
                {
                    const size_t p = static_cast<size_t>( row ) * w + col;
                    if ( !pixelValid( p ) )
                        continue;
                    const double wgt =
                        weights[static_cast<size_t>( yOff + row ) * width + ( xOff + col )];
                    ++iterValid;
                    sumW += wgt;
                    sumW2 += wgt * wgt;
                    for ( size_t b = 0; b < B; ++b )
                    {
                        sumX[b] += wgt * static_cast<double>( beforeBip[p * B + b] );
                        sumY[b] += wgt * static_cast<double>( afterBip[p * B + b] );
                    }
                }
            }
        } );
        if ( iter == 0 && iterValid < B + 2 )
        {
            throw RSOperatorError(
                ErrorCode::ComputationError,
                "IR-MAD computation failed: Insufficient valid pixels for IR-MAD calculation." );
        }
        if ( sumW <= 1e-12 )
            break;

        const double invSumW = 1.0 / sumW;
        std::vector<double> meanX( B ), meanY( B );
        for ( size_t b = 0; b < B; ++b )
        {
            meanX[b] = sumX[b] * invSumW;
            meanY[b] = sumY[b] * invSumW;
        }

        // Pass B: weighted covariance matrices.
        double denom = sumW - ( sumW2 / sumW );
        if ( denom < 1.0 )
            denom = 1.0;
        const double covScale = 1.0 / denom;

        cv::Mat SXX = cv::Mat::zeros( bandCount, bandCount, CV_64F );
        cv::Mat SYY = cv::Mat::zeros( bandCount, bandCount, CV_64F );
        cv::Mat SXY = cv::Mat::zeros( bandCount, bandCount, CV_64F );
        std::vector<double> cx( B ), cy( B );
        forEachTile( [&]( int xOff, int yOff, int w, int h, size_t ) {
            for ( int row = 0; row < h; ++row )
            {
                for ( int col = 0; col < w; ++col )
                {
                    const size_t p = static_cast<size_t>( row ) * w + col;
                    if ( !pixelValid( p ) )
                        continue;
                    const double wgt =
                        weights[static_cast<size_t>( yOff + row ) * width + ( xOff + col )];
                    for ( size_t b = 0; b < B; ++b )
                    {
                        cx[b] = static_cast<double>( beforeBip[p * B + b] ) - meanX[b];
                        cy[b] = static_cast<double>( afterBip[p * B + b] ) - meanY[b];
                    }
                    for ( size_t r = 0; r < B; ++r )
                    {
                        for ( size_t c = 0; c < B; ++c )
                        {
                            SXX.at<double>( static_cast<int>( r ), static_cast<int>( c ) ) +=
                                wgt * cx[r] * cx[c];
                            SYY.at<double>( static_cast<int>( r ), static_cast<int>( c ) ) +=
                                wgt * cy[r] * cy[c];
                            SXY.at<double>( static_cast<int>( r ), static_cast<int>( c ) ) +=
                                wgt * cx[r] * cy[c];
                        }
                    }
                }
            }
        } );
        SXX *= covScale;
        SYY *= covScale;
        SXY *= covScale;

        // Trace-scaled diagonal regularization, sqrt-inverse, CCA (kernel recipe).
        const double epsXX = 1e-6 * cv::trace( SXX )[0] / static_cast<double>( bandCount );
        const double epsYY = 1e-6 * cv::trace( SYY )[0] / static_cast<double>( bandCount );
        for ( int b = 0; b < bandCount; ++b )
        {
            SXX.at<double>( b, b ) += std::max( epsXX, 1e-12 );
            SYY.at<double>( b, b ) += std::max( epsYY, 1e-12 );
        }

        const cv::Mat SXXInvSqrt = irMadSqrtInv( SXX );
        const cv::Mat SYYInvSqrt = irMadSqrtInv( SYY );
        if ( cv::countNonZero( SXXInvSqrt ) == 0 || cv::countNonZero( SYYInvSqrt ) == 0 )
            break;

        const cv::Mat H = SXXInvSqrt * SXY * SYYInvSqrt;
        cv::Mat D, Uh, VhT;
        cv::SVD::compute( H, D, Uh, VhT );

        cv::Mat A = SXXInvSqrt * Uh;
        cv::Mat Bmat = SYYInvSqrt * VhT.t();

        // Ensure canonical variate pairs are positively correlated.
        for ( int k = 0; k < bandCount; ++k )
        {
            const cv::Mat covK = A.col( k ).t() * SXY * Bmat.col( k );
            if ( covK.at<double>( 0, 0 ) < 0.0 )
                Bmat.col( k ) *= -1.0;
        }

        std::vector<double> curRho( B ), curVarMad( B );
        for ( size_t k = 0; k < B; ++k )
        {
            curRho[k] = std::clamp( D.at<double>( static_cast<int>( k ) ), 0.0, 1.0 );
            curVarMad[k] = std::max( 2.0 * ( 1.0 - curRho[k] ), 1e-6 );
        }

        AFinal = A;
        BFinal = Bmat;
        varMadFinal = curVarMad;
        meanXFinal = meanX;
        meanYFinal = meanY;
        haveSolution = true;

        double maxDeltaRho = 0.0;
        for ( size_t k = 0; k < B; ++k )
            maxDeltaRho = std::max( maxDeltaRho, std::abs( curRho[k] - prevRho[k] ) );
        prevRho = curRho;

        if ( iter > 0 && maxDeltaRho < convThreshold )
            break; // converged

        if ( iter + 1 < maxIterations )
        {
            // Pass C: refresh the Chi-square weights for the next iteration.
            forEachTile( [&]( int xOff, int yOff, int w, int h, size_t ) {
                for ( int row = 0; row < h; ++row )
                {
                    for ( int col = 0; col < w; ++col )
                    {
                        const size_t p = static_cast<size_t>( row ) * w + col;
                        if ( !pixelValid( p ) )
                            continue;
                        const double z = irMadPixelChiSquare(
                            beforeBip.data(), afterBip.data(), A, Bmat,
                            meanX, meanY, curVarMad, bandCount, p );
                        weights[static_cast<size_t>( yOff + row ) * width + ( xOff + col )] =
                            chiSquareUpperCdf( static_cast<double>( bandCount ), z );
                    }
                }
            } );
        }
        context.reportProgress(
            0.8 * static_cast<double>( iter + 1 ) / static_cast<double>( maxIterations ),
            "IR-MAD iteration " + std::to_string( iter + 1 ) );
    }

    if ( !haveSolution )
    {
        throw RSOperatorError(
            ErrorCode::ComputationError,
            "IR-MAD computation failed: IR-MAD failed: degenerate weights or "
            "empty transformation matrix" );
    }

    // Final transform pass: stream once more and write the output.
    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( opts.outputPath ), width, height,
                                           1, static_cast<int>( GDT_Float32 ),
                                           beforeDs.geoTransform(), beforeDs.projection(), &outErr );
    if ( !outDs )
    {
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create IR-MAD output raster: " +
                                   outErr.toStdString() );
    }
    DatasetFileGuard outGuard{ outDs, opts.outputPath, false };
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    if ( outBand )
        GDALSetRasterNoDataValue( outBand, std::numeric_limits<double>::quiet_NaN() );

    StreamingMagnitudeStats stats;
    context.reportProgress( 0.9, "Writing IR-MAD chi-square" );
    forEachTile( [&]( int xOff, int yOff, int w, int h, size_t ) {
        for ( int row = 0; row < h; ++row )
        {
            for ( int col = 0; col < w; ++col )
            {
                const size_t p = static_cast<size_t>( row ) * w + col;
                if ( !pixelValid( p ) )
                {
                    tileOut[p] = nan;
                    continue;
                }
                tileOut[p] = static_cast<float>( irMadPixelChiSquare(
                    beforeBip.data(), afterBip.data(), AFinal, BFinal,
                    meanXFinal, meanYFinal, varMadFinal, bandCount, p ) );
            }
        }
        if ( GDALRasterIO( outBand, GF_Write, xOff, yOff, w, h, tileOut.data(),
                           w, h, GDT_Float32, 0, 0 ) != CE_None )
        {
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to write IR-MAD tile at (" +
                                       std::to_string( xOff ) + ", " + std::to_string( yOff ) + ")" );
        }
        const size_t n = static_cast<size_t>( w ) * h;
        for ( size_t p = 0; p < n; ++p )
            stats.add( tileOut[p] );
    } );
    GDALClose( outDs );
    outGuard.ds = nullptr;
    outGuard.committed = true;

    context.reportProgress( 1.0, "IR-MAD complete" );
    Json::Value result( Json::objectValue );
    result["output"] = opts.outputPath;
    result["method"] = "irmad";
    result["width"] = width;
    result["height"] = height;
    result["mean"] = static_cast<float>( stats.mean );
    result["stddev"] = static_cast<float>( stats.stddev() );
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
