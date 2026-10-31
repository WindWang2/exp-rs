// test_r25_a_terrain_solar_latitude.cpp — R25-A DEF-1: rs:terrain_solar must
// not silently default the solar-geometry latitude to 40°N when the caller
// omitted it. The latitude is derivable from the raster CRS (geographic:
// centre row; projected: centre transformed to WGS84), and a raster with no
// CRS must fail closed instead of computing shadows for the wrong place on
// Earth. Discriminator: generated-track daylight sample counts differ
// sharply between 40°N and the fixture latitudes on the solstice day 355.
//
// Oracle (hand-derived, Spencer declination + hour-angle formula):
//   day 355, samples start 6..18 step 1h:
//     lat -33° (Sydney summer): sun up 04:54..19:06 → 13 daylight samples
//     lat  40°N (winter):       sun up 07:25..16:35 →  9 daylight samples
//   day 355, samples 7..17 step 0.5h:
//     lat 45.13°N (UTM 50N northing 5,000,500): up 07:43..16:17 → 17 samples
//     lat 40°N:                                  up 07:25..16:35 → 19 samples

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_cell_geometry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal.h>

#include <json/json.h>

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

// Flat 11x1 DEM at the given latitude, expressed as a geographic CRS raster.
QString writeGeographicDem( const QTemporaryDir &dir, double centreLat )
{
    constexpr int kW = 11;
    constexpr int kH = 1;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:4326" );
    b.withGeoTransform( 150.0, 0.00027778, centreLat + 0.5 * 0.00027778, -0.00027778 );
    for ( int x = 0; x < kW; ++x )
        b.withPixel( 1, x, 0, 10.0f );
    return b.writeToDisk( dir.filePath( "dem_geo.tif" ) );
}

// Flat 11x1001 DEM in UTM 50N, 30 m cells, top northing 5,000,505. The
// SIGNED affine centre is northing 5,000,505 - 30*500.5 = 4,985,490 ≈
// 45.00°N; an abs()-walked "centre" lands at ≈ 5,002,510 ≈ 45.26°N — the
// 0.26° gap is what the latitude assertion pins (R1-2: a 1-row fixture was
// blind to the sign).
QString writeProjectedDem( const QTemporaryDir &dir )
{
    constexpr int kW = 11;
    constexpr int kH = 1001;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 449850.0, 30.0, 5000505.0, -30.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, 10.0f );
    return b.writeToDisk( dir.filePath( "dem_utm.tif" ) );
}

} // namespace

TEST_CASE( "rs:terrain_solar derives latitude from a geographic CRS",
           "[terrain][solar][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString demPath = writeGeographicDem( dir, -33.0 );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["day_of_year"] = 355;
    params["start_hour"] = 6.0;
    params["end_hour"] = 18.0;
    params["step_hours"] = 1.0;
    // NOTE: no "latitude" — must come from the raster CRS (−33°), not 40°N.
    const RunResult run = runOperator( "rs:terrain_solar", params );
    REQUIRE( run.ok );
    CHECK( run.json["daylightSamples"].asUInt64() == 13 ); // RED on master: 9
    CHECK( run.json["latitudeSource"].asString() == "raster_crs" );
    CHECK( run.json["latitude"].asDouble() == Catch::Approx( -33.0 ).epsilon( 0.001 ) );
}

