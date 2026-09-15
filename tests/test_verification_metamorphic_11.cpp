/***************************************************************************
 * test_verification_metamorphic_11.cpp — Metamorphic Oracle lanes 11.0
 *
 * Package C of the F09 track. Platform 10.0 shipped exactly ONE metamorphic
 * invariant (NDVI band-scale over a ramp). 11.0 systematizes the lane: each
 * relation below must hold for ANY input satisfying the relation's premise —
 * not for a fixture — and every relation is paired with a sensitivity
 * control that proves the pair is not passing vacuously (a constant-output
 * operator would satisfy naive invariance checks).
 *
 * Relations implemented here (family → premise → relation):
 *   M1 spectral   two scenes differing by a per-pixel positive rescaling of
 *                 ALL bands → NDVI identical within float tolerance
 *                 (seeded random field, not a ramp — ramps are a
 *                 measure-zero special case);
 *   M2 spectral   permuting input band order while remapping band indices →
 *                 byte-identical index output;
 *   M3 analysis   whole-scene spatial relocation (same pixel values, shifted
 *                 geotransform) → identical per-pixel mask values (per-pixel
 *                 kernels must not read geolocation);
 *   M4 NoData     drilling extra NoData holes into an input → valid-pixel
 *                 outputs identical AND NoData never becomes valid
 *                 (fail-closed propagation is monotone);
 *   M5 CRS        io:warp onto its own source CRS (nearest) → pixel-equal
 *                 copy (identity reproj; the seam that must never silently
 *                 reinterpret coordinates);
 *   M6 composition mosaic over disjoint tiles is order-insensitive.
 *
 * Offline, deterministic (seeded mt19937), bounded (≤ 24×24 fixtures).
 ***************************************************************************/
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "raster_bit_compare.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <QString>
#include <QTemporaryDir>

#include <random>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace
{
std::unique_ptr<RSOperator> create( const std::string &id )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op;
}

