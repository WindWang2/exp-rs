// test_r24_a_sar_incidence.cpp — R24-A science track (WP-A3).
//
// RED provenance (origin/master@5fe642ec6): rs:sar_terrain_flatten and
// rs:sar_terrain_correction read incidenceDeg with a silent 30° default and
// never range-check it, while their siblings rs:sar_backscatter
// (rejects >= 90, tests/test_sar_operators.cpp:1379) and
// rs:sar_terrain_masks (rejects outside (0,90)) validate the same physical
// domain. incidenceDeg = 95 computes sin(95°) > 0 and publishes a
// physically impossible RTC; incidenceDeg = 0 nulls the denominator and
// silently turns the whole output into NoData "successfully".
//
// Expected: typed RSOperatorError for values outside (0, 90), matching the
// sibling contract.

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <array>
#include <vector>

using namespace sicnu::operators;

namespace
{

/// 2×2 linear-power sigma0 scene (values positive and finite) on a UTM
/// grid — the minimum the flatten/correction contracts accept.
bool writeSarScene( const QString &path, const QString &demPath )
{
    ensureGdalInit();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );
    const std::array<double, 6> gt = { 500000, 10, 0, 4500000, 0, -10 };

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    for ( const QString target : { path, demPath } )
    {
        GDALDatasetH ds = GDALCreate( driver, target.toUtf8().constData(), 2, 2, 1,
                                      GDT_Float32, nullptr );
        if ( !ds )
            return false;
        GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) );
        GDALSetProjection( ds, wkt.toUtf8().constData() );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        GDALSetRasterNoDataValue( band, -9999.0 );
        const bool sar = ( target == path );
        const std::vector<float> values = sar ? std::vector<float>( 4, 0.25f )
                                              : std::vector<float>{ 40.0f, 41.0f, 39.0f, 42.0f };
        const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 2,
                                      const_cast<float *>( values.data() ), 2, 2,
                                      GDT_Float32, 0, 0 ) == CE_None;
        GDALClose( ds );
        if ( !ok )
            return false;
    }
    return true;
}

} // namespace

TEST_CASE( "rs:sar_terrain_flatten rejects non-physical incidence angles",
           "[operators][sar][incidence][r24_a]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    REQUIRE( writeSarScene( dir.filePath( "sigma0.tif" ), dir.filePath( "dem.tif" ) ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_flatten" );
    REQUIRE( op != nullptr );

    // Control: a physically valid angle runs the same fixture to success —
    // proves any throw below comes from the angle gate, not the fixture.
    {
        Json::Value params( Json::objectValue );
        params["input"] = dir.filePath( "sigma0.tif" ).toStdString();
        params["dem"] = dir.filePath( "dem.tif" ).toStdString();
        params["output"] = dir.filePath( "ctrl.tif" ).toStdString();
        params["incidenceDeg"] = 35.0;
        RSOperatorContext context;
        REQUIRE_NOTHROW( op->run( params, context ) );
        CHECK( QFile::exists( dir.filePath( "ctrl.tif" ) ) );
    }

    for ( const double badTheta : { 0.0, -30.0, 90.0, 95.0, 700.0 } )
    {
        Json::Value params( Json::objectValue );
        params["input"] = dir.filePath( "sigma0.tif" ).toStdString();
        params["dem"] = dir.filePath( "dem.tif" ).toStdString();
        params["output"] = dir.filePath( "out.tif" ).toStdString();
        params["incidenceDeg"] = badTheta;
        RSOperatorContext context;
        INFO( "theta0=" << badTheta );
        REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
    }
    // No half-written output from the refused runs.
    CHECK( !QFile::exists( dir.filePath( "out.tif" ) ) );
}

TEST_CASE( "rs:sar_terrain_correction rejects non-physical incidence angles",
           "[operators][sar][incidence][r24_a]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    REQUIRE( writeSarScene( dir.filePath( "sigma0.tif" ), dir.filePath( "dem.tif" ) ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_correction" );
    REQUIRE( op != nullptr );

    // Control (same rationale as the flatten case above).
    {
        Json::Value params( Json::objectValue );
        params["input"] = dir.filePath( "sigma0.tif" ).toStdString();
        params["dem"] = dir.filePath( "dem.tif" ).toStdString();
        params["output"] = dir.filePath( "ctrl.tif" ).toStdString();
        params["incidenceDeg"] = 35.0;
        RSOperatorContext context;
        REQUIRE_NOTHROW( op->run( params, context ) );
        CHECK( QFile::exists( dir.filePath( "ctrl.tif" ) ) );
    }

    for ( const double badTheta : { 0.0, -30.0, 90.0, 95.0, 700.0 } )
    {
        Json::Value params( Json::objectValue );
        params["input"] = dir.filePath( "sigma0.tif" ).toStdString();
        params["dem"] = dir.filePath( "dem.tif" ).toStdString();
        params["output"] = dir.filePath( "out.tif" ).toStdString();
        params["incidenceDeg"] = badTheta;
        RSOperatorContext context;
        INFO( "theta0=" << badTheta );
        REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
    }
    CHECK( !QFile::exists( dir.filePath( "out.tif" ) ) );
}
