// test_terrain_analytics_e2e.cpp — terrain-hydrology-11 Phase 4: operator
// surface E2E for the new terrain products (registry → synthetic DEM file →
// run → read output), plus cancellation and resource-guard semantics.

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_terrain_guard.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <json/json.h>

#include <atomic>
#include <cmath>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{
constexpr float kNo = -9999.0f;

struct RunResult
{
    Json::Value json;
    bool ok = false;
    RSOperatorError error = RSOperatorError( ErrorCode::Unknown, "" );
};

RunResult runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    RunResult run;
    if ( op == nullptr )
    {
        run.error = RSOperatorError( ErrorCode::InvalidParameter, "operator not registered" );
        return run;
    }
    RSOperatorContext context;
    try
    {
        run.json = op->run( params, context );
        run.ok = true;
    }
    catch ( const RSOperatorError &e )
    {
        run.error = e;
    }
    return run;
}
} // namespace

TEST_CASE( "rs:terrain_flow flat_resolve drains a plateau DEM",
           "[terrain][hydrology][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 12;
    constexpr int kH = 10;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, 50.0f );
    b.withPixel( 1, 0, 0, 40.0f ); // the drain
    const QString demPath = b.writeToDisk( dir.filePath( "plateau.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "resolved.tif" ).toStdString();
    params["product"] = "flat_resolve";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );
    CHECK( run.json["raisedCells"].asUInt64() > 0 );
    CHECK( run.json["flatEpsilon"].asDouble() > 0.0 );

    // Independent verification: D8 over the resolved output leaves no
    // interior sink.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "resolved.tif" ) ) );
    std::vector<float> resolved( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, resolved.data(), kW, kH ) );
    std::vector<float> dirGrid( resolved.size(), 0.0f );
    REQUIRE( TerrainFlow::flowDirections( resolved.data(), dirGrid.data(), kW, kH, kNo ) );
    for ( int y = 1; y < kH - 1; ++y )
        for ( int x = 1; x < kW - 1; ++x )
        {
            INFO( "cell (" << x << "," << y << ")" );
            REQUIRE( dirGrid[static_cast<size_t>( y ) * kW + x] != 0.0f );
        }
}

TEST_CASE( "rs:terrain_flow flow_direction_inf reports exact plane azimuths",
           "[terrain][hydrology][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 7;
    constexpr int kH = 5;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, static_cast<float>( x ) ); // descends east
    const QString demPath = b.writeToDisk( dir.filePath( "slope.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "dinf.tif" ).toStdString();
    params["product"] = "flow_direction_inf";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "dinf.tif" ) ) );
    std::vector<float> angles( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, angles.data(), kW, kH ) );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            INFO( "cell (" << x << "," << y << ") angle=" << angles[y * kW + x] );
            // z = +col descends WEST (270°); the west rim column is a pit.
            const float expected = x == 0 ? -1.0f : 270.0f;
            CHECK( angles[static_cast<size_t>( y ) * kW + x]
                   == Catch::Approx( expected ).margin( 1e-3 ) );
        }
}

TEST_CASE( "rs:terrain_flow stream_network extracts the thalweg with orders",
           "[terrain][hydrology][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // V-channel z = −2·|x−7| + 0.5·y on 15×15: thalweg drains south.
    constexpr int kW = 15;
    constexpr int kH = 15;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 500000.0, 10.0, 4000000.0, -10.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y,
                         static_cast<float>( -2.0 * std::fabs( x - 7 ) + 0.5 * y ) );
    const QString demPath = b.writeToDisk( dir.filePath( "valley.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "streams.tif" ).toStdString();
    params["product"] = "stream_network";
    params["threshold"] = 3.0;
    params["include_segments"] = "true";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );
    CHECK( run.json["streamCells"].asUInt64() >= kH ); // at least the thalweg
    CHECK( run.json["maxStrahlerOrder"].asInt() >= 1 );
    CHECK( run.json["segmentCount"].asUInt64() >= 1 );
    CHECK( run.json["segments"].isArray() );
    CHECK( run.json["segments"].size() >= 1 );
    // Segment coordinates are in map space (UTM origin + 10 m cells).
    const Json::Value &first = run.json["segments"][0]["line"][0];
    REQUIRE( first.size() == 2 );
    CHECK( first[0].asDouble() > 500000.0 );
    CHECK( first[1].asDouble() < 4000000.0 );

    // The raster carries Strahler orders ≥ 1 on stream cells.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "streams.tif" ) ) );
    std::vector<float> orders( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, orders.data(), kW, kH ) );
    int streamRaster = 0;
    for ( const float o : orders )
        streamRaster += o >= 1.0f ? 1 : 0;
    CHECK( streamRaster == run.json["streamCells"].asUInt64() );
}