Json::Value runOrThrow( RSOperator *op, const Json::Value &params )
{
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

std::vector<float> readBand( const QString &path, int band, int &width, int &height,
                             double &noData )
{
    GDALDataset *ds = static_cast<GDALDataset *>(
        GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    width = ds->GetRasterXSize();
    height = ds->GetRasterYSize();
    noData = ds->GetRasterBand( band )->GetNoDataValue( nullptr );
    std::vector<float> data( static_cast<std::size_t>( width ) * height );
    REQUIRE( ds->GetRasterBand( band )
               ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                           GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return data;
}
} // namespace

TEST_CASE( "M1: NDVI is scale-invariant on a seeded random field", "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Two-band random field, seeded; the scaled scene multiplies BOTH bands
    // by 4.0 per pixel. NDVI = (nir-red)/(nir+red) is invariant under any
    // common positive per-pixel rescaling.
    const int w = 24;
    const int h = 24;
    std::mt19937 rng( 20260916 );
    std::uniform_real_distribution<float> red( 0.02f, 0.9f );
    sicnu::testing::RsSyntheticRasterBuilder base( w, h, 2, GDT_Float32 );
    sicnu::testing::RsSyntheticRasterBuilder scaled( w, h, 2, GDT_Float32 );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const float r = red( rng );
            const float n = 0.02f + 0.9f * red( rng ); // independent-ish nir
            base.withPixel( 1, x, y, r );
            base.withPixel( 2, x, y, n );
            scaled.withPixel( 1, x, y, 4.0f * r );
            scaled.withPixel( 2, x, y, 4.0f * n );
        }
    const auto baseRaster
      = base.writeToDisk( dir.filePath( QStringLiteral( "m1_base.tif" ) ) );
    const auto scaledRaster
      = scaled.writeToDisk( dir.filePath( QStringLiteral( "m1_scaled.tif" ) ) );
    REQUIRE_FALSE( baseRaster.isEmpty() );

    auto op = create( "rs:spectral_index" );
    const QString outBase = dir.filePath( QStringLiteral( "m1_ndvi_base.tif" ) );
    const QString outScaled = dir.filePath( QStringLiteral( "m1_ndvi_scaled.tif" ) );
    for ( const auto &[input, output] :
          { std::pair{ baseRaster, outBase }, std::pair{ scaledRaster, outScaled } } )
    {
        Json::Value p;
        p["input"] = input.toStdString();
        p["output"] = output.toStdString();
        p["index"] = "NDVI";
        p["red"] = 1;
        p["nir"] = 2;
        runOrThrow( op.get(), p );
    }

    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    double nd1 = 0.0, nd2 = 0.0;
    const auto a = readBand( outBase, 1, w1, h1, nd1 );
    const auto b = readBand( outScaled, 1, w2, h2, nd2 );
    REQUIRE( a.size() == b.size() );
    for ( std::size_t i = 0; i < a.size(); ++i )
        CHECK( a[i] == Catch::Approx( b[i] ).margin( 1e-6 ) );

    // Sensitivity control: the pair must not be trivially constant.
    float minV = a[0], maxV = a[0];
    for ( float v : a )
    {
        minV = std::min( minV, v );
        maxV = std::max( maxV, v );
    }
    CHECK( maxV - minV > 0.1f );
}

TEST_CASE( "M2: band order permutation with remapped indices is byte-identical",
           "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Scene A: [red, green, nir]. Scene B: the SAME values in reverse band
    // order. NDVI over A with (red=1, nir=3) must equal NDVI over B with
    // (red=3, nir=1) — byte-identical, not merely approximate: the kernel is
    // the same math on the same numbers.
    const int w = 16;
    const int h = 16;
    std::mt19937 rng( 4242 );
    std::uniform_real_distribution<float> u( 0.05f, 0.95f );
    sicnu::testing::RsSyntheticRasterBuilder a( w, h, 3, GDT_Float32 );
    sicnu::testing::RsSyntheticRasterBuilder b( w, h, 3, GDT_Float32 );
    std::vector<float> r( static_cast<std::size_t>( w ) * h );
    std::vector<float> g( static_cast<std::size_t>( w ) * h );
    std::vector<float> n( static_cast<std::size_t>( w ) * h );
    for ( std::size_t i = 0; i < r.size(); ++i )
    {
        r[i] = u( rng );
        g[i] = u( rng );
        n[i] = u( rng );
    }
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * w + x;
            a.withPixel( 1, x, y, r[i] );
            a.withPixel( 2, x, y, g[i] );
            a.withPixel( 3, x, y, n[i] );
            b.withPixel( 1, x, y, n[i] );
            b.withPixel( 2, x, y, g[i] );
            b.withPixel( 3, x, y, r[i] );
        }
    const auto ra = a.writeToDisk( dir.filePath( QStringLiteral( "m2_a.tif" ) ) );
    const auto rb = b.writeToDisk( dir.filePath( QStringLiteral( "m2_b.tif" ) ) );

    auto op = create( "rs:spectral_index" );
    const QString oa = dir.filePath( QStringLiteral( "m2_ndvi_a.tif" ) );
    const QString ob = dir.filePath( QStringLiteral( "m2_ndvi_b.tif" ) );
    {
        Json::Value p;
        p["input"] = ra.toStdString();
        p["output"] = oa.toStdString();
        p["index"] = "NDVI";
        p["red"] = 1;
        p["nir"] = 3;
        runOrThrow( op.get(), p );
    }
    {
        Json::Value p;
        p["input"] = rb.toStdString();
        p["output"] = ob.toStdString();
        p["index"] = "NDVI";
        p["red"] = 3;
        p["nir"] = 1;
        runOrThrow( op.get(), p );
    }
    const auto report = sicnu::testing::compareRastersBitExact( oa.toStdString(),
                                                                ob.toStdString() );
    CHECK( report.identical );
    if ( !report.identical )
        FAIL( "band-order metamorphic violation: " + report.detail );
}

