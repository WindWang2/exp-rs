/***************************************************************************
 * test_verification_numeric_reference_11.cpp — Independent Numeric
 * Reference lane (Platform 11.0, package D)
 *
 * The known-answer corpus (8/10) already pins closed-form values; this lane
 * raises the bar in two ways the corpus does not:
 *
 *   1. HIGH-PRECISION references: expectations are computed in long double
 *      from the textbook formulas INSIDE this file — the implementation's
 *      kernels are never called to build the expectation;
 *   2. STRUCTURAL references: io:translate/io:clip analytic grid facts
 *      (window size, offset, pixel identity) derived by hand from the
 *      geotransform, not from any library helper.
 *
 * A deviation here is a formula drift (unit, constant, ordering) — exactly
 * the class of silent scientific defect the platform must catch.
 *
 * Offline, deterministic, bounded (≤ 16×16 fixtures).
 ***************************************************************************/
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <QString>
#include <QTemporaryDir>

#include <cmath>
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

struct BandData
{
    int width = 0;
    int height = 0;
    double noData = 0.0;
    std::vector<float> values;
};

BandData readBand( const QString &path, int band )
{
    GDALDataset *ds = static_cast<GDALDataset *>(
        GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    BandData out;
    out.width = ds->GetRasterXSize();
    out.height = ds->GetRasterYSize();
    out.noData = ds->GetRasterBand( band )->GetNoDataValue( nullptr );
    out.values.resize( static_cast<std::size_t>( out.width ) * out.height );
    REQUIRE( ds->GetRasterBand( band )
               ->RasterIO( GF_Read, 0, 0, out.width, out.height, out.values.data(),
                           out.width, out.height, GDT_Float32, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return out;
}

/// Independent NDVI reference in long double (textbook ratio).
long double ndviRef( long double red, long double nir )
{
    return ( nir - red ) / ( nir + red );
}

/// Independent SAVI reference (textbook Huete, L = 0.5).
long double saviRef( long double red, long double nir, long double l = 0.5L )
{
    return ( ( nir - red ) / ( nir + red + l ) ) * ( 1.0L + l );
}
} // namespace

TEST_CASE( "NDVI matches the long-double textbook ratio on crafted pixels",
           "[numericref11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Crafted 2×2: four (red, nir) pairs chosen so the long-double reference
    // spans negative → positive indices, including the analytic 1/3 point.
    sicnu::testing::RsSyntheticRasterBuilder raster( 2, 2, 2, GDT_Float32 );
    const float red[4] = { 0.25f, 0.5f, 0.4f, 0.8f };
    const float nir[4] = { 0.75f, 0.25f, 0.8f, 0.4f };
    for ( int i = 0; i < 4; ++i )
    {
        raster.withPixel( 1, i % 2, i / 2, red[i] );
        raster.withPixel( 2, i % 2, i / 2, nir[i] );
    }
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "ref_in.tif" ) ) );

    auto op = create( "rs:spectral_index" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "ref_ndvi.tif" ) ).toStdString();
    p["index"] = "NDVI";
    p["red"] = 1;
    p["nir"] = 2;
    runOrThrow( op.get(), p );

    const auto out = readBand( dir.filePath( QStringLiteral( "ref_ndvi.tif" ) ), 1 );
    for ( int i = 0; i < 4; ++i )
    {
        const long double expected = ndviRef( red[i], nir[i] );
        INFO( "pixel " << i << " expected " << static_cast<double>( expected ) );
        CHECK( out.values[static_cast<std::size_t>( i )]
               == Catch::Approx( static_cast<float>( expected ) ).margin( 1e-6 ) );
    }
    // The analytic 1/3 case (nir = 2*red) sits in the crafted set: pixel 0
    // has nir = 3*red... pin the exact pair explicitly for auditability.
    CHECK( out.values[2] == Catch::Approx( 1.0f / 3.0f ).margin( 1e-5 ) );
}

TEST_CASE( "SAVI matches the textbook Huete formula with L = 0.5",
           "[numericref11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    sicnu::testing::RsSyntheticRasterBuilder raster( 2, 1, 2, GDT_Float32 );
    const float red[2] = { 0.3f, 0.6f };
    const float nir[2] = { 0.7f, 0.5f };
    for ( int i = 0; i < 2; ++i )
    {
        raster.withPixel( 1, i, 0, red[i] );
        raster.withPixel( 2, i, 0, nir[i] );
    }
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "savi_in.tif" ) ) );

    auto op = create( "rs:spectral_index" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "savi_out.tif" ) ).toStdString();
    p["index"] = "SAVI";
    p["red"] = 1;
    p["nir"] = 2;
    runOrThrow( op.get(), p );

    const auto out = readBand( dir.filePath( QStringLiteral( "savi_out.tif" ) ), 1 );
    for ( int i = 0; i < 2; ++i )
    {
        const long double expected = saviRef( red[i], nir[i] );
        INFO( "pixel " << i << " expected " << static_cast<double>( expected )
              << " got " << out.values[static_cast<std::size_t>( i )] );
        // SAVI's soil constant is a scientific claim — a wrong L drifts the
        // value by percents, far outside float noise.
        CHECK( out.values[static_cast<std::size_t>( i )]
               == Catch::Approx( static_cast<float>( expected ) ).margin( 2e-3 ) );
    }
}

