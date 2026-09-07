// tests/test_harness_grounding.cpp
//
// Harness 4.0 Phases 3/4: dataset grounding (EntityResolver + spatial:understand)
// and the revision-stamped typed spatial context (harness:context).
// Deterministic, headless, runtime-generated GDAL fixtures.

#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QTemporaryDir>

#include "agent/harness/entity_resolver.h"
#include "agent/harness/grounding_tools.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/tool_taxonomy.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <json/json.h>

#include <cmath>
#include <string>

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

namespace {

void ensureGdalDrivers()
{
    static const bool kRegistered = [] {
        GDALAllRegister();
        return true;
    }();
    ( void )kRegistered;
}

/// 8x8, 2-band Float32 GeoTIFF with NIR/RED band-role metadata (SAR/optical
/// grounding reads these roles, so this fixture classifies as optical).
std::string createTestRaster( const QString &path )
{
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );

    double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( geoTransform );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32650 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
    ds->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", "NIR", nullptr );
    ds->GetRasterBand( 1 )->SetMetadataItem( "WAVELENGTH", "842", nullptr );
    ds->GetRasterBand( 1 )->SetMetadataItem( "WAVELENGTH_UNITS", "nm", nullptr );
    ds->GetRasterBand( 2 )->SetMetadataItem( "SICNU_BAND_ROLE", "RED", nullptr );
    float row[8];
    for ( int y = 0; y < 8; ++y )
    {
        for ( int x = 0; x < 8; ++x )
            row[x] = static_cast<float>( y * 8 + x );
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path.toStdString();
}

} // namespace

TEST_CASE( "Modality inference is deterministic and role-driven", "[harness][grounding]" )
{
    Json::Value optical( Json::objectValue );
    Json::Value bands( Json::arrayValue );
    Json::Value nir( Json::objectValue );
    nir["role"] = "NIR";
    bands.append( nir );
    optical["bands"] = bands;
    CHECK( inferModality( optical ) == "optical" );

    Json::Value sar( Json::objectValue );
    Json::Value sarBands( Json::arrayValue );
    Json::Value hh( Json::objectValue );
    hh["role"] = "HH";
    sarBands.append( hh );
    sar["bands"] = sarBands;
    CHECK( inferModality( sar ) == "sar" );

    Json::Value dem( Json::objectValue );
    Json::Value demBands( Json::arrayValue );
    Json::Value elev( Json::objectValue );
    elev["role"] = "DEM";
    demBands.append( elev );
    dem["bands"] = demBands;
    CHECK( inferModality( dem ) == "dem" );

    CHECK( inferModality( Json::Value( Json::objectValue ) ) == "unknown" );
}

TEST_CASE( "Entity resolution never guesses", "[harness][grounding]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string rasterPath = createTestRaster( dir.filePath( "scene.tif" ) );

    SECTION( "empty reference is a typed failure" )
    {
        HarnessError error;
        CHECK( !resolveDatasetRef( QStringLiteral( "  " ), &error ).has_value() );
        CHECK( error.code == "INVALID_PARAMETER" );
    }

    SECTION( "path reference resolves and gains a stable entity id" )
    {
        HarnessError error;
        const auto resolved = resolveDatasetRef( QString::fromStdString( rasterPath ), &error );
        REQUIRE( resolved.has_value() );
        CHECK( resolved->resolution == "path" );
        CHECK( resolved->assetEntityId.startsWith( "asset-" ) );
        CHECK( QFile::exists( resolved->path ) );
    }

    SECTION( "missing path is DATASET_NOT_FOUND with the probed location" )
    {
        HarnessError error;
        CHECK( !resolveDatasetRef( dir.filePath( "ghost.tif" ), &error ).has_value() );
        CHECK( error.code == "DATASET_NOT_FOUND" );
        CHECK( error.details.isMember( "path" ) );
    }

    SECTION( "unknown entity id and unknown name are DATASET_NOT_FOUND" )
    {
        HarnessError error;
        CHECK( !resolveDatasetRef( QStringLiteral( "asset-9999" ), &error ).has_value() );
        CHECK( error.code == "DATASET_NOT_FOUND" );
    }

    SECTION( "entity id round-trips through the registry" )
    {
        HarnessError error;
        const auto byPath = resolveDatasetRef( QString::fromStdString( rasterPath ), &error );
        REQUIRE( byPath.has_value() );
        const auto byEntity =
          resolveDatasetRef( byPath->assetEntityId, &error );
        REQUIRE( byEntity.has_value() );
        CHECK( byEntity->resolution == "entity_id" );
        CHECK( byEntity->path == byPath->path );
        CHECK( byEntity->assetEntityId == byPath->assetEntityId );
    }
}

TEST_CASE( "spatial:understand returns a typed grounding document", "[harness][grounding]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    registerGroundingTools();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string rasterPath = createTestRaster( dir.filePath( "scene.tif" ) );

    auto tool = SpatialToolRegistry::instance().find( "spatial:understand" );
    REQUIRE( tool.has_value() );

    Json::Value input;
    input["asset"] = rasterPath;
    const SpatialToolResult result = ( *tool )->execute( input );
    REQUIRE( result.success );

    const Json::Value &doc = result.output["dataset_understanding"];
    CHECK( doc["schema_version"].asString() == "1.0" );
    CHECK( doc["kind"].asString() == "dataset_understanding" );
    CHECK( doc["source_kind"].asString() == "raster" );
    CHECK( doc["modality"].asString() == "optical" );
    CHECK( doc["band_count"].asInt() == 2 );
    CHECK( doc["band_roles"][0].asString() == "NIR" );
    CHECK( doc["crs"]["authid"].asString() == "EPSG:32650" );
    CHECK( doc["entity"].isMember( "asset_entity_id" ) );

    SECTION( "unknown references produce typed failures, not prose guesses" )
    {
        Json::Value bad;
        bad["asset"] = "asset-424242";
        const SpatialToolResult failure = ( *tool )->execute( bad );
        CHECK( !failure.success );
        CHECK( failure.errorCode == "DATASET_NOT_FOUND" );
    }

    SECTION( "taxonomy classifies grounding as data.inspect" )
    {
        CHECK( taxonomyForTool( "spatial:understand" ).toString() == "data.inspect" );
    }
}

TEST_CASE( "harness:context is revision-stamped and refresh-aware", "[harness][context]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    registerGroundingTools();
    auto tool = SpatialToolRegistry::instance().find( "harness:context" );
    REQUIRE( tool.has_value() );

    const SpatialToolResult first = ( *tool )->execute( Json::Value() );
    REQUIRE( first.success );
    const std::string revision = first.output["revision"].asString();
    CHECK( !revision.empty() );
    CHECK( !first.output["unchanged"].asBool() );
    CHECK( first.output["context"].isObject() );

    SECTION( "same revision short-circuits to a tiny response" )
    {
        Json::Value input;
        input["if_revision"] = revision;
        const SpatialToolResult second = ( *tool )->execute( input );
        REQUIRE( second.success );
        CHECK( second.output["unchanged"].asBool() );
        CHECK( !second.output.isMember( "context" ) );
    }

    SECTION( "a stale revision returns the full document" )
    {
        Json::Value input;
        input["if_revision"] = "0000000000000000000000000000000";
        const SpatialToolResult third = ( *tool )->execute( input );
        REQUIRE( third.success );
        CHECK( !third.output["unchanged"].asBool() );
        CHECK( third.output["context"].isObject() );
    }
}