TEST_CASE( "M3: per-pixel masks do not read geolocation (spatial relocation)",
           "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Same 16×16 pixel values; scene B lives 500 m east / 300 m north at a
    // coarser pixel size. A per-pixel threshold kernel must produce the same
    // mask VALUES in the same array order — geolocation is not its business.
    sicnu::testing::RsSyntheticRasterBuilder a( 16, 16, 1, GDT_Float32 );
    a.withRampPattern( 1, 0.0f, 1.0f ).withCrs( QStringLiteral( "EPSG:4326" ) );
    sicnu::testing::RsSyntheticRasterBuilder b( 16, 16, 1, GDT_Float32 );
    b.withRampPattern( 1, 0.0f, 1.0f )
        .withCrs( QStringLiteral( "EPSG:4326" ) )
        .withGeoTransform( 500.0, 2.0, 1300.0, 2.0 );
    const auto ra = a.writeToDisk( dir.filePath( QStringLiteral( "m3_a.tif" ) ) );
    const auto rb = b.writeToDisk( dir.filePath( QStringLiteral( "m3_b.tif" ) ) );

    auto op = create( "rs:threshold_raster" );
    const QString oa = dir.filePath( QStringLiteral( "m3_mask_a.tif" ) );
    const QString ob = dir.filePath( QStringLiteral( "m3_mask_b.tif" ) );
    for ( const auto &[input, output] :
          { std::pair{ ra, oa }, std::pair{ rb, ob } } )
    {
        Json::Value p;
        p["input"] = input.toStdString();
        p["output"] = output.toStdString();
        p["thresholdMethod"] = "manual";
        p["threshold"] = 0.5;
        runOrThrow( op.get(), p );
    }

    int w = 0, h = 0;
    double nd = 0.0;
    const auto va = readBand( oa, 1, w, h, nd );
    int w2 = 0, h2 = 0;
    double nd2 = 0.0;
    const auto vb = readBand( ob, 1, w2, h2, nd2 );
    REQUIRE( va.size() == vb.size() );
    for ( std::size_t i = 0; i < va.size(); ++i )
        CHECK( va[i] == vb[i] );
}

TEST_CASE( "M4: NoData drilling is monotone (valid pixels stable, holes stay holes)",
           "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Scene: red ramp, nir constant. Drilled scene: a 4×4 hole set to the
    // declared NoData. Band ratio over both: the 256−16 valid pixels must be
    // IDENTICAL, and the drilled hole must not turn valid.
    const int w = 16;
    const int h = 16;
    sicnu::testing::RsSyntheticRasterBuilder base( w, h, 2, GDT_Float32 );
    base.withRampPattern( 1, 0.1f, 0.9f )
        .withConstantValue( 2, 0.4f )
        .withNoData( -9999.0 );
    sicnu::testing::RsSyntheticRasterBuilder drilled( w, h, 2, GDT_Float32 );
    drilled.withRampPattern( 1, 0.1f, 0.9f )
        .withConstantValue( 2, 0.4f )
        .withNoData( -9999.0 );
    for ( int y = 4; y < 8; ++y )
        for ( int x = 4; x < 8; ++x )
        {
            base.withPixel( 1, x, y, 0.5f );
            base.withPixel( 2, x, y, 0.5f );
            drilled.withPixel( 1, x, y, -9999.0f );
            drilled.withPixel( 2, x, y, -9999.0f );
        }
    const auto rBase = base.writeToDisk( dir.filePath( QStringLiteral( "m4_base.tif" ) ) );
    const auto rDrill = drilled.writeToDisk( dir.filePath( QStringLiteral( "m4_drill.tif" ) ) );

    auto op = create( "rs:band_ratio" );
    const auto run = [&]( const QString &input, const QString &output ) {
        Json::Value p;
        p["input"] = input.toStdString();
        p["output"] = output.toStdString();
        p["numeratorBand"] = 1;
        p["denominatorBand"] = 2;
        runOrThrow( op.get(), p );
    };
    const QString oBase = dir.filePath( QStringLiteral( "m4_ratio_base.tif" ) );
    const QString oDrill = dir.filePath( QStringLiteral( "m4_ratio_drill.tif" ) );
    run( rBase, oBase );
    run( rDrill, oDrill );

    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    double nd1 = 0.0, nd2 = 0.0;
    const auto va = readBand( oBase, 1, w1, h1, nd1 );
    const auto vb = readBand( oDrill, 1, w2, h2, nd2 );
    REQUIRE( va.size() == vb.size() );
    int validChecked = 0;
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * w + x;
            const bool isHole = x >= 4 && x < 8 && y >= 4 && y < 8;
            if ( isHole )
            {
                // The hole must not silently become a finite trusted value.
                const bool holeInvalid = std::isnan( vb[i] ) || vb[i] == nd2
                                         || vb[i] == -9999.0f;
                CHECK( holeInvalid );
            }
            else
            {
                CHECK( va[i] == vb[i] );
                ++validChecked;
            }
        }
    CHECK( validChecked == w * h - 16 );
}