TEST_CASE( "rs:terrain_flow outlets finds the plateau rim drain",
           "[terrain][hydrology][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 9;
    constexpr int kH = 9;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, 30.0f );
    b.withPixel( 1, 4, 0, 20.0f ); // north rim low point = outlet
    const QString demPath = b.writeToDisk( dir.filePath( "plateau.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "outlets.tif" ).toStdString();
    params["product"] = "outlets";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );
    CHECK( run.json["outletCount"].asUInt64() >= 1 );
    bool foundRimOutlet = false;
    for ( const Json::Value &o : run.json["outlets"] )
        if ( o["col"].asInt() == 4 && o["row"].asInt() == 0 )
            foundRimOutlet = true;
    CHECK( foundRimOutlet );
}

TEST_CASE( "rs:terrain_viewshed plane visibility, radius cut and curvature",
           "[terrain][viewshed][operator][e2e]" )
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

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "viewshed.tif" ).toStdString();
    params["product"] = "viewshed";
    params["observer"] = "10,10";
    params["observer_height"] = 2.0;
    const RunResult run = runOperator( "rs:terrain_viewshed", params );
    REQUIRE( run.ok );
    CHECK( run.json["visibleFraction"].asDouble() == Catch::Approx( 1.0 ) );

    // Radius cut: 5 m around (10,10).
    params["radius"] = 5.0;
    params["output"] = dir.filePath( "viewshed_r5.tif" ).toStdString();
    const RunResult runR = runOperator( "rs:terrain_viewshed", params );
    REQUIRE( runR.ok );
    int within = 0;
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            within += std::hypot( x - 10.0, y - 10.0 ) <= 5.0 ? 1 : 0;
    CHECK( runR.json["visibleCells"].asUInt64() == static_cast<Json::UInt64>( within ) );

    // Curvature on a projected CRS is accepted.
    Json::Value curved( params );
    curved["curvature"] = "true";
    curved["output"] = dir.filePath( "viewshed_curved.tif" ).toStdString();
    const RunResult runC = runOperator( "rs:terrain_viewshed", curved );
    CHECK( runC.ok );
    CHECK( runC.json["curvatureFactor"].asDouble() > 0.0 );

    // Cumulative with two observers.
    Json::Value cum( params );
    cum["product"] = "cumulative";
    cum["observers"] = "4,4;16,16";
    cum["output"] = dir.filePath( "cumulative.tif" ).toStdString();
    const RunResult run2 = runOperator( "rs:terrain_viewshed", cum );
    REQUIRE( run2.ok );

    // Refusal: curvature on a geographic CRS fails closed.
    RsSyntheticRasterBuilder g( 8, 8, 1 );
    g.withCrs( "EPSG:4326" );
    g.withGeoTransform( 0.0, 0.001, 0.0, -0.001 );
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
            g.withPixel( 1, x, y, 10.0f );
    const QString geoPath = g.writeToDisk( dir.filePath( "geo.tif" ) );
    REQUIRE( !geoPath.isEmpty() );
    Json::Value geoParams( params );
    geoParams["input"] = geoPath.toStdString();
    geoParams["curvature"] = "true";
    geoParams["output"] = dir.filePath( "geo_viewshed.tif" ).toStdString();
    const RunResult runG = runOperator( "rs:terrain_viewshed", geoParams );
    CHECK_FALSE( runG.ok );
    CHECK( runG.error.code() == ErrorCode::InvalidParameter );
}

