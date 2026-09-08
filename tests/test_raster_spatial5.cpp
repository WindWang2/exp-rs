// test_raster_spatial5.cpp — Foundation 5.0 Milestone G: mask/label/window
// raster operators over the Milestone A primitives (kernel-backed E2E).
//
// Every expected value is hand-derivable from the small masks; the primitives
// themselves carry their own closed-form suites (test_primitives5).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Json::Value baseParams( const QTemporaryDir &dir, const char *in, const char *out )
{
    Json::Value params( Json::objectValue );
    params["input"] = dir.filePath( in ).toStdString();
    params["output"] = dir.filePath( out ).toStdString();
    return params;
}

std::vector<float> readBand( const QString &path )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    REQUIRE( ds.readBandData( 1, out.data(), ds.width(), ds.height() ) );
    return out;
}
} // namespace

TEST_CASE( "rs:morphology dilate grows the foreground", "[spatial][morphology][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // 5x5 single centre pixel.
    RsSyntheticRasterBuilder b( 5, 5, 1 );
    b.withCrs( "EPSG:32650" );
    b.withPixel( 1, 2, 2, 1.0f );
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:morphology" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    params["op"] = "dilate";
    RSOperatorContext context;
    REQUIRE_NOTHROW( op->run( params, context ) );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    // 3x3 block grown around the centre; NoData-free output is 0 elsewhere.
    REQUIRE( out[2 * 5 + 2] == 1.0f );
    REQUIRE( out[1 * 5 + 1] == 1.0f );
    REQUIRE( out[0] == 0.0f );
    int ones = 0;
    for ( const float v : out )
        ones += v == 1.0f;
    REQUIRE( ones == 9 );
}

TEST_CASE( "rs:connected_components labels two blobs in raster order",
           "[spatial][components][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder b( 4, 4, 1 );
    b.withCrs( "EPSG:32650" );
    b.withRect( 1, 0, 0, 1, 1, 1.0f );  // blob A: (0,0)
    b.withRect( 1, 3, 2, 4, 4, 1.0f );  // blob B: bottom-right
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:connected_components" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    RSOperatorContext context;
    const Json::Value result = op->run( params, context );
    REQUIRE( result["componentCount"].asInt() == 2 );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    REQUIRE( out[0] == 1.0f );      // first component in raster order
    REQUIRE( out[2 * 4 + 3] == 2.0f ); // second component
    REQUIRE( std::isnan( out[1 * 4 + 1] ) ); // background → NaN
}

TEST_CASE( "rs:sieve removes the small component", "[spatial][sieve][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder b( 6, 4, 1 );
    b.withCrs( "EPSG:32650" );
    b.withRect( 1, 0, 0, 3, 2, 1.0f );  // 3x2 = 6 pixels
    b.withPixel( 1, 5, 3, 1.0f );       // lone pixel
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:sieve" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    params["min_area_pixels"] = 2;
    RSOperatorContext context;
    const Json::Value result = op->run( params, context );
    REQUIRE( result["removedPixels"].asUInt() == 1 );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    REQUIRE( out[0] == 1.0f );
    REQUIRE( out[3 * 6 + 5] == 0.0f ); // sieved away
}

TEST_CASE( "rs:fill_holes closes interior background but keeps border seas",
           "[spatial][fill_holes][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // 5x5 ring of 1s with an interior 0 (hole) at (2,2); corner (0,0) is
    // border-connected background.
    RsSyntheticRasterBuilder b( 5, 5, 1 );
    b.withCrs( "EPSG:32650" );
    b.withRect( 1, 1, 1, 4, 4, 1.0f );
    b.withPixel( 1, 2, 2, 0.0f );
    b.withPixel( 1, 0, 0, 0.0f );
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:fill_holes" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    RSOperatorContext context;
    const Json::Value result = op->run( params, context );
    REQUIRE( result["filledPixels"].asUInt() == 1 );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    REQUIRE( out[2 * 5 + 2] == 1.0f ); // hole filled
    REQUIRE( out[0] == 0.0f );         // border sea untouched
}

TEST_CASE( "rs:proximity distances match hand-derived pixels", "[spatial][proximity][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder b( 5, 3, 1 );
    b.withCrs( "EPSG:32650" );
    b.withPixel( 1, 0, 1, 1.0f );
    b.withPixel( 1, 4, 1, 1.0f );
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:proximity" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    RSOperatorContext context;
    REQUIRE_NOTHROW( op->run( params, context ) );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    REQUIRE( out[1 * 5 + 0] == 0.0f );
    REQUIRE( out[1 * 5 + 2] == Catch::Approx( 2.0f ).margin( 1e-6 ) ); // midway
    REQUIRE( out[0 * 5 + 2] == Catch::Approx( std::sqrt( 5.0 ) ).margin( 1e-6 ) );
    REQUIRE( out[0 * 5 + 0] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
}

TEST_CASE( "rs:local_extrema flags the window max", "[spatial][extrema][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // 3x3 all 1 except centre 5: centre is the 3x3 max; the corner (0,0) is
    // the max of its own clipped window (border replicate policy).
    RsSyntheticRasterBuilder b( 3, 3, 1 );
    b.withCrs( "EPSG:32650" );
    b.withConstantValue( 1, 1.0f );
    b.withPixel( 1, 1, 1, 5.0f );
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:local_extrema" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    params["extremum"] = "max";
    RSOperatorContext context;
    REQUIRE_NOTHROW( op->run( params, context ) );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    // In a 3x3 raster every cell's window contains the 5 at (1,1), so the
    // centre is the only flagged maximum.
    REQUIRE( out[1 * 3 + 1] == 1.0f );
    REQUIRE( out[0] == 0.0f );
    REQUIRE( out[2] == 0.0f );
    int flagged = 0;
    for ( const float v : out )
        flagged += v == 1.0f;
    REQUIRE( flagged == 1 );
}

TEST_CASE( "rs:focal_stats mean and exclusion of NoData", "[spatial][focal][operator]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder b( 3, 1, 1 );
    b.withCrs( "EPSG:32650" );
    b.withNoData( -9999.0 );
    b.withPixel( 1, 0, 0, 2.0f );
    b.withPixel( 1, 1, 0, 4.0f );
    b.withPixel( 1, 2, 0, -9999.0f ); // NoData: excluded, never zeroed
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:focal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    params["stat"] = "mean";
    RSOperatorContext context;
    REQUIRE_NOTHROW( op->run( params, context ) );

    const auto out = readBand( dir.filePath( "out.tif" ) );
    // Centre window {2,4,NoData}: mean over 2 valid cells = 3.
    REQUIRE( out[1] == Catch::Approx( 3.0f ).margin( 1e-6 ) );
    REQUIRE( std::isnan( out[2] ) ); // NoData centre stays NaN
}

TEST_CASE( "rs:proximity refuses an all-background mask", "[spatial][proximity][contract]" )
{
    using namespace sicnu::testing;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder b( 3, 3, 1 );
    b.withCrs( "EPSG:32650" );
    b.withConstantValue( 1, 0.0f );
    const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:proximity" );
    REQUIRE( op != nullptr );
    Json::Value params = baseParams( dir, "in.tif", "out.tif" );
    RSOperatorContext context;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