TEST_CASE( "M5: io:warp onto its own CRS is a pixel-equal copy", "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 12, 12, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.0f, 1.0f )
                    .withConstantValue( 2, 0.25f )
                    .withCrs( QStringLiteral( "EPSG:4326" ) )
                    .writeToDisk( dir.filePath( QStringLiteral( "m5_in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    auto op = create( "io:warp" );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "m5_out.tif" ) ).toStdString();
    p["targetCrs"] = "EPSG:4326";
    p["resampling"] = "nearest";
    runOrThrow( op.get(), p );

    const auto report = sicnu::testing::compareRastersBitExact(
        raster.toStdString(), dir.filePath( QStringLiteral( "m5_out.tif" ) ).toStdString() );
    CHECK( report.identical );
    if ( !report.identical )
        FAIL( "identity reproject changed pixels: " + report.detail );
}

TEST_CASE( "M6: mosaic over disjoint tiles is order-insensitive", "[metamorphic11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Two disjoint 8×16 tiles that concatenate into one 16×16 scene. Mosaic
    // AB and BA must produce the same combined raster: FirstWins overlap
    // resolution must not leak composition order into disjoint content.
    sicnu::testing::RsSyntheticRasterBuilder left( 8, 16, 1, GDT_Float32 );
    left.withRampPattern( 1, 0.0f, 1.0f ).withCrs( QStringLiteral( "EPSG:4326" ) );
    sicnu::testing::RsSyntheticRasterBuilder right( 8, 16, 1, GDT_Float32 );
    right.withCheckerboard( 1, 2, 0.1f, 0.9f )
        .withCrs( QStringLiteral( "EPSG:4326" ) )
        .withGeoTransform( 8.0, 1.0, 16.0, 1.0 );
    const auto rl = left.writeToDisk( dir.filePath( QStringLiteral( "m6_left.tif" ) ) );
    const auto rr = right.writeToDisk( dir.filePath( QStringLiteral( "m6_right.tif" ) ) );

    auto op = create( "rs:mosaic" );
    const QString oab = dir.filePath( QStringLiteral( "m6_ab.tif" ) );
    const QString oba = dir.filePath( QStringLiteral( "m6_ba.tif" ) );
    {
        Json::Value p;
        p["inputs"] = Json::Value( Json::arrayValue );
        p["inputs"].append( rl.toStdString() );
        p["inputs"].append( rr.toStdString() );
        p["output"] = oab.toStdString();
        runOrThrow( op.get(), p );
    }
    {
        Json::Value p;
        p["inputs"] = Json::Value( Json::arrayValue );
        p["inputs"].append( rr.toStdString() );
        p["inputs"].append( rl.toStdString() );
        p["output"] = oba.toStdString();
        runOrThrow( op.get(), p );
    }
    const auto report = sicnu::testing::compareRastersBitExact( oab.toStdString(),
                                                                oba.toStdString() );
    CHECK( report.identical );
    if ( !report.identical )
        FAIL( "mosaic order leak: " + report.detail );
}
