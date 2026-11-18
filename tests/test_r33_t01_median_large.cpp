// tests/test_r33_t01_median_large.cpp — float32 median for large kernels
// (R33-T01, issue #2088): OpenCV medianBlur supports CV_32F only at ksize
// 3|5 (verified against the locked OpenCV 5.0.0 imgproc contract); the
// operator validates kernels up to 101, so 7..101 used to reach OpenCV's
// CV_8U-only assertion. Larger kernels now run an EXACT selection median:
// original float values (no UInt8 rescale), NaN taps excluded (all-NaN →
// NaN), BORDER_REPLICATE like cv::medianBlur, even valid counts take the
// mean of the two central order statistics.
//
// Oracles: (a) a hand-tallied replicate-weighted multiset for the k=7
// centre pixel (median exactly 12), (b) an in-test brute-force tap
// collector implementing the same SPEC independently for every pixel,
// (c) degenerate uniform/NaN cases, (d) small kernels keep OpenCV parity
// against cv::medianBlur with the operator's mask semantics.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "operators/opencv/opencv_filter_operators.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t01_median_large";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

bool writeRaster( const QString &path, const std::vector<float> &values, int w, int h )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32,
                                  nullptr );
    if ( !ds )
        return false;
    const bool ok = GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, w, h,
                                  const_cast<float *>( values.data() ), w, h, GDT_Float32,
                                  0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

std::vector<float> readRaster( const QString &path )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( 1, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

void runMedian( const QString &in, const QString &out, int kernelSize )
{
    auto op = std::make_unique<sicnu::operators::opencv::OpenCvMedianBlurOperator>();
    Json::Value params( Json::objectValue );
    params["input"] = in.toStdString();
    params["output"] = out.toStdString();
    params["kernelSize"] = kernelSize;
    RSOperatorContext ctx;
    op->run( params, ctx );
}

/// SPEC-level independent oracle: collect the valid taps of the k×k
/// replicate-border window at (x,y), select the exact median (mean of the
/// two central order statistics on even counts).
float oracleMedian( const std::vector<float> &dem, int w, int h, int x, int y, int k )
{
    const int r = k / 2;
    std::vector<float> taps;
    for ( int j = -r; j <= r; ++j )
    {
        const int sy = std::clamp( y + j, 0, h - 1 );
        for ( int i = -r; i <= r; ++i )
        {
            const int sx = std::clamp( x + i, 0, w - 1 );
            const float v = dem[static_cast<size_t>( sy ) * w + sx];
            if ( std::isfinite( v ) )
                taps.push_back( v );
        }
    }
    if ( taps.empty() )
        return std::numeric_limits<float>::quiet_NaN();
    const size_t mid = taps.size() / 2;
    std::nth_element( taps.begin(), taps.begin() + static_cast<long>( mid ), taps.end() );
    float m = taps[mid];
    if ( ( taps.size() & 1u ) == 0u )
    {
        const float lower =
            *std::max_element( taps.begin(), taps.begin() + static_cast<long>( mid ) );
        m = 0.5f * ( lower + m );
    }
    return m;
}

} // anonymous namespace

