// test_r25_a_sar_geometry_fallback.cpp — R25-A FEAT-1.
//
// rs:sar_terrain_flatten / rs:sar_terrain_correction must honour the scene
// geometry declared in the INPUT's metadata (SICNU_SAR_INCIDENCE_DEG /
// SICNU_SAR_HEADING_DEG / SICNU_SAR_LOOK_AZIMUTH_DEG — the keys
// rs:sar_terrain_masks writes) when the caller passes no explicit angles.
// The old resolution jumped straight from "no parameter" to the 30°/0°
// defaults, so a masks→flatten pipeline silently re-derived geometry for a
// different acquisition (R24-A CROSS_TRACK candidate, confirmed live on
// master 2610cb4741).
//
// Resolution contract under test: parameter > input metadata > default
// (+ warning). Numeric oracle: on flat terrain the local incidence band of
// rs:sar_terrain_correction equals the resolved θ0 (foundation5 pin), so a
// metadata-declared 40° must produce 40° — master produces 30°.

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <array>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace
{

/// 4×4 linear-power sigma0 scene + flat DEM on a UTM grid (same contract as
/// test_r24_a_sar_incidence's fixture).
bool writeSarScene( const QString &sarPath, const QString &demPath )
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
    for ( const QString target : { sarPath, demPath } )
    {
        GDALDatasetH ds = GDALCreate( driver, target.toUtf8().constData(), 4, 4, 1,
                                      GDT_Float32, nullptr );
        if ( !ds )
            return false;
        GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) );
        GDALSetProjection( ds, wkt.toUtf8().constData() );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        GDALSetRasterNoDataValue( band, -9999.0 );
        const bool sar = ( target == sarPath );
        const std::vector<float> values = sar ? std::vector<float>( 16, 0.25f )
                                              : std::vector<float>( 16, 40.0f );
        const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 4, 4,
                                      const_cast<float *>( values.data() ), 4, 4,
                                      GDT_Float32, 0, 0 ) == CE_None;
        GDALClose( ds );
        if ( !ok )
            return false;
    }
    return true;
}

/// Stamps dataset-level scene-geometry metadata (the keys terrain_masks
/// writes onto its products).
void stampGeometry( const QString &path, const char *incidence, const char *heading,
                    const char *lookAzimuth )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    if ( incidence )
        GDALSetMetadataItem( ds, "SICNU_SAR_INCIDENCE_DEG", incidence, nullptr );
    if ( heading )
        GDALSetMetadataItem( ds, "SICNU_SAR_HEADING_DEG", heading, nullptr );
    if ( lookAzimuth )
        GDALSetMetadataItem( ds, "SICNU_SAR_LOOK_AZIMUTH_DEG", lookAzimuth, nullptr );
    GDALClose( ds );
}

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

