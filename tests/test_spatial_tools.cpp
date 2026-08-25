// tests/test_spatial_tools.cpp
// ADR 0122 — spatial tool framework tests: registry, raster/vector inspection
// tools (runtime-generated GDAL fixtures), model catalog, algorithm metadata
// store, and the catalog provider bridge.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include "agent/spatial_tools/model_catalog_tool.h"
#include "agent/spatial_tools/raster_inspect_tool.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"
#include "agent/spatial_tools/vector_inspect_tool.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "operators/framework/model_catalog.h"
#include "processing/framework/algorithm_meta_store.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>

#include <json/json.h>

#include <cmath>
#include <string>

namespace {

void ensureGdalDrivers()
{
    static const bool kRegistered = [] {
        GDALAllRegister();
        OGRRegisterAll();
        return true;
    }();
    ( void )kRegistered;
}

/// 8x8, 2-band Float32 GeoTIFF in UTM 50N with band role metadata.
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

    GDALRasterBand *nir = ds->GetRasterBand( 1 );
    nir->SetNoDataValue( -9999.0 );
    nir->SetMetadataItem( "SICNU_BAND_ROLE", "NIR", nullptr );
    nir->SetMetadataItem( "WAVELENGTH", "842", nullptr );
    nir->SetMetadataItem( "WAVELENGTH_UNITS", "nm", nullptr );
    float row[8];
    for ( int y = 0; y < 8; ++y )
    {
        for ( int x = 0; x < 8; ++x )
            row[x] = static_cast<float>( y * 8 + x );
        nir->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    }

    GDALRasterBand *red = ds->GetRasterBand( 2 );
    red->SetMetadataItem( "SICNU_BAND_ROLE", "RED", nullptr );

    GDALClose( ds );
    return path.toStdString();
}

/// 3-point GeoJSON feature collection with one string and one real field.
std::string createTestVector( const QString &path )
{
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GeoJSON" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( ds != nullptr );

    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    OGRLayer *layer = ds->CreateLayer( "points", &srs, wkbPoint, nullptr );
    REQUIRE( layer != nullptr );

    OGRFieldDefn nameField( "name", OFTString );
    layer->CreateField( &nameField );
    OGRFieldDefn valueField( "value", OFTReal );
    layer->CreateField( &valueField );

    for ( int i = 0; i < 3; ++i )
    {
        OGRFeature *feature = OGRFeature::CreateFeature( layer->GetLayerDefn() );
        feature->SetField( "name", ( std::string( "p" ) + std::to_string( i ) ).c_str() );
        feature->SetField( "value", 1.5 * ( i + 1 ) );
        OGRPoint point( 116.0 + 0.1 * i, 39.0 + 0.1 * i );
        feature->SetGeometry( &point );
        REQUIRE( layer->CreateFeature( feature ) == OGRERR_NONE );
        OGRFeature::DestroyFeature( feature );
    }

    GDALClose( ds );
    return path.toStdString();
}

} // namespace

TEST_CASE( "SpatialToolRegistry registers built-in tools", "[agent][spatial]" )
{
    auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
    registry.reset();

    CHECK( registry.size() >= 3 );
    CHECK( registry.find( "spatial:raster_inspect" ).has_value() );
    CHECK( registry.find( "spatial:vector_inspect" ).has_value() );
    CHECK( registry.find( "spatial:list_models" ).has_value() );
    CHECK_FALSE( registry.find( "spatial:nonexistent" ).has_value() );

    // Duplicate registration is rejected, existing tool stays authoritative.
    const auto duplicate = std::make_shared<sicnu::agent::spatial_tools::RasterInspectTool>();
    CHECK_FALSE( registry.registerTool( duplicate ) );
    CHECK( registry.size() >= 3 );
}

