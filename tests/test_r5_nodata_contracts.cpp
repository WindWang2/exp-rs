/***************************************************************************
 * test_r5_nodata_contracts.cpp — R5 NoData/lifecycle closure oracles
 *
 * Track 02 (R5 RS/GDAL fused execution + operator correctness). Every
 * expectation is analytically derivable or a bit-for-bit equivalence; the
 * derivation sits next to each assertion. Regressions pin the R5 NoData
 * audit fixes:
 *
 *   image_enhancement (filter/speckle) — a declared sentinel behaves exactly
 *                           like NaN inside every window (bit-for-bit vs the
 *                           same raster with NaN at the sentinel positions);
 *                           previously the sentinel entered mean/gaussian
 *                           sums and Lee statistics as real data
 *   rs:resample / rs:align — the warp void contract: declared nodata is
 *                           pinned on the output; undeclared float inputs
 *                           get a declared NaN (voids are holes, not zeros)
 *   rs:register_images    — the output declares (and fills voids with) the
 *                           SOURCE's declared sentinel, not a hardcoded one
 *   rs:sar_coregister     — the resampled slave declares NaN voids (the
 *                           local variant already did; the global one
 *                           silently shipped undeclared NaNs)
 *   rs:sar_interferogram  — the interferogram band declares NaN voids, at
 *                           parity with the coherence band
 *   rs:spectral_similarity— heterogeneous per-band sentinels are ALL
 *                           honored (previously "first declared wins" let a
 *                           second band's void score as data)
 *   rs:obia_segment       — per-band sentinels void pixels in the simple
 *                           engine (previously band 1's declaration was
 *                           applied to every band)
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "support/r4_operator_fixtures.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>
#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "processing/algorithms/image_enhancement_streaming.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

using namespace r4fixtures;

namespace
{

/// Reads a band's declared nodata (has == false when undeclared).
void readNodata( const QString &path, int band1Based, bool *has, double *value )
{
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, band1Based );
    REQUIRE( band != nullptr );
    int hasNd = 0;
    const double nd = GDALGetRasterNoDataValue( band, &hasNd );
    if ( has )
        *has = hasNd != 0;
    if ( value )
        *value = nd;
    GDALClose( ds );
}

Json::Value runR5Operator( const std::string &id, const Json::Value &params,
                           const std::string &workDir )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx( workDir );
    return op->run( params, ctx );
}

/// Plain complex SLC raster (no CRS — InSAR products carry the master grid).
QString writeComplexRaster( const QString &path, int w, int h,
                            const std::function<float( int, int )> &re,
                            const std::function<float( int, int )> &im )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_CFloat32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    std::vector<std::complex<float>> pixels( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
            pixels[static_cast<size_t>( y ) * w + x] = { re( x, y ), im( x, y ) };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, w, h, pixels.data(), w, h,
                           GDT_CFloat32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// image_enhancement filter/speckle — declared sentinel ≡ NaN (bit-for-bit)
// ---------------------------------------------------------------------------

namespace
{

/// Streams one band through @p kernel and returns the written core plane.
std::vector<float> filteredBand( const GdalDatasetWrapper &src, const QString &dstPath,
                                 int tileDim, int halo,
                                 const ImageEnhancementStreaming::WindowedTileFn &kernel,
                                 float sentinel, int w, int h )
{
    GdalStreamingOutput dst( dstPath, w, h, 1, GDT_Float32, src.geoTransform(),
                             src.projection() );
    REQUIRE( dst.isOpen() );
    REQUIRE( ImageEnhancementStreaming::streamBandWindowed( src, 1, dst, tileDim, halo, kernel,
                                                            sentinel ) );
    dst.close();
    GdalDatasetWrapper dstHolder;
    REQUIRE( dstHolder.open( dstPath ) );
    std::vector<float> out( static_cast<size_t>( w ) * h );
    REQUIRE( dstHolder.readBandData( 1, out.data(), w, h ) );
    return out;
}

} // namespace

TEST_CASE( "enhancement filter treats the declared sentinel exactly like NaN",
           "[r5][nodata][enhancement]" )
{
    // 6x6 plane, values 1..36; the top-left 2x2 block is declared sentinel
    // -9999. Oracle: filtering the sentinel raster must produce EXACTLY the
    // same plane as filtering the same raster with NaN at those positions —
    // the pre-wash contract. (Pre-R5, -9999 entered the mean/gaussian sums
    // as data, so the planes diverged and void centers came back finite.)
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kDim = 6;
    constexpr float kSentinel = -9999.0f;
    std::vector<float> withSentinel( kDim * kDim );
    std::vector<float> withNaN( kDim * kDim );
    for ( int y = 0; y < kDim; ++y )
    {
        for ( int x = 0; x < kDim; ++x )
        {
            const float v = static_cast<float>( y * kDim + x + 1 );
            const bool isVoid = x < 2 && y < 2;
            withSentinel[static_cast<size_t>( y ) * kDim + x] = isVoid ? kSentinel : v;
            withNaN[static_cast<size_t>( y ) * kDim + x] = isVoid
                                                               ? std::numeric_limits<float>::quiet_NaN()
                                                               : v;
        }
    }
    const QString sentinelPath = writeFloatRaster( dir.filePath( "sentinel.tif" ), kDim, kDim,
                                                   { withSentinel }, true, kSentinel );
    const QString nanPath = writeFloatRaster( dir.filePath( "nan.tif" ), kDim, kDim, { withNaN },
                                              false, 0.0 );

    GdalDatasetWrapper srcSentinel, srcNaN;
    REQUIRE( srcSentinel.open( sentinelPath ) );
    REQUIRE( srcNaN.open( nanPath ) );

    const auto checkEquivalence = [&]( const char *name, int halo,
                                       const ImageEnhancementStreaming::WindowedTileFn &kernel ) {
        INFO( "kernel: " << name );
        const auto got = filteredBand( srcSentinel,
                                       dir.filePath( QStringLiteral( "filt_%1_s.tif" ).arg( name ) ),
                                       4, halo, kernel, kSentinel, kDim, kDim );
        const auto want = filteredBand( srcNaN,
                                        dir.filePath( QStringLiteral( "filt_%1_n.tif" ).arg( name ) ),
                                        4, halo, kernel, kSentinel, kDim, kDim );
        for ( size_t i = 0; i < got.size(); ++i )
        {
            if ( std::isnan( want[i] ) )
                REQUIRE( std::isnan( got[i] ) );
            else
                REQUIRE( got[i] == want[i] ); // bit-for-bit
        }
        // And the void centers must actually be void (not filtered values).
        REQUIRE( std::isnan( got[0] ) );
        REQUIRE( std::isnan( got[static_cast<size_t>( kDim ) + 1] ) );
    };

    checkEquivalence( "mean3", 1, []( const GdalBlockStream::Tile &t, const float *buf,
                                      float *core ) {
        ImageEnhancementStreaming::convolveTileMean( t, buf, core, 3 );
    } );
    checkEquivalence( "gaussian3", 1, []( const GdalBlockStream::Tile &t, const float *buf,
                                          float *core ) {
        ImageEnhancementStreaming::convolveTileGaussian( t, buf, core, 3, 0.5f );
    } );
    checkEquivalence( "median3", 1, []( const GdalBlockStream::Tile &t, const float *buf,
                                        float *core ) {
        ImageEnhancementStreaming::convolveTileMedian( t, buf, core, 3 );
    } );
    checkEquivalence( "lee3", 1, []( const GdalBlockStream::Tile &t, const float *buf,
                                     float *core ) {
        ImageEnhancementStreaming::speckleTileLee( t, buf, core, 3, 0.25f );
    } );
    checkEquivalence( "frost3", 1, []( const GdalBlockStream::Tile &t, const float *buf,
                                       float *core ) {
        ImageEnhancementStreaming::speckleTileFrost( t, buf, core, 3, 1.0f );
    } );
}

// ---------------------------------------------------------------------------
// rs:resample / rs:align — the warp void contract
// ---------------------------------------------------------------------------

TEST_CASE( "resample pins declared nodata and declares NaN voids for undeclared "
           "float rasters",
           "[r5][nodata][warp]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    Json::Value p;
    p["resolution"] = 2.0; // coarse-ify the 1-unit grid → interpolated voids? no voids here,
                           // but the DECLARATION must survive resampling either way

    // Declared -9999 input: the output must declare -9999.
    {
        std::vector<float> band( 64 );
        for ( size_t i = 0; i < band.size(); ++i )
            band[i] = static_cast<float>( i );
        const QString in = writeFloatRaster( dir.filePath( "declared.tif" ), 8, 8, { band },
                                             true, -9999.0 );
        p["input"] = in.toStdString();
        p["output"] = dir.filePath( "declared_out.tif" ).toStdString();
        runR5Operator( "rs:resample", p, dir.path().toStdString() );
        bool has = false;
        double nd = 0.0;
        readNodata( dir.filePath( "declared_out.tif" ), 1, &has, &nd );
        REQUIRE( has );
        REQUIRE( nd == -9999.0 );
    }

    // Undeclared float input: the output declares NaN (warp voids are holes,
    // not silently-zero "data").
    {
        std::vector<float> band( 64 );
        for ( size_t i = 0; i < band.size(); ++i )
            band[i] = static_cast<float>( i );
        const QString in = writeFloatRaster( dir.filePath( "plain.tif" ), 8, 8, { band }, false,
                                             0.0 );
        p["input"] = in.toStdString();
        p["output"] = dir.filePath( "plain_out.tif" ).toStdString();
        runR5Operator( "rs:resample", p, dir.path().toStdString() );
        bool has = false;
        double nd = 0.0;
        readNodata( dir.filePath( "plain_out.tif" ), 1, &has, &nd );
        REQUIRE( has );
        REQUIRE( std::isnan( nd ) );
    }
}

TEST_CASE( "align pads reference-grid voids with the declared nodata, not zeros",
           "[r5][nodata][warp]" )
{
    // 4x4 input aligned onto an 8x8 reference grid in the same CRS: the
    // outside-footprint region must carry the DECLARED sentinel (-9999), both
    // as pixel values and as the band's NoData declaration. Pre-R5 the pad
    // was undeclared zeros — "real data zero" for every downstream statistic.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> band( 16, 5.0f );
    const QString in = writeFloatRaster( dir.filePath( "in.tif" ), 4, 4, { band }, true, -9999.0 );
    // Reference grid: 8x8 at 0.5-unit resolution sharing the input's origin
    // area (same EPSG:4326 tag as writeFloatRaster stamps).
    const QString ref = writeFloatRaster( dir.filePath( "ref.tif" ), 8, 8,
                                          { std::vector<float>( 64, 1.0f ) }, false, 0.0 );

    Json::Value p;
    p["input"] = in.toStdString();
    p["reference"] = ref.toStdString();
    p["output"] = dir.filePath( "aligned.tif" ).toStdString();
    p["resampling"] = "near";
    runR5Operator( "rs:align", p, dir.path().toStdString() );

    bool has = false;
    double nd = 0.0;
    readNodata( dir.filePath( "aligned.tif" ), 1, &has, &nd );
    REQUIRE( has );
    REQUIRE( nd == -9999.0 );
    const auto aligned = readBand( dir.filePath( "aligned.tif" ), 1 );
    REQUIRE( aligned.size() == 64 );
    size_t voids = 0;
    for ( const float v : aligned )
        if ( v == -9999.0f )
            ++voids;
    // The input covers 4x4 of the 8x8 grid; at least the far half must be
    // declared void with the declared value.
    REQUIRE( voids >= 48 );
    size_t zeros = 0;
    for ( const float v : aligned )
        if ( v == 0.0f )
            ++zeros;
    REQUIRE( zeros == 0 );
}

// ---------------------------------------------------------------------------
// rs:register_images — source sentinel honored and declared
// ---------------------------------------------------------------------------

TEST_CASE( "register_images output declares the source sentinel, not a hardcoded one",
           "[r5][nodata][register]" )
{
    // The source DECLARES -3.4e38 (float sentinel large-negative) but carries
    // no such pixels (matching keeps it simple). The warped output's voids
    // must be filled with and declare the SOURCE's sentinel; pre-R5 both were
    // hardcoded -9999. Texture is the proven sine+grain registration fixture
    // (test_registration_operators.cpp) shifted by (4, 6).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kDim = 256;
    constexpr double kSentinel = -3.4e38;
    auto grain = []( int x, int y ) {
        const std::uint32_t h = static_cast<std::uint32_t>( x ) * 374761393u
                                + static_cast<std::uint32_t>( y ) * 668265263u;
        const std::uint32_t h2 = ( h ^ ( h >> 13 ) ) * 1274126177u;
        return static_cast<double>( ( h2 ^ ( h2 >> 16 ) ) & 0xFFFF ) / 32767.5 - 1.0;
    };
    auto texture = [&grain]( int x, int y ) {
        return 50.0f + static_cast<float>( 20.0 * std::sin( x / 9.3 ) * std::sin( y / 7.7 )
                                           + 10.0 * std::sin( ( x + y ) / 5.1 )
                                           + 3.0 * grain( x, y ) );
    };
    std::vector<float> srcPlane( static_cast<size_t>( kDim ) * kDim );
    for ( int y = 0; y < kDim; ++y )
        for ( int x = 0; x < kDim; ++x )
            srcPlane[static_cast<size_t>( y ) * kDim + x] = texture( x, y );
    const QString src = writeFloatRaster( dir.filePath( "src.tif" ), kDim, kDim, { srcPlane },
                                          true, kSentinel );
    std::vector<float> refPlane( static_cast<size_t>( kDim ) * kDim );
    for ( int y = 0; y < kDim; ++y )
        for ( int x = 0; x < kDim; ++x )
            refPlane[static_cast<size_t>( y ) * kDim + x] = texture( x - 4, y - 6 );
    const QString ref = writeFloatRaster( dir.filePath( "ref.tif" ), kDim, kDim, { refPlane },
                                          false, 0.0 );

    Json::Value p;
    p["source"] = src.toStdString();
    p["reference"] = ref.toStdString();
    p["output"] = dir.filePath( "registered.tif" ).toStdString();
    p["metric"] = "phase_correlation";
    p["maxDim"] = kDim;
    const Json::Value result = runR5Operator( "rs:register_images", p, dir.path().toStdString() );
    REQUIRE( result["status"].asString() == "success" );

    bool has = false;
    double nd = 0.0;
    readNodata( dir.filePath( "registered.tif" ), 1, &has, &nd );
    REQUIRE( has );
    // Compare in float space: the sentinel rides the output's Float32 band.
    REQUIRE( nd == static_cast<double>( static_cast<float>( kSentinel ) ) );
    // The warp fill follows the declared sentinel: out-of-source voids carry
    // it verbatim instead of a hardcoded -9999.
    const auto out = readBand( dir.filePath( "registered.tif" ), 1 );
    REQUIRE( std::count( out.begin(), out.end(), static_cast<float>( kSentinel ) ) > 0 );
    REQUIRE( std::count( out.begin(), out.end(), -9999.0f ) == 0 );
}

// ---------------------------------------------------------------------------
// rs:sar_coregister / rs:sar_interferogram — NaN void declarations
// ---------------------------------------------------------------------------

TEST_CASE( "sar_coregister output declares its NaN resampling voids",
           "[r5][nodata][sar]" )
{
    // Synthetic global shift (2, −1) over LCG-amplitude/phase-ramp SLC — the
    // proven coregistration fixture (test_sar_platform10.cpp). The operator
    // runs the resample pass and writes the coregistered slave; the shift
    // voids are NaN samples and the band must DECLARE them (pre-R5 the
    // global variant shipped undeclared NaNs; the local variant declared).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 32;
    const int h = 32;
    const double slope = 0.2;
    auto field = [&]( int x, int y ) -> std::complex<float> {
        const unsigned lcg = ( 1103515245u * static_cast<unsigned>( y * w + x + 1 ) + 12345u );
        const double amp = 0.75 + 0.5 * ( ( lcg >> 16 ) % 1024 ) / 1024.0;
        const double phase = slope * x + 0.1 * y;
        return { static_cast<float>( amp * std::cos( phase ) ),
                 static_cast<float>( amp * std::sin( phase ) ) };
    };
    const auto re = [&]( int x, int y ) { return field( x, y ).real(); };
    const auto im = [&]( int x, int y ) { return field( x, y ).imag(); };
    const QString master = writeComplexRaster( dir.filePath( "master.tif" ), w, h, re, im );
    // Slave(x, y) = master(x − 2, y + 1), zero-filled out of range.
    const auto slaveRe = [&]( int x, int y ) {
        const int sx = x - 2, sy = y + 1;
        return ( sx < 0 || sx >= w || sy < 0 || sy >= h ) ? 0.0f : field( sx, sy ).real();
    };
    const auto slaveIm = [&]( int x, int y ) {
        const int sx = x - 2, sy = y + 1;
        return ( sx < 0 || sx >= w || sy < 0 || sy >= h ) ? 0.0f : field( sx, sy ).imag();
    };
    const QString slave = writeComplexRaster( dir.filePath( "slave.tif" ), w, h, slaveRe, slaveIm );

    Json::Value p;
    p["master"] = master.toStdString();
    p["slave"] = slave.toStdString();
    p["output"] = dir.filePath( "coreg.tif" ).toStdString();
    p["searchRadius"] = 6;
    p["reportOnly"] = 0;
    const Json::Value result = runR5Operator( "rs:sar_coregister", p, dir.path().toStdString() );
    REQUIRE( result["confidentPatches"].asInt64() >= 3 );

    bool has = false;
    double nd = 0.0;
    readNodata( dir.filePath( "coreg.tif" ), 1, &has, &nd );
    REQUIRE( has );
    REQUIRE( std::isnan( nd ) );
}

TEST_CASE( "sar_interferogram declares NaN on the interferogram band (coherence parity)",
           "[r5][nodata][sar]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto re = []( int x, int y ) {
        return 10.0f * std::cos( 0.1f * x );
    };
    const auto im = []( int x, int y ) {
        return 10.0f * std::sin( 0.1f * x );
    };
    const QString master =
        writeComplexRaster( dir.filePath( "master.tif" ), 32, 32, re, im );
    const QString slave = writeComplexRaster( dir.filePath( "slave.tif" ), 32, 32, re, im );

    Json::Value p;
    p["master"] = master.toStdString();
    p["slave"] = slave.toStdString();
    p["output"] = dir.filePath( "ifg.tif" ).toStdString();
    p["coherenceOutput"] = dir.filePath( "coh.tif" ).toStdString();
    runR5Operator( "rs:sar_interferogram", p, dir.path().toStdString() );

    bool ifgHas = false, cohHas = false;
    double ifgNd = 0.0, cohNd = 0.0;
    readNodata( dir.filePath( "ifg.tif" ), 1, &ifgHas, &ifgNd );
    readNodata( dir.filePath( "coh.tif" ), 1, &cohHas, &cohNd );
    REQUIRE( ifgHas );
    REQUIRE( std::isnan( ifgNd ) );
    REQUIRE( cohHas ); // the coherence declaration was already there (parity anchor)
    REQUIRE( std::isnan( cohNd ) );
}

// ---------------------------------------------------------------------------
// rs:spectral_similarity — heterogeneous per-band sentinels
// ---------------------------------------------------------------------------

TEST_CASE( "similarity honors heterogeneous per-band sentinels",
           "[r5][nodata][similarity]" )
{
    // Two bands with DIFFERENT positive declared sentinels (positive so the
    // kernel's negativity guard cannot mask the case): band1 void = 1.0,
    // band2 void = 2.0. Pixels: A = [1, 10] (band1 void), B = [10, 2]
    // (band2 void), C = [10, 10] (clean). References: [10, 10].
    // Pre-R5 only the FIRST declared sentinel (1.0) was detected, so B was
    // classified as if its band2 value 2.0 were reflectance data.
    // Per-band declarations ride on a VRT wrapper: GTiff stores ONE nodata
    // tag per dataset, so raw per-band declarations are unwritable there.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::vector<float> b1 = { 1.0f, 10.0f, 10.0f };
    const std::vector<float> b2 = { 10.0f, 2.0f, 10.0f };
    const QString raw = writeFloatRaster( dir.filePath( "img_raw.tif" ), 3, 1, { b1, b2 },
                                          false, 0.0 );
    QFile vrtFile( dir.filePath( "img.vrt" ) );
    REQUIRE( vrtFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream out( &vrtFile );
    out << "<VRTDataset rasterXSize=\"3\" rasterYSize=\"1\">\n";
    out << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
    out << "    <NoDataValue>1</NoDataValue>\n";
    out << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">img_raw.tif</SourceFilename>"
           "<SourceBand>1</SourceBand></SimpleSource>\n";
    out << "  </VRTRasterBand>\n";
    out << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
    out << "    <NoDataValue>2</NoDataValue>\n";
    out << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">img_raw.tif</SourceFilename>"
           "<SourceBand>2</SourceBand></SimpleSource>\n";
    out << "  </VRTRasterBand>\n";
    out << "</VRTDataset>\n";
    vrtFile.close();

    // The fixture's declaration sanity check: GDAL must report the two
    // DIFFERENT per-band sentinels through the wrapper.
    {
        bool h1 = false, h2 = false;
        double n1 = 0.0, n2 = 0.0;
        readNodata( dir.filePath( "img.vrt" ), 1, &h1, &n1 );
        readNodata( dir.filePath( "img.vrt" ), 2, &h2, &n2 );
        REQUIRE( h1 );
        REQUIRE( h2 );
        REQUIRE( n1 == 1.0 );
        REQUIRE( n2 == 2.0 );
    }

    Json::Value refs( Json::arrayValue );
    Json::Value r0( Json::arrayValue );
    r0.append( 10 );
    r0.append( 10 );
    refs.append( r0 );
    Json::Value p;
    p["input"] = dir.filePath( "img.vrt" ).toStdString();
    p["output"] = dir.filePath( "labels.tif" ).toStdString();
    p["refs"] = refs;
    const Json::Value result = runR5Operator( "rs:spectral_similarity", p,
                                              dir.path().toStdString() );
    REQUIRE( result["labelledPixels"].asUInt() == 1 );

    const auto labels = readBand( dir.filePath( "labels.tif" ), 1 );
    REQUIRE( labels[0] == -9999.0f ); // band1 void → unlabelled
    REQUIRE( labels[1] == -9999.0f ); // band2 void → unlabelled (was mislabelled pre-R5)
    REQUIRE( labels[2] == 0.0f );     // clean pixel → reference 0
}

// ---------------------------------------------------------------------------
// rs:obia_segment — per-band sentinels in the simple engine
// ---------------------------------------------------------------------------

TEST_CASE( "obia_segment (simple) voids pixels by each band's own sentinel",
           "[r5][nodata][obia]" )
{
    // Band 1 has NO declaration; band 2 declares -9999. Pixel 0 carries -9999
    // in band 2 → must be label 0 (nodata). Pre-R5 only band 1's (absent)
    // declaration was consulted, so the void entered segmentation as data.
    // A VRT wrapper provides the per-band declaration (GTiff's single nodata
    // tag would leak band 2's value onto band 1 on reopen and spoil the
    // regression).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::vector<float> b1( 25, 10.0f );
    std::vector<float> b2( 25, 10.0f );
    b2[0] = -9999.0f;
    const QString raw = writeFloatRaster( dir.filePath( "img_raw.tif" ), 5, 5, { b1, b2 },
                                          false, 0.0 );
    QFile vrtFile( dir.filePath( "img.vrt" ) );
    REQUIRE( vrtFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream out( &vrtFile );
    out << "<VRTDataset rasterXSize=\"5\" rasterYSize=\"5\">\n";
    out << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
    out << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">img_raw.tif</SourceFilename>"
           "<SourceBand>1</SourceBand></SimpleSource>\n";
    out << "  </VRTRasterBand>\n";
    out << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
    out << "    <NoDataValue>-9999</NoDataValue>\n";
    out << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">img_raw.tif</SourceFilename>"
           "<SourceBand>2</SourceBand></SimpleSource>\n";
    out << "  </VRTRasterBand>\n";
    out << "</VRTDataset>\n";
    vrtFile.close();

    Json::Value p;
    p["input"] = dir.filePath( "img.vrt" ).toStdString();
    p["output"] = dir.filePath( "seg.tif" ).toStdString();
    p["engine"] = "simple";
    p["smoothKernel"] = 1;
    p["quantizeBins"] = 8;
    p["minRegionSize"] = 1;
    const Json::Value result = runR5Operator( "rs:obia_segment", p, dir.path().toStdString() );
    REQUIRE( result["segments"].asInt() >= 1 );

    const auto labels = readBand( dir.filePath( "seg.tif" ), 1 );
    REQUIRE( labels[0] == 0.0f ); // void pixel stays unsegmented
    REQUIRE( labels[12] != 0.0f ); // clean center pixel is segmented
}