TEST_CASE( "rs:sar_terrain_correction uses declared scene geometry",
           "[operators][sar][r25a][geometry][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString sar = dir.filePath( "sigma0.tif" );
    const QString dem = dir.filePath( "dem.tif" );
    REQUIRE( writeSarScene( sar, dem ) );
    stampGeometry( sar, "40.0", "90.0", nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = sar.toStdString();
    params["dem"] = dem.toStdString();
    params["output"] = dir.filePath( "corrected.tif" ).toStdString();
    // NO geometry parameters: the declared metadata must drive the run.
    const RunResult run = runOperator( "rs:sar_terrain_correction", params );
    REQUIRE( run.ok );
    CHECK( run.json["incidenceDeg"].asDouble() == Catch::Approx( 40.0 ) ); // RED: 30.0
    CHECK( run.json["incidenceSource"].asString() == "input_metadata" );
    CHECK( run.json["headingDeg"].asDouble() == Catch::Approx( 90.0 ) );
    CHECK( run.json["headingSource"].asString() == "input_metadata" );
    // Right-looking heading 90 → look azimuth 180.
    CHECK( run.json["lookAzimuthDeg"].asDouble() == Catch::Approx( 180.0 ) );
    CHECK( run.json["lookAzimuthSource"].asString() == "derived_from_heading" );

    // Numeric oracle through the production path: on flat terrain the local
    // incidence band equals the resolved θ0 (foundation5 pin) — 40, not the
    // 30° default master used.
    GdalDatasetWrapper out;
    REQUIRE( out.open( dir.filePath( "corrected.tif" ) ) );
    std::vector<float> incidence( 16 );
    const int incidenceBand = run.json["incidenceBand"].asInt();
    REQUIRE( incidenceBand > 0 );
    REQUIRE( out.readBandData( incidenceBand, incidence.data(), 4, 4 ) );
    for ( float v : incidence )
        CHECK( v == Catch::Approx( 40.0f ).epsilon( 1e-4 ) ); // RED: 30.0

    // The resolved geometry is re-declared on the output (chainability).
    GDALDatasetH outH = GDALOpen( dir.filePath( "corrected.tif" ).toUtf8().constData(),
                                  GA_ReadOnly );
    REQUIRE( outH != nullptr );
    const char *inc = GDALGetMetadataItem( outH, "SICNU_SAR_INCIDENCE_DEG", nullptr );
    REQUIRE( inc != nullptr );
    CHECK( std::string( inc ) == "40" );
    const char *head = GDALGetMetadataItem( outH, "SICNU_SAR_HEADING_DEG", nullptr );
    REQUIRE( head != nullptr );
    CHECK( std::string( head ) == "90" );
    GDALClose( outH );
}

TEST_CASE( "rs:sar_terrain_flatten resolves geometry parameter > metadata > default",
           "[operators][sar][r25a][geometry][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    SECTION( "declared look azimuth beats the heading derivation" )
    {
        const QString sar = dir.filePath( "sigma0_az.tif" );
        const QString dem = dir.filePath( "dem_az.tif" );
        REQUIRE( writeSarScene( sar, dem ) );
        stampGeometry( sar, "38.5", "10.0", "271.0" );
        Json::Value params( Json::objectValue );
        params["input"] = sar.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = dir.filePath( "flat_az.tif" ).toStdString();
        const RunResult run = runOperator( "rs:sar_terrain_flatten", params );
        REQUIRE( run.ok );
        CHECK( run.json["lookAzimuthDeg"].asDouble() == Catch::Approx( 271.0 ) ); // RED: 100
        CHECK( run.json["lookAzimuthSource"].asString() == "input_metadata" );
        CHECK( run.json["incidenceDeg"].asDouble() == Catch::Approx( 38.5 ) );
        CHECK( run.json["incidenceSource"].asString() == "input_metadata" );
    }

    SECTION( "an explicit parameter still beats the metadata" )
    {
        const QString sar = dir.filePath( "sigma0_p.tif" );
        const QString dem = dir.filePath( "dem_p.tif" );
        REQUIRE( writeSarScene( sar, dem ) );
        stampGeometry( sar, "40.0", "90.0", nullptr );
        Json::Value params( Json::objectValue );
        params["input"] = sar.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = dir.filePath( "flat_p.tif" ).toStdString();
        params["incidenceDeg"] = 25.0;
        const RunResult run = runOperator( "rs:sar_terrain_flatten", params );
        REQUIRE( run.ok );
        CHECK( run.json["incidenceDeg"].asDouble() == Catch::Approx( 25.0 ) );
        CHECK( run.json["incidenceSource"].asString() == "parameter" );
        // Heading still comes from the metadata (parameter absent).
        CHECK( run.json["headingDeg"].asDouble() == Catch::Approx( 90.0 ) );
    }

    SECTION( "garbage metadata fails closed with the source named" )
    {
        const QString sar = dir.filePath( "sigma0_bad.tif" );
        const QString dem = dir.filePath( "dem_bad.tif" );
        REQUIRE( writeSarScene( sar, dem ) );
        stampGeometry( sar, "120.0", nullptr, nullptr ); // non-physical
        Json::Value params( Json::objectValue );
        params["input"] = sar.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = dir.filePath( "flat_bad.tif" ).toStdString();
        const RunResult run = runOperator( "rs:sar_terrain_flatten", params );
        CHECK_FALSE( run.ok );
        CHECK( run.error.code() == ErrorCode::InvalidParameter );
        CHECK( run.error.message().find( "input_metadata" ) != std::string::npos );
        CHECK( !QFile::exists( dir.filePath( "flat_bad.tif" ) ) );
    }
}