TEST_CASE( "SpatialTool schemas declare required inputs", "[agent][spatial]" )
{
    const sicnu::agent::spatial_tools::RasterInspectTool tool;
    const Json::Value schema = tool.inputSchema();
    REQUIRE( schema["required"].isArray() );
    CHECK( schema["required"][0].asString() == "path" );

    Json::Value input( Json::objectValue );
    const std::string error = sicnu::agent::spatial_tools::validateAgainstRequired( input, schema );
    CHECK( error == "Missing required parameter: path" );
    input["path"] = "/tmp/none.tif";
    CHECK( sicnu::agent::spatial_tools::validateAgainstRequired( input, schema ).empty() );
}

TEST_CASE( "RasterInspectTool inspects a generated GeoTIFF", "[agent][spatial][raster]" )
{
    QTemporaryDir dir;
    const std::string path = createTestRaster( dir.filePath( "test.tif" ) );

    sicnu::agent::spatial_tools::RasterInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = path;
    input["stats"] = true;

    const auto result = tool.execute( input );
    REQUIRE( result.success );

    CHECK( result.output["driver"].asString() == "GTiff" );
    CHECK( result.output["size"]["width"].asInt() == 8 );
    CHECK( result.output["size"]["height"].asInt() == 8 );
    CHECK( result.output["bandCount"].asInt() == 2 );
    CHECK( result.output["pixelSize"]["x"].asDouble() == Catch::Approx( 30.0 ) );
    CHECK( result.output["extent"]["minX"].asDouble() == Catch::Approx( 500000.0 ) );
    REQUIRE( result.output["crs"].isObject() );
    CHECK( result.output["crs"]["authid"].asString() == "EPSG:32650" );

    REQUIRE( result.output["bands"].isArray() );
    const Json::Value band1 = result.output["bands"][0];
    CHECK( band1["dataType"].asString() == "Float32" );
    CHECK( band1["role"].asString() == "NIR" );
    CHECK( band1["wavelength"].asString() == "842" );
    CHECK( band1["nodata"].asDouble() == Catch::Approx( -9999.0 ) );
    CHECK( band1["stats"]["min"].asDouble() == Catch::Approx( 0.0 ) );
    CHECK( band1["stats"]["max"].asDouble() == Catch::Approx( 63.0 ) );
    CHECK( result.output["bands"][1]["role"].asString() == "RED" );
}

TEST_CASE( "RasterInspectTool rejects missing files", "[agent][spatial][raster]" )
{
    sicnu::agent::spatial_tools::RasterInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = "/definitely/not/a/raster.tif";

    const auto result = tool.execute( input );
    CHECK_FALSE( result.success );
    CHECK( result.error.find( "not found" ) != std::string::npos );
}

TEST_CASE( "VectorInspectTool inspects a generated GeoJSON", "[agent][spatial][vector]" )
{
    QTemporaryDir dir;
    const std::string path = createTestVector( dir.filePath( "test.geojson" ) );

    sicnu::agent::spatial_tools::VectorInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = path;
    input["max_features"] = 2;

    const auto result = tool.execute( input );
    REQUIRE( result.success );

    CHECK( result.output["layerCount"].asInt() == 1 );
    REQUIRE( result.output["layers"].isArray() );

    const Json::Value layer = result.output["layers"][0];
    CHECK( layer["featureCount"].asInt64() == 3 );
    CHECK( layer["crs"].asString() == "EPSG:4326" );
    REQUIRE( layer["extent"].isObject() );
    CHECK( layer["extent"]["minX"].asDouble() == Catch::Approx( 116.0 ) );

    REQUIRE( layer["fields"].isArray() );
    CHECK( layer["fields"].size() == 2 );
    CHECK( layer["fields"][0]["name"].asString() == "name" );
    CHECK( layer["fields"][0]["type"].asString() == "string" );

    REQUIRE( layer["sampleFeatures"].isArray() );
    CHECK( layer["sampleFeatures"].size() == 2 );
    CHECK( layer["sampleFeatures"][0]["attributes"]["name"].asString() == "p0" );
    CHECK( layer["sampleFeatures"][0]["attributes"]["value"].asDouble() == Catch::Approx( 1.5 ) );
    CHECK( layer["sampleFeatures"][0]["geometryJson"].asString().find( "Point" )
               != std::string::npos );
}

