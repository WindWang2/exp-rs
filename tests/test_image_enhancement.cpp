#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/chunked_processor.h"
#include <QThread>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

using namespace Catch;

TEST_CASE("Linear min-max stretch", "[enhancement]") {
    std::vector<float> input = {0, 25, 50, 75, 100};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 0.0f, 100.0f);
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[2] == Approx(127.5f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Percentage clip stretch", "[enhancement]") {
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++) input[i] = static_cast<float>(i);
    std::vector<float> output(100);
    ImageEnhancement::percentClipStretch(input.data(), output.data(), 100, 5.0f);
    REQUIRE(output[5] == Approx(0.0f).margin(1.0f));
    REQUIRE(output[94] == Approx(255.0f).margin(1.0f));
}

TEST_CASE("Standard deviation stretch", "[enhancement]") {
    std::vector<float> input = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<float> output(10);
    ImageEnhancement::stddevStretch(input.data(), output.data(), 10, 2.0f);
    REQUIRE(output[0] >= 0.0f);
    REQUIRE(output[9] <= 255.0f);
}

TEST_CASE("Histogram equalization", "[enhancement]") {
    std::vector<float> input = {1, 1, 1, 1, 1, 2, 2, 3, 5, 10};
    std::vector<float> output(10);
    ImageEnhancement::histogramEqualize(input.data(), output.data(), 10, 256);
    REQUIRE(output[0] < output[9]);
    REQUIRE(output[0] == output[1]);
    REQUIRE(output[1] == output[4]);
}

TEST_CASE("Contrast stretch preserves nodata", "[enhancement]") {
    float nodata = -9999.0f;
    std::vector<float> input = {10, 20, -9999, 30, 40};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 10.0f, 40.0f, nodata);
    REQUIRE(output[2] == Approx(nodata));
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Lee filter excludes +-Inf pixels from local statistics", "[enhancement]") {
    // A single +Inf must not poison the summed-area table: before the
    // isfinite() guard every window whose rectangle contained the cell
    // produced Inf/NaN local statistics (#634).
    constexpr int W = 10, H = 10;
    std::vector<float> input(W * H, 1.0f);
    input[5 * W + 5] = std::numeric_limits<float>::infinity();
    std::vector<float> output(W * H, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), W, H, 3, 0.5f);
    for (float v : output)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("Speckle filters reject rasters beyond the integral-image limit", "[enhancement][integral-image]") {
    // #691: width*height > INT32_MAX used to overflow the all-int32 index
    // math and the per-pixel valid-count vector. The guard must fail loudly
    // (log + untouched output) BEFORE touching the buffers or allocating, so
    // 1-element buffers are enough to exercise it.
    constexpr int W = 50000;
    constexpr int H = 50000; // 2.5e9 pixels > INT32_MAX
    float input = 1.0f;

    float output = -777.0f;
    ImageEnhancement::leeFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::enhancedLeeFilter(&input, &output, W, H, 3, 0.5f, 1.0f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::frostFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::kuanFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::gammaMapFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);
}

TEST_CASE("ChunkedProcessor respects the maxThreads cap", "[enhancement][chunked]") {
    // #692: process() fans out on a dedicated (non-global) pool bounded by the
    // nested-parallelism token. maxThreads=1 must execute all chunks on a
    // single thread instead of one thread per core.
    constexpr int W = 8;
    constexpr int H = 600; // 3 chunks at the default 256-row chunk height
    ChunkedProcessor processor(W, H, 0);
    REQUIRE(processor.chunkCount() > 1);

    std::mutex threadsMutex;
    std::set<const QThread *> threads;
    std::atomic<int> executed{0};
    auto recordThread = [&](const ChunkedProcessor::Chunk &chunk) {
        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            threads.insert(QThread::currentThread());
        }
        ++executed;
        return chunk.endRow > chunk.startRow;
    };

    REQUIRE(processor.process(recordThread, nullptr, 1));
    REQUIRE(executed.load() == processor.chunkCount());
    REQUIRE(threads.size() == 1);

    // Default token: all chunks still complete; observed concurrency never
    // exceeds the documented auto budget (cores / 4, at least 1).
    executed.store(0);
    threads.clear();
    REQUIRE(processor.process(recordThread));
    REQUIRE(executed.load() == processor.chunkCount());
    REQUIRE(threads.size() >= 1);
    REQUIRE(threads.size() <= static_cast<size_t>(ChunkedProcessor::defaultMaxThreads()));
    REQUIRE(ChunkedProcessor::defaultMaxThreads() >= 1);
}

