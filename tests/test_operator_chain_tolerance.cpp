/***************************************************************************
 * test_operator_chain_tolerance.cpp — R4 multi-operator tolerance chains
 *
 * Track 7 (R4 operator oracles). Each chain runs 2-3 registered operators
 * back-to-back and asserts the chain end against an independently derived
 * closed form. Truth sources: documented formulas (MTL radiance rescaling,
 * NDVI, normalized-difference change, CVA magnitude, Horn 3×3 slope on the
 * analytic z = 2x plane) — never implementation back-calculation.
 *
 * Tolerance layering (brief WP-C): pure-arithmetic chains 1e-5 relative;
 * the terrain chain is analytic to floating-point rounding (1e-6).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "support/r4_operator_fixtures.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <vector>

using namespace r4fixtures;

namespace
{
constexpr double kDeg = 57.29577951308232; // 180/π

void writeTextFile( const QString &path, const std::vector<std::string> &lines )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) );
    QTextStream ts( &f );
    for ( const auto &line : lines )
        ts << QString::fromStdString( line ) << "\n";
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Chain 1 — radiometric: MTL DN→radiance calibration → NDVI
// ---------------------------------------------------------------------------

TEST_CASE( "chain: MTL calibration to radiance then NDVI is algebraically "
           "gain-invariant",
           "[r4][chain][radiometric]" )
{
    // 2-band DN raster, 10x10: DN_nir(r,c) = r + c + 2, DN_red(r,c) = r + c + 1.
    // MTL: L = RADIANCE_MULT·DN + RADIANCE_ADD with mult=0.1, add=0.2 for
    // both bands. Chain end:
    //   NDVI = (0.1(n−r)) / (0.1(n+r) + 0.4) = 1 / (n + r + 4)
    //        = 1 / (2(r+c) + 7)
    // The gain cancels algebraically (7.0-matrix gain-invariance tradition);
    // the scene-independent ADD does NOT — asserting through two real
    // operators that the add survives the normalized difference.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 10, h = 10;
    std::vector<float> nir( static_cast<size_t>( w ) * h );
    std::vector<float> red( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
        {
            const size_t i = static_cast<size_t>( r ) * w + c;
            nir[i] = static_cast<float>( r + c + 2 );
            red[i] = static_cast<float>( r + c + 1 );
        }
    const QString dn = writeFloatRaster( dir.filePath( "dn.tif" ), w, h, { nir, red }, true,
                                         kSentinel );

    // Landsat-MTL-style key = value metadata (radiance rescaling per band).
    writeTextFile( dir.filePath( "scene.mtl" ),
                   { "GROUP = LANDSAT_METADATA_FILE",
                     "  RADIANCE_MULT_BAND_1 = 0.1",
                     "  RADIANCE_ADD_BAND_1 = 0.2",
                     "  RADIANCE_MULT_BAND_2 = 0.1",
                     "  RADIANCE_ADD_BAND_2 = 0.2",
                     "END_GROUP = LANDSAT_METADATA_FILE" } );

    Json::Value calib;
    calib["input"] = dn.toStdString();
    calib["output"] = dir.filePath( "radiance.tif" ).toStdString();
    calib["metadata_path"] = dir.filePath( "scene.mtl" ).toStdString();
    calib["unit"] = "radiance";
    runOperator( "rs:radiometric_calibration", calib, dir.path().toStdString() );

    Json::Value ndvi;
    ndvi["input"] = dir.filePath( "radiance.tif" ).toStdString();
    ndvi["output"] = dir.filePath( "ndvi.tif" ).toStdString();
    ndvi["nir"] = 1;
    ndvi["red"] = 2;
    runOperator( "rs:ndvi", ndvi, dir.path().toStdString() );

    const auto out = readBand( dir.filePath( "ndvi.tif" ), 1 );
    REQUIRE( out.size() == 100 );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
        {
            const double want = 1.0 / ( 2.0 * ( r + c ) + 7.0 );
            INFO( "pixel (" << r << "," << c << ") expected " << want );
            REQUIRE( nearRel( out[static_cast<size_t>( r ) * w + c], want, 1e-5 ) );
        }
}

// ---------------------------------------------------------------------------
// Chain 2 — change: difference → normalized difference → CVA magnitude
// ---------------------------------------------------------------------------

TEST_CASE( "chain: change difference, normalized difference and CVA stay closed-form",
           "[r4][chain][change]" )
{
    // The change primitives consume band 1 of each input (BIP band 0) and
    // emit ONE magnitude band; CVA is the only band-pair metric here.
    // Single-band inputs (10x10), before pixel i = 1+i, after = 2i+3:
    //   difference (legacy facade): |after − before| = i + 2
    //   normalized difference:      (i+2) / ((2i+3)+(1+i)) = (i+2)/(3i+4)
    // Two-band inputs for CVA, before = (1+i, 1+i), after = (2i+3, 3i+4):
    //   CVA magnitude: √(Δ_1² + Δ_2²) = √((i+2)² + (2i+3)²)
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 10, h = 10;
    const size_t n = static_cast<size_t>( w ) * h;
    std::vector<float> before1( n ), before2( n ), after1( n ), after2( n ), b1s( n ), a1s( n );
    for ( int i = 0; i < w * h; ++i )
    {
        before1[static_cast<size_t>( i )] = static_cast<float>( 1 + i );
        before2[static_cast<size_t>( i )] = static_cast<float>( 1 + i );
        after1[static_cast<size_t>( i )] = static_cast<float>( 2 * i + 3 );
        after2[static_cast<size_t>( i )] = static_cast<float>( 3 * i + 4 );
        b1s[static_cast<size_t>( i )] = static_cast<float>( 1 + i );
        a1s[static_cast<size_t>( i )] = static_cast<float>( 2 * i + 3 );
    }
    const QString beforeS = writeFloatRaster( dir.filePath( "before1.tif" ), w, h, { b1s },
                                              true, kSentinel );
    const QString afterS = writeFloatRaster( dir.filePath( "after1.tif" ), w, h, { a1s },
                                             true, kSentinel );
    const QString before = writeFloatRaster( dir.filePath( "before.tif" ), w, h,
                                             { before1, before2 }, true, kSentinel );
    const QString after = writeFloatRaster( dir.filePath( "after.tif" ), w, h,
                                            { after1, after2 }, true, kSentinel );

    Json::Value pd;
    pd["before"] = beforeS.toStdString();
    pd["after"] = afterS.toStdString();
    pd["output"] = dir.filePath( "diff.tif" ).toStdString();
    runOperator( "rs:change_difference", pd, dir.path().toStdString() );

    const auto d1 = readBand( dir.filePath( "diff.tif" ), 1 );
    for ( int i = 0; i < w * h; ++i )
        REQUIRE( nearRel( d1[static_cast<size_t>( i )], i + 2.0, 1e-5 ) );

    Json::Value pn;
    pn["before"] = beforeS.toStdString();
    pn["after"] = afterS.toStdString();
    pn["output"] = dir.filePath( "nd.tif" ).toStdString();
    runOperator( "rs:change_normalized_difference", pn, dir.path().toStdString() );
    const auto nd = readBand( dir.filePath( "nd.tif" ), 1 );
    for ( int i = 0; i < w * h; ++i )
        REQUIRE( nearRel( nd[static_cast<size_t>( i )],
                          static_cast<double>( i + 2 ) / ( 3 * i + 4 ), 1e-5 ) );

    Json::Value pc;
    pc["before"] = before.toStdString();
    pc["after"] = after.toStdString();
    pc["output"] = dir.filePath( "cva.tif" ).toStdString();
    runOperator( "rs:change_cva", pc, dir.path().toStdString() );
    const auto cva = readBand( dir.filePath( "cva.tif" ), 1 );
    for ( int i = 0; i < w * h; ++i )
    {
        const double want = std::sqrt( std::pow( i + 2.0, 2 ) + std::pow( 2 * i + 3.0, 2 ) );
        REQUIRE( nearRel( cva[static_cast<size_t>( i )], want, 1e-5 ) );
    }
}

// ---------------------------------------------------------------------------
// Chain 3 — terrain: Horn slope/aspect on the analytic z = 2x plane
// ---------------------------------------------------------------------------

TEST_CASE( "chain: terrain slope/aspect on the z=2x plane matches atan(2) with "
           "NoData centres never fabricated",
           "[r4][chain][terrain]" )
{
    // 8x8 DEM, z = 2·col (east-west linear plane), unit cell size, zFactor 1.
    // Horn 3×3 on a linear plane is exact (the 7.0-matrix tradition):
    //   slope  = atan(2) in degrees ≈ 63.43494882…
    //   aspect = 270 (east-facing, compass convention; atan2(−dzdx, dzdy))
    // The outer ring is declared NoData (-9999). The kernel's NoData
    // fallback reduces a window with sentinel neighbours to a 2-pixel
    // gradient — on a linear plane the 2-pixel gradient equals the Horn
    // result, so every valid centre must return atan(2) (an invariance
    // across both gradient paths); an invalid CENTRE is never fabricated:
    // it propagates NoData.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const int w = 8, h = 8;
    std::vector<float> dem( static_cast<size_t>( w ) * h, static_cast<float>( kSentinel ) );
    for ( int r = 1; r < h - 1; ++r )
        for ( int c = 1; c < w - 1; ++c )
            dem[static_cast<size_t>( r ) * w + c] = static_cast<float>( 2 * c );
    const QString demPath = writeFloatRaster( dir.filePath( "dem.tif" ), w, h, { dem }, true,
                                              kSentinel );

    Json::Value ps;
    ps["input"] = demPath.toStdString();
    ps["output"] = dir.filePath( "slope.tif" ).toStdString();
    ps["product"] = "slope";
    ps["cellSize"] = 1.0;
    ps["zFactor"] = 1.0;
    ps["nodata"] = kSentinel;
    runOperator( "rs:terrain_analysis", ps, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto slope = readBand( dir.filePath( "slope.tif" ), 1, &hasNd, &declared );
    const double atan2deg = std::atan( 2.0 ) * kDeg;
    for ( int r = 1; r < h - 1; ++r )
        for ( int c = 1; c < w - 1; ++c )
        {
            INFO( "slope pixel (" << r << "," << c << ")" );
            REQUIRE( nearRel( slope[static_cast<size_t>( r ) * w + c], atan2deg, 1e-6 ) );
        }
    // Sentinel centres (the outer ring) propagate NoData — never 0.
    REQUIRE( nearRel( slope[0], kSentinel ) );
    REQUIRE( nearRel( slope[static_cast<size_t>( 7 ) * w + 7], kSentinel ) );

    Json::Value pa;
    pa["input"] = demPath.toStdString();
    pa["output"] = dir.filePath( "aspect.tif" ).toStdString();
    pa["product"] = "aspect";
    pa["cellSize"] = 1.0;
    pa["zFactor"] = 1.0;
    pa["nodata"] = kSentinel;
    runOperator( "rs:terrain_analysis", pa, dir.path().toStdString() );
    const auto aspect = readBand( dir.filePath( "aspect.tif" ), 1 );
    REQUIRE( nearRel( aspect[static_cast<size_t>( 3 ) * w + 3], 270.0, 1e-6 ) );
    REQUIRE( nearRel( aspect[static_cast<size_t>( 3 ) * w + 5], 270.0, 1e-6 ) );
    REQUIRE( nearRel( aspect[static_cast<size_t>( 1 ) * w + 5], 270.0, 1e-6 ) );
}