TEST_CASE( "rs:terrain_solar derives latitude from a projected CRS via WGS84",
           "[terrain][solar][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString demPath = writeProjectedDem( dir );
    REQUIRE( !demPath.isEmpty() );
    // Diagnose the derivation: what does the written raster actually declare?
    {
        GdalDatasetWrapper probe;
        REQUIRE( probe.open( demPath ) );
        const QString wkt = probe.projection();
        INFO( "wkt empty=" << wkt.isEmpty() << " len=" << wkt.length() );
        INFO( "wkt: " << wkt.left( 200 ).toStdString() );
        const std::optional<double> lat =
            sicnu::processing::gdal_util::sceneLatitudeDeg( probe );
        INFO( "sceneLatitudeDeg has_value=" << ( lat.has_value() ? "yes" : "no" )
              << ( lat ? ( *lat ) : 0.0 ) );
        REQUIRE( lat.has_value() );
    }

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["day_of_year"] = 355;
    params["start_hour"] = 7.0;
    params["end_hour"] = 17.0;
    params["step_hours"] = 0.5;
    const RunResult run = runOperator( "rs:terrain_solar", params );
    REQUIRE( run.ok );
    CHECK( run.json["daylightSamples"].asUInt64() == 17 ); // RED on master: 19
    CHECK( run.json["latitudeSource"].asString() == "raster_crs" );
    // Signed-centre truth ≈ 45.00°N (the abs() bug reported ≈ 45.26°).
    CHECK( run.json["latitude"].asDouble() == Catch::Approx( 45.00 ).epsilon( 0.02 ) );
}

TEST_CASE( "rs:terrain_solar fails closed without latitude and without CRS",
           "[terrain][solar][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Write the raster with raw GDAL: RsSyntheticRasterBuilder leaves the
    // GTiff driver's default WGS84 geographic CRS in place when withCrs is
    // never called, so it cannot express a genuinely CRS-less DEM.
    const QString demPath = dir.filePath( "dem_nocrs.tif" );
    {
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH ds = GDALCreate( driver, demPath.toUtf8().constData(), 5, 1, 1,
                                      GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        const double gt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
        GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        REQUIRE( band != nullptr );
        const float values[5] = { 10.0f, 10.0f, 10.0f, 10.0f, 10.0f };
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 5, 1,
                               const_cast<float *>( values ), 5, 1, GDT_Float32, 0, 0 )
                 == CE_None );
        GDALClose( ds );
    }
    {
        GdalDatasetWrapper probe;
        REQUIRE( probe.open( demPath ) );
        REQUIRE( probe.projection().isEmpty() ); // genuinely CRS-less
    }

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["day_of_year"] = 355;
    const RunResult run = runOperator( "rs:terrain_solar", params );
    CHECK_FALSE( run.ok ); // RED on master: silently succeeds at 40°N
    CHECK( run.error.code() == ErrorCode::InvalidInputData );
}

TEST_CASE( "rs:terrain_solar explicit latitude parameter still wins",
           "[terrain][solar][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString demPath = writeGeographicDem( dir, -33.0 );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["day_of_year"] = 355;
    params["start_hour"] = 6.0;
    params["end_hour"] = 18.0;
    params["step_hours"] = 1.0;
    params["latitude"] = 40.0;
    const RunResult run = runOperator( "rs:terrain_solar", params );
    REQUIRE( run.ok );
    CHECK( run.json["daylightSamples"].asUInt64() == 9 ); // 40°N winter day
    CHECK( run.json["latitudeSource"].asString() == "parameter" );
}

TEST_CASE( "rs:terrain_solar rejects an out-of-range derived latitude",
           "[terrain][solar][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Geographic CRS whose centre row is beyond ±90° is malformed input;
    // clamping it to the poles would fabricate polar night/day.
    constexpr int kW = 5;
    constexpr int kH = 1;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:4326" );
    b.withGeoTransform( 150.0, 0.00027778, 95.0, -0.00027778 );
    for ( int x = 0; x < kW; ++x )
        b.withPixel( 1, x, 0, 10.0f );
    const QString demPath = b.writeToDisk( dir.filePath( "dem_badlat.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "shadow.tif" ).toStdString();
    params["product"] = "shadow_duration";
    params["day_of_year"] = 355;
    const RunResult run = runOperator( "rs:terrain_solar", params );
    CHECK_FALSE( run.ok );
    CHECK( run.error.code() == ErrorCode::InvalidInputData );
}