TEST_CASE( "rs:terrain_solar shadow duration and hillshade series E2E",
           "[terrain][solar][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 11;
    constexpr int kH = 1;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, 1.0, -1.0 );
    for ( int x = 0; x < kW; ++x )
        b.withPixel( 1, x, 0, 0.0f );
    b.withPixel( 1, 2, 0, 10.0f ); // wall
    const QString demPath = b.writeToDisk( dir.filePath( "wall.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["sun_track"] = "90,45;90,45"; // two equal samples: full shadow east
    const RunResult run = runOperator( "rs:terrain_solar", params );
    REQUIRE( run.ok );
    CHECK( run.json["daylightSamples"].asUInt64() == 2 );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "shadow.tif" ) ) );
    std::vector<float> frac( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, frac.data(), kW, kH ) );
    // Sun due east (azimuth 90°): the shadow falls WEST of the wall.
    CHECK( frac[4] == Catch::Approx( 0.0f ) );
    CHECK( frac[1] == Catch::Approx( 1.0f ) );

    // Generated track (equinox at the equator) + hillshade series.
    Json::Value series( Json::objectValue );
    series["input"] = demPath.toStdString();
    series["output"] = dir.filePath( "series.tif" ).toStdString();
    series["product"] = "hillshade_series";
    series["day_of_year"] = 81;
    series["latitude"] = 0.0;
    series["start_hour"] = 6.0;
    series["end_hour"] = 18.0;
    series["step_hours"] = 6.0;
    const RunResult runS = runOperator( "rs:terrain_solar", series );
    REQUIRE( runS.ok );
    CHECK( runS.json["trackSamples"].asUInt64() == 3 );
    GdalDatasetWrapper seriesDs;
    REQUIRE( seriesDs.open( dir.filePath( "series.tif" ) ) );
    CHECK( seriesDs.bandCount() == 3 );
}

TEST_CASE( "rs:terrain_landform products E2E", "[terrain][landform][operator][e2e]" )
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
            b.withPixel( 1, x, y, 42.0f );
    const QString demPath = b.writeToDisk( dir.filePath( "flat.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    // geomorphon: a plateau is flat everywhere (class 0).
    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "geom.tif" ).toStdString();
    params["product"] = "geomorphon";
    const RunResult run = runOperator( "rs:terrain_landform", params );
    REQUIRE( run.ok );
    CHECK( run.json["formHistogram"]["0"].asUInt64() == kW * kH );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "geom.tif" ) ) );
    std::vector<float> forms( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, forms.data(), kW, kH ) );
    for ( const float f : forms )
        REQUIRE( f == 0.0f );

    // tpi_multiscale: plane plateau → TPI 0 at every scale, one band per radius.
    Json::Value tpi( Json::objectValue );
    tpi["input"] = demPath.toStdString();
    tpi["output"] = dir.filePath( "tpi.tif" ).toStdString();
    tpi["product"] = "tpi_multiscale";
    tpi["radii"] = "2,5";
    const RunResult runT = runOperator( "rs:terrain_landform", tpi );
    REQUIRE( runT.ok );
    GdalDatasetWrapper tpiDs;
    REQUIRE( tpiDs.open( dir.filePath( "tpi.tif" ) ) );
    CHECK( tpiDs.bandCount() == 2 );
    std::vector<float> tpiBand( static_cast<size_t>( kW ) * kH );
    REQUIRE( tpiDs.readBandData( 2, tpiBand.data(), kW, kH ) );
    for ( const float v : tpiBand )
        CHECK( std::fabs( v ) < 1e-3 );

    // landform_class: plateau → plains everywhere.
    Json::Value lc( Json::objectValue );
    lc["input"] = demPath.toStdString();
    lc["output"] = dir.filePath( "classes.tif" ).toStdString();
    lc["product"] = "landform_class";
    const RunResult runL = runOperator( "rs:terrain_landform", lc );
    REQUIRE( runL.ok );
    CHECK( runL.json["classHistogram"]["0"].asUInt64() == kW * kH );
}

TEST_CASE( "terrain operators surface Cancelled and leave no partial output",
           "[terrain][cancel][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kW = 64;
    constexpr int kH = 64;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, static_cast<float>( x + y ) );
    const QString demPath = b.writeToDisk( dir.filePath( "dem.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:terrain_viewshed" );
    REQUIRE( op != nullptr );

    RSOperatorContext context;
    std::atomic<bool> cancelled{ true };
    context.setCancelFlag( &cancelled );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "cancelled.tif" ).toStdString();
    params["product"] = "viewshed";
    params["observer"] = "32,32";
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
    INFO( "A pre-cancelled run must not leave an output file" );
    REQUIRE_FALSE( QFile::exists( dir.filePath( "cancelled.tif" ) ) );
}

TEST_CASE( "terrain cell-budget guard refuses oversized grids fail-closed",
           "[terrain][guard]" )
{
    // Default budget 2^28 cells.
    CHECK_FALSE( sicnu::operators::rs::terrainExceedsCellBudget( 16384, 16384 ) );
    CHECK( sicnu::operators::rs::terrainExceedsCellBudget( 16385, 16384 ) );
    CHECK_FALSE( sicnu::operators::rs::terrainExceedsCellBudget( 0, 0 ) );
}
