/***************************************************************************
 * test_operator_determinism_digest.cpp — R4 determinism digests (ADR 0124)
 *
 * Track 7 (R4 operator oracles). Each case runs one registered operator
 * TWICE over identical inputs and requires the two products to be identical:
 * rasters byte-exact (grid, dtype, geotransform, nodata declaration, raw
 * pixels — sicnu::testing::compareRastersBitExact), tabular products by
 * SHA-256 of the bytes. Serial single-threaded execution is the documented
 * regression anchor; a drift here means non-deterministic reduction order,
 * unscheduled threading, or time/pointer-dependent logic.
 *
 * Fixture grids deliberately include non-multiple-of-256 extents
 * (rs:focal_stats 300×300, rs:terrain_analysis 300×300) so window/tile
 * boundaries and halo paths are covered (brief WP-D review gate).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "raster_bit_compare.h"
#include "support/r4_operator_fixtures.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <string>
#include <vector>

using namespace r4fixtures;
using sicnu::testing::compareRastersBitExact;

namespace
{
/// Runs @p op twice into out1/out2 and requires the raster products to be
/// byte-identical (ADR 0124 serial anchor).
void requireRasterDigestStable( const std::string &op, const Json::Value &base,
                                const QString &out1, const QString &out2,
                                const QString &workDir, const char *outKey = "output" )
{
    Json::Value p1 = base;
    p1[outKey] = out1.toStdString();
    runOperator( op, p1, workDir.toStdString() );
    Json::Value p2 = base;
    p2[outKey] = out2.toStdString();
    runOperator( op, p2, workDir.toStdString() );

    const auto rep = compareRastersBitExact( out1.toStdString(), out2.toStdString() );
    if ( !rep.identical )
        FAIL( op << " digest drift: " << rep.detail );
}
} // anonymous namespace

TEST_CASE( "digest: spectral indices are byte-stable across runs", "[r4][digest][spectral]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 10 ) * 20;
    std::vector<float> nir( n ), red( n );
    for ( size_t i = 0; i < n; ++i )
    {
        nir[i] = static_cast<float>( ( i * 37 ) % 91 + 10 );
        red[i] = static_cast<float>( ( i * 53 ) % 61 + 5 );
    }
    nir[7] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "in.tif" ), 10, 20, { nir, red },
                                            true, kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["nir"] = 1;
    p["red"] = 2;
    requireRasterDigestStable( "rs:ndvi", p, dir.filePath( "ndvi1.tif" ),
                               dir.filePath( "ndvi2.tif" ), dir.path() );
    Json::Value pm;
    pm["input"] = input.toStdString();
    pm["green"] = 1;
    pm["swir"] = 2;
    requireRasterDigestStable( "rs:mndwi", pm, dir.filePath( "mndwi1.tif" ),
                               dir.filePath( "mndwi2.tif" ), dir.path() );
}

TEST_CASE( "digest: image enhancement stretch and ratio are byte-stable",
           "[r4][digest][enhancement]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 100 ) * 70;
    std::vector<float> v( n );
    for ( size_t i = 0; i < n; ++i )
        v[i] = static_cast<float>( i % 251 );
    v[1234] = static_cast<float>( kSentinel );
    const QString grid = writeFloatRaster( dir.filePath( "grid.tif" ), 100, 70, { v }, true,
                                           kSentinel );

    Json::Value ps;
    ps["input"] = grid.toStdString();
    ps["method"] = "stretch";
    ps["stretchType"] = "linear";
    requireRasterDigestStable( "rs:image_enhancement", ps, dir.filePath( "st1.tif" ),
                               dir.filePath( "st2.tif" ), dir.path() );

    const size_t m = static_cast<size_t>( 50 ) * 40;
    std::vector<float> b1( m, 6.0f ), b2( m, 3.0f );
    b2[77] = static_cast<float>( kSentinel );
    const QString pair = writeFloatRaster( dir.filePath( "pair.tif" ), 50, 40, { b1, b2 },
                                           true, kSentinel );
    Json::Value pr;
    pr["input"] = pair.toStdString();
    pr["method"] = "ratio_ihs";
    pr["transform"] = "ratio";
    pr["band1"] = 1;
    pr["band2"] = 2;
    requireRasterDigestStable( "rs:image_enhancement", pr, dir.filePath( "ra1.tif" ),
                               dir.filePath( "ra2.tif" ), dir.path() );
}

TEST_CASE( "digest: focal stats over a 300x300 grid (tile boundaries) is byte-stable",
           "[r4][digest][focal]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 300, h = 300;
    std::vector<float> v( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            v[static_cast<size_t>( r ) * w + c] =
                static_cast<float>( ( 10 * r + c ) % 401 );
    v[155 * 300 + 199] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "dem.tif" ), w, h, { v }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["window"] = 5;
    p["stat"] = "mean";
    requireRasterDigestStable( "rs:focal_stats", p, dir.filePath( "f1.tif" ),
                               dir.filePath( "f2.tif" ), dir.path() );
}

TEST_CASE( "digest: change difference and CVA are byte-stable", "[r4][digest][change]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 100 ) * 50;
    std::vector<float> b1( n ), b2( n ), a1( n ), a2( n );
    for ( size_t i = 0; i < n; ++i )
    {
        b1[i] = static_cast<float>( i % 97 + 1 );
        b2[i] = static_cast<float>( i % 89 + 2 );
        a1[i] = static_cast<float>( i % 83 + 3 );
        a2[i] = static_cast<float>( i % 71 + 4 );
    }
    const QString before = writeFloatRaster( dir.filePath( "b.tif" ), 100, 50, { b1, b2 },
                                             true, kSentinel );
    const QString after = writeFloatRaster( dir.filePath( "a.tif" ), 100, 50, { a1, a2 },
                                            true, kSentinel );
    Json::Value p;
    p["before"] = before.toStdString();
    p["after"] = after.toStdString();
    requireRasterDigestStable( "rs:change_difference", p, dir.filePath( "d1.tif" ),
                               dir.filePath( "d2.tif" ), dir.path() );
    requireRasterDigestStable( "rs:change_cva", p, dir.filePath( "c1.tif" ),
                               dir.filePath( "c2.tif" ), dir.path() );
}

TEST_CASE( "digest: terrain slope over a 300x300 grid (tile boundaries) is byte-stable",
           "[r4][digest][terrain]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 300, h = 300;
    std::vector<float> dem( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            dem[static_cast<size_t>( r ) * w + c] = static_cast<float>( 2 * c + r % 5 );
    const QString input = writeFloatRaster( dir.filePath( "dem.tif" ), w, h, { dem }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["product"] = "slope";
    p["cellSize"] = 1.0;
    p["zFactor"] = 1.0;
    p["nodata"] = kSentinel;
    requireRasterDigestStable( "rs:terrain_analysis", p, dir.filePath( "s1.tif" ),
                               dir.filePath( "s2.tif" ), dir.path() );
}

TEST_CASE( "digest: zonal statistics CSV is sha256-stable", "[r4][digest][zonal]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 10 ) * 10;
    std::vector<float> v( n );
    for ( size_t i = 0; i < n; ++i )
        v[i] = static_cast<float>( i * 7 % 100 );
    v[0] = static_cast<float>( kSentinel );
    v[42] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v }, true,
                                            kSentinel );
    const QString zones = writeZoneGeoJson( dir.filePath( "zones.geojson" ), "zone" );

    Json::Value base;
    base["input"] = input.toStdString();
    base["vector"] = zones.toStdString();
    base["zoneField"] = "zone";

    const QString csv1 = dir.filePath( "z1.csv" );
    const QString csv2 = dir.filePath( "z2.csv" );
    Json::Value p1 = base;
    p1["output"] = csv1.toStdString();
    runOperator( "rs:zonal_stats", p1, dir.path().toStdString() );
    Json::Value p2 = base;
    p2["output"] = csv2.toStdString();
    runOperator( "rs:zonal_stats", p2, dir.path().toStdString() );

    const QByteArray d1 = QCryptographicHash::hash(
        QFile( csv1 ).readAll(), QCryptographicHash::Sha256 );
    const QByteArray d2 = QCryptographicHash::hash(
        QFile( csv2 ).readAll(), QCryptographicHash::Sha256 );
    REQUIRE( d1 == d2 );
}

TEST_CASE( "digest: threshold raster is byte-stable", "[r4][digest][threshold]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 80 ) * 60;
    std::vector<float> v( n );
    for ( size_t i = 0; i < n; ++i )
        v[i] = static_cast<float>( ( i * 13 ) % 257 );
    const QString input = writeFloatRaster( dir.filePath( "grid.tif" ), 80, 60, { v }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["thresholdMethod"] = "manual";
    p["threshold"] = 128.0;
    requireRasterDigestStable( "rs:threshold_raster", p, dir.filePath( "t1.tif" ),
                               dir.filePath( "t2.tif" ), dir.path() );
}

TEST_CASE( "digest: spectral derivative is byte-stable", "[r4][digest][derivative]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const double wl[4] = { 100.0, 200.0, 300.0, 400.0 };
    std::vector<std::vector<float>> bands;
    for ( int b = 1; b <= 4; ++b )
    {
        std::vector<float> v( static_cast<size_t>( 20 ) * 15 );
        for ( size_t i = 0; i < v.size(); ++i )
            v[i] = static_cast<float>( 1.0 + wl[b - 1] / 100.0 + ( i % 7 ) * 0.001 );
        v[101] = static_cast<float>( kSentinel );
        bands.push_back( std::move( v ) );
    }
    const QString input = writeFloatRaster( dir.filePath( "spec.tif" ), 20, 15, bands, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["order"] = 2;
    Json::Value axis( Json::arrayValue );
    for ( const double w : wl )
        axis.append( w );
    p["wavelengths"] = axis;
    requireRasterDigestStable( "rs:spectral_derivative", p, dir.filePath( "d1.tif" ),
                               dir.filePath( "d2.tif" ), dir.path() );
}

TEST_CASE( "digest: PCA components are byte-stable across runs", "[r4][digest][pca]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 60 ) * 50;
    std::vector<float> b1( n ), b2( n ), b3( n );
    for ( size_t i = 0; i < n; ++i )
    {
        const float t = static_cast<float>( i % 211 );
        b1[i] = t;
        b2[i] = 0.5f * t + 7.0f;
        b3[i] = 2.0f * t - 3.0f;
    }
    b1[500] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "cube.tif" ), 60, 50,
                                            { b1, b2, b3 }, true, kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["numComponents"] = 3;
    requireRasterDigestStable( "rs:pca", p, dir.filePath( "p1.tif" ),
                               dir.filePath( "p2.tif" ), dir.path() );
}

TEST_CASE( "digest: SAR speckle filter is byte-stable", "[r4][digest][sar]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 64 ) * 64;
    std::vector<float> v( n );
    for ( size_t i = 0; i < n; ++i )
        v[i] = static_cast<float>( 0.1 + ( i * 37 % 100 ) / 100.0 );
    v[512] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "sar.tif" ), 64, 64, { v }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["method"] = "lee";
    p["kernelSize"] = 5;
    requireRasterDigestStable( "rs:sar_speckle", p, dir.filePath( "sp1.tif" ),
                               dir.filePath( "sp2.tif" ), dir.path() );
}

TEST_CASE( "digest: mosaic overlap merge is byte-stable", "[r4][digest][mosaic]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 10 ) * 10;
    std::vector<float> a( n ), b( n );
    for ( size_t i = 0; i < n; ++i )
    {
        a[i] = static_cast<float>( i );
        b[i] = static_cast<float>( 1000 + i );
    }
    b[0] = static_cast<float>( kSentinel );
    const QString inA = writeFloatRaster( dir.filePath( "a.tif" ), 10, 10, { a }, true,
                                          kSentinel );
    const QString inB = writeFloatRaster( dir.filePath( "b.tif" ), 10, 10, { b }, true,
                                          kSentinel );
    Json::Value p;
    Json::Value inputs( Json::arrayValue );
    inputs.append( inA.toStdString() );
    inputs.append( inB.toStdString() );
    p["inputs"] = inputs;
    requireRasterDigestStable( "rs:mosaic", p, dir.filePath( "m1.tif" ),
                               dir.filePath( "m2.tif" ), dir.path() );
}

TEST_CASE( "digest: spectral similarity labels are byte-stable", "[r4][digest][similarity]" )
{
    // Closes the review P2-1 gate gap: 12 digest cases. The similarity seam
    // carries a per-tile classification (background covariance-free, but a
    // per-pixel scan over references) — byte-stable labels and scores across
    // runs evidence the serial anchor for it too.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const size_t n = static_cast<size_t>( 30 ) * 20;
    std::vector<float> b1( n ), b2( n );
    for ( size_t i = 0; i < n; ++i )
    {
        b1[i] = static_cast<float>( i % 17 ) + 1.0f;
        b2[i] = static_cast<float>( ( i * 7 ) % 23 ) + 1.0f;
    }
    b1[99] = 255.0f;
    b2[99] = 255.0f; // declared-sentinel void
    const QString input = writeFloatRaster( dir.filePath( "img.tif" ), 30, 20, { b1, b2 },
                                            true, 255.0 );
    Json::Value refs( Json::arrayValue );
    Json::Value r0( Json::arrayValue );
    r0.append( 2 );
    r0.append( 1 );
    Json::Value r1( Json::arrayValue );
    r1.append( 1 );
    r1.append( 2 );
    refs.append( r0 );
    refs.append( r1 );
    Json::Value p;
    p["input"] = input.toStdString();
    p["refs"] = refs;
    requireRasterDigestStable( "rs:spectral_similarity", p, dir.filePath( "sim1.tif" ),
                               dir.filePath( "sim2.tif" ), dir.path() );
}