// ---------------------------------------------------------------------------
// Streaming stretch/filter paths (#691): the contrast stretch dialog and the
// image enhancement panel now run streaming statistics + streaming apply
// passes (ImageEnhancementStreaming) instead of materializing full frames.
// The full-frame ImageEnhancement kernels are the oracle.
// ---------------------------------------------------------------------------

#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>

#include <array>
#include <optional>

namespace IES = ImageEnhancementStreaming;

namespace
{

constexpr float kSentinel = -9999.0f;

// Deterministic raster with declared -9999 NoData cells and NaN holes.
std::vector<float> makeStretchBand( int w, int h )
{
    std::vector<float> band( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * w + x;
            float v = 10.0f + 3.0f * static_cast<float>( ( x * 5 + y * 3 ) % 40 );
            if ( ( x * 11 + y * 7 ) % 29 == 0 )
                v = kSentinel; // declared NoData sentinel
            else if ( ( x + 2 * y ) % 37 == 0 )
                v = std::numeric_limits<float>::quiet_NaN();
            band[i] = v;
        }
    }
    return band;
}

std::vector<float> readBand( const QString &path, int bandNum, int w, int h )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> out( static_cast<size_t>( w ) * h );
    REQUIRE( ds.readBandData( bandNum, out.data(), w, h ) );
    return out;
}

void requireCloseOrBothNan( float a, float b )
{
    if ( std::isnan( a ) && std::isnan( b ) )
        return;
    REQUIRE( a == Approx( b ).margin( 1e-3 ) );
}

// Stream one band through halo tiles with @a kernel, writing to @a dstPath.
bool runFilterTileCase( const QString &srcPath, const QString &dstPath, int w, int h,
                        int tileDim, int halo, const IES::WindowedTileFn &kernel )
{
    GdalDatasetWrapper src;
    if ( !src.open( srcPath ) )
        return false;
    GdalStreamingOutput dst( dstPath, w, h, 1, GDT_Float32, src.geoTransform(), src.projection() );
    if ( !dst.isOpen() )
        return false;
    if ( !IES::streamBandWindowed( src, 1, dst, tileDim, halo, kernel ) )
        return false;
    return dst.closeWithError( nullptr );
}

} // namespace

TEST_CASE( "Streaming stretch matches full-frame kernels (multi-tile, nodata)", "[enhancement][streaming]" )
{
    ensureGdalInit();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const int w = 30, h = 23;
    const std::vector<float> band = makeStretchBand( w, h );
    const size_t n = band.size();

    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    const QString dstPath = tmp.path() + QStringLiteral( "/dst.tif" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    std::vector<std::vector<float>> bandsData = { band };
    REQUIRE( writeGdalOutput( srcPath, w, h, bandsData, gt, QString(), &err,
                              std::optional<double>( kSentinel ) ) );

    // Per-band declared NoData (as both dialogs resolve it, #445).
    const float ndF = kSentinel;

    // Oracle: exactly what the dialogs computed before the conversion.
    std::vector<float> oracle( n, 0.0f );
    IES::StretchParams params;

    SECTION( "linear min-max" )
    {
        float minVal = std::numeric_limits<float>::max();
        float maxVal = std::numeric_limits<float>::lowest();
        for ( float v : band )
        {
            if ( !std::isfinite( v ) || v == ndF ) continue;
            minVal = std::min( minVal, v );
            maxVal = std::max( maxVal, v );
        }
        if ( minVal > maxVal ) { minVal = 0.0f; maxVal = 0.0f; }
        ImageEnhancement::linearStretch( band.data(), oracle.data(), n, minVal, maxVal, ndF );
        params.kind = IES::StretchKind::Linear;
    }
    SECTION( "percent clip (endpoints match)" )
    {
        ImageEnhancement::percentClipStretch( band.data(), oracle.data(), n, 2.0f, ndF );
        params.kind = IES::StretchKind::PercentClip;
        params.clipPercent = 2.0f;
    }
    SECTION( "stddev k=2" )
    {
        ImageEnhancement::stddevStretch( band.data(), oracle.data(), n, 2.0f, ndF );
        params.kind = IES::StretchKind::StdDev;
        params.stddevK = 2.0f;
    }
    SECTION( "histogram equalize" )
    {
        ImageEnhancement::histogramEqualize( band.data(), oracle.data(), n, 256, ndF );
        params.kind = IES::StretchKind::HistogramEqualize;
    }
    SECTION( "piecewise" )
    {
        const std::vector<std::pair<float, float>> pts = { { 0.0f, 0.0f }, { 60.0f, 90.0f }, { 140.0f, 255.0f } };
        ImageEnhancement::piecewiseLinearStretch( band.data(), oracle.data(), n, pts, ndF );
        params.kind = IES::StretchKind::Piecewise;
        params.piecewisePoints = pts;
    }

    // Streaming apply (tileDim 13 forces several tiles incl. edge-clamped ones).
    GdalDatasetWrapper src;
    REQUIRE( src.open( srcPath ) );
    GdalStreamingOutput dst( dstPath, w, h, 1, GDT_Float32, src.geoTransform(), src.projection() );
    REQUIRE( dst.isOpen() );
    REQUIRE( IES::streamBandStretch( src, 1, ndF, params, dst, 13, &err ) );
    REQUIRE( dst.closeWithError( nullptr ) );

    const std::vector<float> got = readBand( dstPath, 1, w, h );
    for ( size_t i = 0; i < n; ++i )
        requireCloseOrBothNan( got[i], oracle[i] );

    // Sentinels must survive the streaming round trip (dialog output convention).
    for ( size_t i = 0; i < n; ++i )
        if ( band[i] == kSentinel )
            REQUIRE( got[i] == kSentinel );
}

TEST_CASE( "PercentClipSelector reproduces sorted-array endpoints", "[enhancement][streaming]" )
{
    // 30 deterministic values with duplicates.
    const std::vector<float> vals = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9,
                                      3, 2, 3, 8, 4, 6, 2, 6, 4, 7, 2, 5, 1, 7, 9 };
    std::vector<float> sorted = vals;
    std::sort( sorted.begin(), sorted.end() );

    const float pct = 20.0f;
    const size_t validCount = vals.size();
    // percentClipStretch rank arithmetic (float truncation included).
    const size_t clipCount = static_cast<size_t>( static_cast<float>( validCount ) * pct / 100.0f );
    const size_t lo = clipCount;
    const size_t hi = validCount - 1 - clipCount;
    REQUIRE( hi > lo );

    float minVal = *std::min_element( vals.begin(), vals.end() );
    float maxVal = *std::max_element( vals.begin(), vals.end() );
    IES::PercentClipSelector selector;
    selector.beginHistogram( minVal, maxVal );
    for ( float v : vals )
        selector.addValue( v );
    selector.finalize( validCount, pct );
    REQUIRE( selector.needsRefinement() );
    for ( float v : vals )
        selector.collectValue( v );
    const IES::PercentileEndpoints ep = selector.endpoints();
    REQUIRE( ep.lo == Approx( sorted[lo] ) );
    REQUIRE( ep.hi == Approx( sorted[hi] ) );
}