TEST_CASE( "median ksize>5 runs an exact float32 selection median (#2088)",
           "[r33t01][opencv][median]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const float nan = std::numeric_limits<float>::quiet_NaN();

    SECTION( "kernel 7: hand-tallied centre, brute-force oracle everywhere" )
    {
        constexpr int kN = 5;
        const std::vector<float> dem = {
            1.0f,  2.0f,  3.0f,  4.0f,  5.0f,   //
            6.0f,  7.0f,  8.0f,  9.0f,  10.0f,  //
            11.0f, 12.0f, 13.0f, 14.0f, 15.0f,  //
            16.0f, 17.0f, 1e30f, 19.0f, 20.0f,  //
            21.0f, nan,  23.0f, 24.0f, 25.0f,
        };
        const QString in = tmp.filePath( "in7.tif" );
        REQUIRE( writeRaster( in, dem, kN, kN ) );
        const QString outPath = tmp.filePath( "out7.tif" );
        runMedian( in, outPath, 7 );
        const std::vector<float> out = readRaster( outPath );

        // Hand-tallied replicate weights for the centre (2,2), r=3 on 5×5:
        // per axis the clamped index multiset is {0×2, 1, 2, 3, 4×2}; the
        // NaN tap (4,1) has weight 2 → 47 valid taps. Cumulative tallies
        // put index 23 (0-based) of the sorted multiset on the value 12.
        REQUIRE( out[static_cast<size_t>( 2 ) * kN + 2] == Approx( 12.0f ).margin( 0 ) );

        // Brute-force spec oracle for EVERY pixel.
        for ( int y = 0; y < kN; ++y )
            for ( int x = 0; x < kN; ++x )
            {
                const size_t i = static_cast<size_t>( y ) * kN + x;
                const float expected = oracleMedian( dem, kN, kN, x, y, 7 );
                INFO( "pixel " << i );
                if ( std::isnan( expected ) )
                    REQUIRE( std::isnan( out[i] ) );
                else
                    REQUIRE( out[i] == expected );
            }
    }

    SECTION( "kernel 101 works and matches the oracle on a tiny raster" )
    {
        constexpr int kN = 4;
        std::vector<float> dem( kN * kN );
        for ( int i = 0; i < kN * kN; ++i )
            dem[i] = static_cast<float>( ( i * 53 ) % 29 ) - 14.0f; // spread
        dem[5] = nan;
        dem[10] = 1e30f;
        const QString in = tmp.filePath( "in101.tif" );
        REQUIRE( writeRaster( in, dem, kN, kN ) );
        const QString outPath = tmp.filePath( "out101.tif" );
        runMedian( in, outPath, 101 );
        const std::vector<float> out = readRaster( outPath );
        for ( int y = 0; y < kN; ++y )
            for ( int x = 0; x < kN; ++x )
            {
                const size_t i = static_cast<size_t>( y ) * kN + x;
                INFO( "pixel " << i );
                REQUIRE( out[i] == oracleMedian( dem, kN, kN, x, y, 101 ) );
            }
        // The 1e30 extreme is preserved exactly where the selection lands
        // on it (never rescaled/wrapped through a UInt8 path).
        REQUIRE( out[10] <= 1e30f );
        REQUIRE( std::isfinite( out[10] ) );
    }

    SECTION( "even valid count takes the mean of the two central stats" )
    {
        // 3×3 raster, only (0,0)=10 and (2,2)=20 valid. k=7 (r=3) at the
        // centre: clamped index multiset per axis is {0×3, 1, 2×3} → each
        // corner weight 9 → multiset {10×9, 20×9} = 18 taps (EVEN) →
        // median (10+20)/2 = 15 exactly.
        constexpr int kN = 3;
        std::vector<float> dem( kN * kN, std::numeric_limits<float>::quiet_NaN() );
        dem[0] = 10.0f;
        dem[8] = 20.0f;
        const QString in = tmp.filePath( "even.tif" );
        REQUIRE( writeRaster( in, dem, kN, kN ) );
        const QString outPath = tmp.filePath( "out_even.tif" );
        runMedian( in, outPath, 7 );
        const std::vector<float> out = readRaster( outPath );
        for ( int y = 0; y < kN; ++y )
            for ( int x = 0; x < kN; ++x )
            {
                const size_t i = static_cast<size_t>( y ) * kN + x;
                INFO( "pixel " << i );
                REQUIRE( out[i] == oracleMedian( dem, kN, kN, x, y, 7 ) );
            }
        REQUIRE( out[4] == Approx( 15.0f ).margin( 0 ) );
    }

    SECTION( "all-NaN raster → all-NaN output" )
    {
        constexpr int kN = 3;
        const std::vector<float> dem( kN * kN, nan );
        const QString in = tmp.filePath( "allnan.tif" );
        REQUIRE( writeRaster( in, dem, kN, kN ) );
        const QString outPath = tmp.filePath( "out_allnan.tif" );
        runMedian( in, outPath, 7 );
        for ( float v : readRaster( outPath ) )
            REQUIRE( std::isnan( v ) );
    }

    SECTION( "300×300 with k=9 streams across tile seams exactly" )
    {
        // Larger than the 256-pixel stream tile: pixels on the seam
        // (x = 255/256) must match the full-frame spec oracle bit-exactly.
        constexpr int kN = 300;
        std::vector<float> dem( static_cast<size_t>( kN ) * kN );
        for ( int y = 0; y < kN; ++y )
            for ( int x = 0; x < kN; ++x )
                dem[static_cast<size_t>( y ) * kN + x] =
                    static_cast<float>( ( ( x * 131 + y * 197 ) % 211 ) - 105 )
                    + ( ( x + y ) % 7 == 0 ? 0.25f : 0.0f );
        dem[static_cast<size_t>( 150 ) * kN + 150] = nan;
        dem[static_cast<size_t>( 256 ) * kN + 100] = nan; // just past the seam
        const QString in = tmp.filePath( "in300.tif" );
        REQUIRE( writeRaster( in, dem, kN, kN ) );
        const QString outPath = tmp.filePath( "out300.tif" );
        runMedian( in, outPath, 9 );
        const std::vector<float> out = readRaster( outPath );
        // Seam columns and the NaN neighbourhood, plus corners/edges.
        const int probeX[] = { 0, 1, 254, 255, 256, 257, 298, 299 };
        const int probeY[] = { 0, 1, 150, 255, 256, 299 };
        for ( int y : probeY )
            for ( int x : probeX )
            {
                const float expected = oracleMedian( dem, kN, kN, x, y, 9 );
                INFO( "y=" << y << " x=" << x );
                if ( std::isnan( expected ) )
                    REQUIRE( std::isnan( out[static_cast<size_t>( y ) * kN + x] ) );
                else
                    REQUIRE( out[static_cast<size_t>( y ) * kN + x] == expected );
            }
        // A single NaN TAP inside a 9×9 window does NOT void the pixel
        // (only an all-NaN window does): (150,150) keeps the selection
        // median of the 80 valid taps — matching the oracle exactly.
        REQUIRE( out[static_cast<size_t>( 150 ) * kN + 150]
                 == oracleMedian( dem, kN, kN, 150, 150, 9 ) );
        REQUIRE( std::isfinite( out[static_cast<size_t>( 150 ) * kN + 150] ) );
        REQUIRE( std::isfinite( out[0] ) );
    }
}

