// test_terrain_agent_tools.cpp — terrain-hydrology-11 Phase 4: the terrain
// spatial agent tools (profile + viewshed quick inspect) against synthetic
// DEMs, including Unicode-path handling and error contracts.

#include "synthetic_raster_builder.h"

#include "agent/spatial_tools/terrain_spatial_tools.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <sstream>
#include <string>

using namespace sicnu::agent::spatial_tools;
using sicnu::testing::RsSyntheticRasterBuilder;

namespace
{
constexpr float kNo = -9999.0f;

Json::Value jsonObject( const std::string &text )
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream( text );
    Json::parseFromStream( builder, stream, &value, &errors );
    return value;
}

SpatialToolPtr tool( const std::string &name )
{
    registerTerrainTools(); // idempotent (static guard)
    const auto t = SpatialToolRegistry::instance().find( name );
    REQUIRE( t.has_value() );
    return *t;
}
} // namespace

TEST_CASE( "spatial:terrain_profile returns exact elevations along a line",
           "[terrain][agent][profile]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 16;
    constexpr int kH = 8;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, static_cast<float>( x + 2 * y ) );
    const QString demPath = b.writeToDisk( dir.filePath( "dem.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    const auto result = tool( "spatial:terrain_profile" )
                            ->execute( jsonObject( R"( {"raster": ")" + demPath.toStdString()
                                                   + R"(", "from": "0,0", "to": "10,0", )"
                                                     R"("samples": 11} )" ) );
    REQUIRE( result.success );
    const Json::Value &out = result.output;
    REQUIRE( out["samples"].size() == 11 );
    // z = x on the first row; distances 0..10 map units.
    for ( int k = 0; k < 11; ++k )
    {
        INFO( "sample " << k );
        CHECK( out["samples"][k]["elevation"].asFloat()
               == Catch::Approx( static_cast<float>( k ) ) );
        CHECK( out["samples"][k]["distance"].asDouble()
               == Catch::Approx( static_cast<double>( k ) ) );
    }
    CHECK( out["summary"]["minElevation"].asDouble() == 0.0 );
    CHECK( out["summary"]["maxElevation"].asDouble() == 10.0 );
    CHECK( out["summary"]["elevationGain"].asDouble() == 10.0 );
}

TEST_CASE( "spatial:terrain_profile handles Unicode paths and rejects bad input",
           "[terrain][agent][profile]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString sub = dir.filePath( QStringLiteral( "地形测试" ) );
    REQUIRE( QDir().mkpath( sub ) );
    RsSyntheticRasterBuilder b( 6, 6, 1 );
    b.withCrs( "EPSG:32650" );
    for ( int y = 0; y < 6; ++y )
        for ( int x = 0; x < 6; ++x )
            b.withPixel( 1, x, y, 7.0f );
    const QString demPath = b.writeToDisk( sub + QStringLiteral( "/高程.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    const auto result = tool( "spatial:terrain_profile" )
                            ->execute( jsonObject( R"( {"raster": ")" + demPath.toStdString()
                                                   + R"(", "from": "1,1", "to": "4,4"} )" ) );
    REQUIRE( result.success );
    CHECK( result.output["samples"].size() == 64 );

    // Bad inputs surface as validation failures with codes.
    const auto missing =
        tool( "spatial:terrain_profile" )->execute( jsonObject( R"( {"raster": "/nonexistent.tif", "from": "0,0", "to": "1,1"} )" ) );
    CHECK_FALSE( missing.success );
    const auto badPair = tool( "spatial:terrain_profile" )
                             ->execute( jsonObject( R"( {"raster": ")" + demPath.toStdString()
                                                    + R"(", "from": "0", "to": "1,1"} )" ) );
    CHECK_FALSE( badPair.success );
    CHECK( badPair.errorCode == "INVALID_PARAMETER" );
}

TEST_CASE( "spatial:terrain_viewshed_inspect summarises plane visibility",
           "[terrain][agent][viewshed]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 21;
    constexpr int kH = 21;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, 100.0f );
    const QString demPath = b.writeToDisk( dir.filePath( "plane.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    const auto result =
        tool( "spatial:terrain_viewshed_inspect" )
            ->execute( jsonObject( R"( {"raster": ")" + demPath.toStdString()
                                   + R"(", "observer": "10,10", "observer_height": 2.0} )" ) );
    REQUIRE( result.success );
    CHECK( result.output["visibleCells"].asUInt64() == kW * kH );
    CHECK( result.output["visibleFraction"].asDouble() == Catch::Approx( 1.0 ) );
    REQUIRE( result.output["horizon"].size() == 24 ); // 15° sectors

    // Observer on a NoData cell fails with a validation code.
    RsSyntheticRasterBuilder g( 8, 8, 1 );
    g.withCrs( "EPSG:32650" );
    g.withNoData( kNo );
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
            g.withPixel( 1, x, y, 10.0f );
    g.withPixel( 1, 4, 4, kNo );
    const QString gdPath = g.writeToDisk( dir.filePath( "nodata.tif" ) );
    REQUIRE( !gdPath.isEmpty() );
    const auto bad = tool( "spatial:terrain_viewshed_inspect" )
                         ->execute( jsonObject( R"( {"raster": ")" + gdPath.toStdString()
                                                + R"(", "observer": "4,4"} )" ) );
    CHECK_FALSE( bad.success );
    CHECK( bad.errorCode == "INVALID_PARAMETER" );
}