TEST_CASE( "Streaming stretch keeps an all-nodata band at the sentinel", "[enhancement][streaming][nodata]" )
{
    ensureGdalInit();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const int w = 12, h = 9;
    std::vector<float> band( static_cast<size_t>( w ) * h, kSentinel );
    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    const QString dstPath = tmp.path() + QStringLiteral( "/dst.tif" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    std::vector<std::vector<float>> bandsData = { band };
    REQUIRE( writeGdalOutput( srcPath, w, h, bandsData, gt, QString(), &err,
                              std::optional<double>( kSentinel ) ) );

    GdalDatasetWrapper src;
    REQUIRE( src.open( srcPath ) );
    GdalStreamingOutput dst( dstPath, w, h, 1, GDT_Float32, src.geoTransform(), src.projection() );
    REQUIRE( dst.isOpen() );
    for ( const IES::StretchKind kind : { IES::StretchKind::Linear, IES::StretchKind::PercentClip,
                                          IES::StretchKind::StdDev, IES::StretchKind::HistogramEqualize } )
    {
        IES::StretchParams params;
        params.kind = kind;
        REQUIRE( IES::streamBandStretch( src, 1, kSentinel, params, dst, 8, &err ) );
    }
    REQUIRE( dst.closeWithError( nullptr ) );

    const std::vector<float> got = readBand( dstPath, 1, w, h );
    for ( float v : got )
        REQUIRE( v == kSentinel );
}

TEST_CASE( "Streaming spatial filter tiles match full-frame filters", "[enhancement][streaming]" )
{
    ensureGdalInit();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const int w = 27, h = 19;
    // Pattern with NaN holes, no declared sentinel (panel filter convention).
    std::vector<float> band( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * w + x;
            float v = 20.0f + 10.0f * static_cast<float>( ( x * 3 + y * 5 ) % 11 );
            if ( ( x * 13 + y * 19 ) % 31 == 0 )
                v = std::numeric_limits<float>::quiet_NaN();
            band[i] = v;
        }

    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    const QString dstPath = tmp.path() + QStringLiteral( "/dst.tif" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    std::vector<std::vector<float>> bandsData = { band };
    REQUIRE( writeGdalOutput( srcPath, w, h, bandsData, gt, QString(), &err ) );

    const size_t n = band.size();
    std::vector<float> oracle( n, 0.0f );
    const int kernelSize = 5;

    SECTION( "mean 5x5" )
    {
        ImageEnhancement::meanFilter( band.data(), oracle.data(), w, h, kernelSize );
        REQUIRE( runFilterTileCase( srcPath, dstPath, w, h, 16, kernelSize / 2,
                                         [kernelSize]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                             IES::convolveTileMean( t, buf, core, kernelSize );
                                         } ) );
    }
    SECTION( "gaussian 5x5 sigma 1" )
    {
        ImageEnhancement::gaussianFilter( band.data(), oracle.data(), w, h, kernelSize, 1.0f );
        REQUIRE( runFilterTileCase( srcPath, dstPath, w, h, 16, kernelSize / 2,
                                         [kernelSize]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                             IES::convolveTileGaussian( t, buf, core, kernelSize, 1.0f );
                                         } ) );
    }
    SECTION( "median 5x5" )
    {
        ImageEnhancement::medianFilter( band.data(), oracle.data(), w, h, kernelSize );
        REQUIRE( runFilterTileCase( srcPath, dstPath, w, h, 16, kernelSize / 2,
                                         [kernelSize]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                             IES::convolveTileMedian( t, buf, core, kernelSize );
                                         } ) );
    }
    SECTION( "sobel" )
    {
        ImageEnhancement::sobelFilter( band.data(), oracle.data(), w, h );
        REQUIRE( runFilterTileCase( srcPath, dstPath, w, h, 16, 1,
                                         []( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                             IES::convolveTileSobel( t, buf, core );
                                         } ) );
    }
    SECTION( "laplacian" )
    {
        ImageEnhancement::laplacianFilter( band.data(), oracle.data(), w, h );
        REQUIRE( runFilterTileCase( srcPath, dstPath, w, h, 16, 1,
                                         []( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                             IES::convolveTileLaplacian( t, buf, core );
                                         } ) );
    }

    const std::vector<float> got = readBand( dstPath, 1, w, h );
    for ( size_t i = 0; i < n; ++i )
        requireCloseOrBothNan( got[i], oracle[i] );
}