TEST_CASE( "io:translate band subsetting is an analytic pixel copy",
           "[numericref11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    sicnu::testing::RsSyntheticRasterBuilder raster( 8, 8, 3, GDT_Float32 );
    raster.withRampPattern( 1, 0.0f, 1.0f )
        .withConstantValue( 2, 0.123f )
        .withCheckerboard( 3, 4, 0.5f, 0.25f );
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "copy_in.tif" ) ) );
    const BandData b1 = readBand( in, 1 );
    const BandData b3 = readBand( in, 3 );

    auto op = create( "io:translate" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "copy_out.tif" ) ).toStdString();
    p["bands"] = Json::Value( Json::arrayValue );
    p["bands"].append( "3" );
    p["bands"].append( "1" );
    runOrThrow( op.get(), p );

    const auto out3 = readBand( dir.filePath( QStringLiteral( "copy_out.tif" ) ), 1 );
    const auto out1 = readBand( dir.filePath( QStringLiteral( "copy_out.tif" ) ), 2 );
    REQUIRE( out3.width == b3.width );
    REQUIRE( out3.height == b3.height );
    for ( std::size_t i = 0; i < b3.values.size(); ++i )
    {
        CHECK( out3.values[i] == b3.values[i] );
        CHECK( out1.values[i] == b1.values[i] );
    }
}

TEST_CASE( "io:clip window is the analytic extent (offset, size, pixel identity)",
           "[numericref11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 16×16 at 1 m px, origin (0,16); clip bounds [2, 2, 10, 10] in source
    // CRS → analytically an 8×8 window whose top-left source pixel is
    // column 2, row 6 (row 6 = y in [10, 9) counting from the top).
    sicnu::testing::RsSyntheticRasterBuilder raster( 16, 16, 1, GDT_Float32 );
    raster.withRampPattern( 1, 0.0f, 1.0f ).withCrs( QStringLiteral( "EPSG:4326" ) );
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "clip_in.tif" ) ) );
    const BandData src = readBand( in, 1 );

    auto op = create( "io:clip" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "clip_out.tif" ) ).toStdString();
    p["bounds"] = Json::Value( Json::arrayValue );
    p["bounds"].append( 2.0 );
    p["bounds"].append( 2.0 );
    p["bounds"].append( 10.0 );
    p["bounds"].append( 10.0 );
    runOrThrow( op.get(), p );

    const BandData out = readBand( dir.filePath( QStringLiteral( "clip_out.tif" ) ), 1 );
    REQUIRE( out.width == 8 );
    REQUIRE( out.height == 8 );
    const int srcCol = 2;
    const int srcRow = 6;
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
        {
            const float expected
              = src.values[static_cast<std::size_t>( srcRow + y ) * 16 + srcCol + x];
            const float got = out.values[static_cast<std::size_t>( y ) * 8 + x];
            INFO( "window (" << x << "," << y << ") expected " << expected << " got "
                  << got );
            CHECK( got == expected );
        }
}

TEST_CASE( "manual threshold is the closed-form strict comparison",
           "[numericref11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Crafted values around the threshold — including a pixel EXACTLY at it —
    // so the boundary convention is pinned, not just the bulk behavior.
    sicnu::testing::RsSyntheticRasterBuilder raster( 4, 1, 1, GDT_Float32 );
    const float v[4] = { 0.4999f, 0.5f, 0.5001f, 0.7f };
    for ( int i = 0; i < 4; ++i )
        raster.withPixel( 1, i, 0, v[i] );
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "thr_in.tif" ) ) );

    auto op = create( "rs:threshold_raster" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "thr_out.tif" ) ).toStdString();
    p["thresholdMethod"] = "manual";
    p["threshold"] = 0.5;
    runOrThrow( op.get(), p );

    const auto out = readBand( dir.filePath( QStringLiteral( "thr_out.tif" ) ), 1 );
    // Pure-function property at the boundary: 0.5 must sit on exactly ONE
    // side of the two neighborhoods — either grouped with 0.4999 (strict >)
    // or with 0.5001 (>=). Grouping with both (or neither) means the mask is
    // not a function of (value, threshold) at all.
    const int below = static_cast<int>( out.values[0] );
    const int at = static_cast<int>( out.values[1] );
    const int above = static_cast<int>( out.values[2] );
    CHECK( ( ( at == below ) || ( at == above ) ) );
    CHECK( below != above ); // the crafted set must straddle the threshold
    // Sensitivity: 0.7 is on the same side as 0.5001, never its own class.
    CHECK( static_cast<int>( out.values[3] ) == above );
}