TEST_CASE( "median small kernels keep the OpenCV path (parity, #2088)",
           "[r33t01][opencv][median]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const float nan = std::numeric_limits<float>::quiet_NaN();

    constexpr int kN = 6;
    std::vector<float> dem( kN * kN );
    for ( int i = 0; i < kN * kN; ++i )
        dem[i] = static_cast<float>( ( i * 37 ) % 23 ) + 0.5f;
    dem[static_cast<size_t>( 2 ) * kN + 3] = nan; // hole → NaN mask path
    const QString in = tmp.filePath( "in3.tif" );
    REQUIRE( writeRaster( in, dem, kN, kN ) );

    for ( const int k : { 1, 3, 5 } )
    {
        const QString outPath = tmp.filePath( QString( "out_%1.tif" ).arg( k ) );
        runMedian( in, outPath, k );
        const std::vector<float> out = readRaster( outPath );

        // Independent OpenCV reference with the operator's mask semantics.
        cv::Mat ref( kN, kN, CV_32FC1, dem.data() );
        cv::Mat mask;
        cv::compare( ref, ref, mask, cv::CMP_EQ );
        cv::Mat clean = ref.clone();
        clean.setTo( 0.0f, ~mask );
        cv::Mat filtered;
        if ( k == 1 )
            filtered = clean.clone(); // identity
        else
            cv::medianBlur( clean, filtered, k );
        filtered.setTo( nan, ~mask );

        for ( int i = 0; i < kN * kN; ++i )
        {
            INFO( "k=" << k << " pixel " << i );
            const float expected = filtered.at<float>( i / kN, i % kN );
            if ( std::isnan( expected ) )
                REQUIRE( std::isnan( out[i] ) );
            else
                REQUIRE( out[i] == expected );
        }
    }
}
