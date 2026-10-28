// test_r25_a_change_area_table.cpp — R25-A FEAT-3.
//
// rs:post_classification_change class-area table: pixel counts are
// grid-relative, so the result now reports per-class areas in km2 (from/to
// dates) plus the changed area, with the cell-area basis named
// (projected_grid vs the geographic scene-centre approximation).
//
// Oracle: a 3x2 grid of 100 m cells (cell area = 0.01 km2) with
//   before = [0 1 1 / 1 1 1], after = [1 1 0 / 1 0 1]
//   fromTotals = [1, 5], toTotals = [2, 4]
//   changed = 3 pixels = 0.03 km2; unchanged = 3 = 0.03 km2.

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

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

QString writeClassMap( const QTemporaryDir &dir, const char *name,
                       const std::vector<float> &classes )
{
    RsSyntheticRasterBuilder b( 3, 2, 1, GDT_Byte );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 500000.0, 100.0, 2000000.0, -100.0 ); // 100 m cells
    for ( int y = 0; y < 2; ++y )
        for ( int x = 0; x < 3; ++x )
            b.withPixel( 1, x, y, classes[static_cast<size_t>( y ) * 3 + x] );
    return b.writeToDisk( dir.filePath( name ) );
}

} // namespace

TEST_CASE( "rs:post_classification_change reports class areas in km2",
           "[operators][change][r25a][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString before = writeClassMap( dir, "before.tif",
                                           { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } );
    const QString after = writeClassMap( dir, "after.tif",
                                         { 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f } );
    REQUIRE( !before.isEmpty() );
    REQUIRE( !after.isEmpty() );

    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = dir.filePath( "change.tif" ).toStdString();
    const RunResult run = runOperator( "rs:post_classification_change", params );
    REQUIRE( run.ok );

    // Counts (pinned prior behaviour): from [1,5], to [2,4], changed 3.
    CHECK( run.json["classCount"].asInt() == 2 );
    CHECK( run.json["fromTotals"][0].asUInt64() == 1 );
    CHECK( run.json["fromTotals"][1].asUInt64() == 5 );
    CHECK( run.json["toTotals"][0].asUInt64() == 2 );
    CHECK( run.json["toTotals"][1].asUInt64() == 4 );
    CHECK( run.json["changedPixels"].asUInt64() == 3 );

    // Areas (new): 100 m cells → 0.01 km2 per pixel.
    CHECK( run.json["cellAreaM2"].asDouble() == Catch::Approx( 10000.0 ).epsilon( 1e-9 ) );
    CHECK( run.json["cellAreaBasis"].asString() == "projected_grid" );
    CHECK( run.json["fromAreasKm2"][0].asDouble() == Catch::Approx( 0.01 ).epsilon( 1e-9 ) );
    CHECK( run.json["fromAreasKm2"][1].asDouble() == Catch::Approx( 0.05 ).epsilon( 1e-9 ) );
    CHECK( run.json["toAreasKm2"][0].asDouble() == Catch::Approx( 0.02 ).epsilon( 1e-9 ) );
    CHECK( run.json["toAreasKm2"][1].asDouble() == Catch::Approx( 0.04 ).epsilon( 1e-9 ) );
    CHECK( run.json["changedAreaKm2"].asDouble() == Catch::Approx( 0.03 ).epsilon( 1e-9 ) );
}

TEST_CASE( "rs:post_classification_change names the geographic area basis",
           "[operators][change][r25a][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // EPSG:4326, 0.001° cells at the equator (~111 m × 111 m; 1° ≈ 111.32
    // km, so 0.001° ≈ 111 m — cell area ≈ 1.24e4 m²). The exact value uses
    // the WGS84 arc at the scene-centre latitude (0°); the BASIS string is
    // the contract under test, plus a sanity bound (R1-5).
    RsSyntheticRasterBuilder b( 3, 2, 1, GDT_Byte );
    b.withCrs( "EPSG:4326" );
    b.withGeoTransform( 20.0, 0.001, 0.0005, -0.001 );
    for ( int y = 0; y < 2; ++y )
        for ( int x = 0; x < 3; ++x )
            b.withPixel( 1, x, y, 1.0f );
    const QString flat = b.writeToDisk( dir.filePath( "flat.tif" ) );
    REQUIRE( !flat.isEmpty() );

    Json::Value params( Json::objectValue );
    params["before"] = flat.toStdString();
    params["after"] = flat.toStdString();
    params["output"] = dir.filePath( "none.tif" ).toStdString();
    const RunResult run = runOperator( "rs:post_classification_change", params );
    REQUIRE( run.ok );
    CHECK( run.json["cellAreaBasis"].asString() == "geographic_scene_centre" );
    CHECK( run.json["changedAreaKm2"].asDouble() == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    // 0.001° ≈ 111.3 m per side at the equator → ~1.24e4 m² per cell.
    CHECK( run.json["cellAreaM2"].asDouble() > 1.0e4 );
    CHECK( run.json["cellAreaM2"].asDouble() < 1.5e4 );
    CHECK( run.json["toAreasKm2"][1].asDouble()
           == Catch::Approx( 6.0 * run.json["cellAreaM2"].asDouble() / 1.0e6 ).epsilon( 1e-9 ) );
}