TEST_CASE( "Streaming band ratio and IHS tiles match point kernels", "[enhancement][streaming]" )
{
    // bandRatioTile === ImageEnhancement::bandRatio
    std::vector<float> a = { 10, 0, -5, 8, 25 };
    std::vector<float> b = { 2, 4, 5, 0, -25 };
    std::vector<float> got( a.size(), 0.0f );
    std::vector<float> oracle( a.size(), 0.0f );
    IES::bandRatioTile( a.data(), b.data(), got.data(), a.size() );
    ImageEnhancement::bandRatio( a.data(), b.data(), oracle.data(), a.size() );
    for ( size_t i = 0; i < a.size(); ++i )
        REQUIRE( got[i] == Approx( oracle[i] ).margin( 1e-6 ) );

    // ihsTransformTile === the panel's per-pixel masking + rgbToIhs
    const float ndR = -9999.0f, ndG = -9999.0f, ndB = -9999.0f;
    std::vector<float> bip = {
        200, 100, 50,   // valid
        100, 100, 100,  // valid
        std::numeric_limits<float>::quiet_NaN(), 100, 100, // r NaN -> NaN
        -9999.0f, 100, 100, // r sentinel -> NaN
    };
    const size_t n = 4;
    std::vector<float> gotI( n ), gotH( n ), gotS( n );
    IES::ihsTransformTile( bip.data(), ndR, ndG, ndB, gotI.data(), gotH.data(), gotS.data(), n );
    for ( size_t i = 0; i < n; ++i )
    {
        const float rv = bip[i * 3], gv = bip[i * 3 + 1], bv = bip[i * 3 + 2];
        float ei, eh, es;
        if ( !std::isfinite( rv ) || !std::isfinite( gv ) || !std::isfinite( bv ) ||
             rv == ndR || gv == ndG || bv == ndB )
        {
            ei = eh = es = std::numeric_limits<float>::quiet_NaN();
        }
        else
        {
            ImageEnhancement::rgbToIhs( rv, gv, bv, ei, eh, es );
        }
        if ( std::isnan( ei ) )
        {
            REQUIRE( std::isnan( gotI[i] ) );
            REQUIRE( std::isnan( gotH[i] ) );
            REQUIRE( std::isnan( gotS[i] ) );
        }
        else
        {
            REQUIRE( gotI[i] == Approx( ei ).margin( 1e-5 ) );
            REQUIRE( gotH[i] == Approx( eh ).margin( 1e-5 ) );
            REQUIRE( gotS[i] == Approx( es ).margin( 1e-5 ) );
        }
    }
}