TEST_CASE( "ModelCatalog scans manifests and the tool exposes them", "[agent][spatial][models]" )
{
    QTemporaryDir dir;
    QDir( dir.path() ).mkpath( "sam-building" );
    QFile manifest( dir.filePath( "sam-building/model.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( R"({
        "name": "sam-building",
        "task": "segmentation",
        "input": "raster",
        "output": "polygon",
        "framework": "onnx",
        "path": "",
        "gpu": true,
        "accuracy": 0.89,
        "description": "Building extraction template",
        "tags": ["buildings", "segmentation"]
    })" );
    manifest.close();

    auto &catalog = sicnu::operators::ModelCatalog::instance();
    catalog.setDirectory( dir.path().toStdString() );

    CHECK( catalog.models().size() == 1 );
    const auto model = catalog.find( "sam-building" );
    REQUIRE( model.has_value() );
    CHECK( model->task == "segmentation" );
    CHECK( model->gpu );
    CHECK( model->accuracy == Catch::Approx( 0.89 ) );
    CHECK( catalog.modelsByTask( "classification" ).empty() );

    sicnu::agent::spatial_tools::ModelCatalogTool tool;
    Json::Value input( Json::objectValue );
    const auto result = tool.execute( input );
    REQUIRE( result.success );
    REQUIRE( result.output["models"].isArray() );
    CHECK( result.output["models"].size() == 1 );
    CHECK( result.output["models"][0]["name"].asString() == "sam-building" );
    CHECK( result.output["models"][0]["task"].asString() == "segmentation" );

    Json::Value byName( Json::objectValue );
    byName["name"] = "missing-model";
    CHECK_FALSE( tool.execute( byName ).success );
}

TEST_CASE( "AlgorithmMetaStore loads sidecar manifests", "[agent][spatial][meta]" )
{
    QTemporaryDir dir;
    QFile sidecar( dir.filePath( "rs-inference.json" ) );
    REQUIRE( sidecar.open( QIODevice::WriteOnly ) );
    sidecar.write( R"({
        "id": "rs:inference",
        "task": "inference",
        "input": "raster",
        "output": "raster",
        "gpu": false,
        "notes": "ONNX tracer bullet; resolve models via spatial:list_models"
    })" );
    sidecar.close();

    auto &store = sicnu::processing::AlgorithmMetaStore::instance();
    const size_t loaded = store.loadFromDirectory( dir.path().toStdString() );
    CHECK( loaded == 1 );

    const auto entry = store.find( "rs:inference" );
    REQUIRE( entry.has_value() );
    CHECK( entry->task == "inference" );
    CHECK_FALSE( entry->gpu );
    CHECK( entry->accuracy < 0.0 );

    const Json::Value json = entry->toJson();
    CHECK( json["id"].asString() == "rs:inference" );
    CHECK_FALSE( json.isMember( "accuracy" ) );

    CHECK_FALSE( store.find( "rs:unknown" ).has_value() );
    CHECK( store.loadFromDirectory( "/definitely/not/a/dir" ) == 0 );
    CHECK( store.size() == 0 );
}

TEST_CASE( "SpatialToolProvider feeds the unified AgentToolCatalog", "[agent][spatial][catalog]" )
{
    using namespace sicnu::agent::tool_catalog;
    AgentToolCatalog::instance().reset();

    const auto tool = AgentToolCatalog::instance().findTool( "spatial:raster_inspect" );
    REQUIRE( tool.has_value() );
    CHECK( tool->category == ToolCategory::Data );
    CHECK( tool->group == "spatial" );
    CHECK( tool->inputSchema["required"][0].asString() == "path" );

    CHECK( AgentToolCatalog::instance().findTool( "spatial:list_models" ).has_value() );
}
