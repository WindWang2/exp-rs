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

#include <cpl_vsi.h>
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

/// RAII helper to ensure VSI files are removed even when assertions throw.
struct VsiGuard
{
    explicit VsiGuard( std::string p ) : path( std::move( p ) ) {}
    ~VsiGuard() { if ( !path.empty() ) VSIUnlink( path.c_str() ); }
    std::string path;
};

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

TEST_CASE( "VectorInspectTool supports /vsi virtual paths and connection strings", "[agent][spatial_tools]" )
{
    ensureGdalDrivers();
    const std::string memPath = "/vsimem/test_vector.geojson";
    createTestVector( QString::fromStdString( memPath ) );

    sicnu::agent::spatial_tools::VectorInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = memPath;
    input["max_features"] = 1;

    const auto result = tool.execute( input );
    REQUIRE( result.success );
    CHECK( result.output["layerCount"].asInt() == 1 );
    CHECK( result.output["driver"].asString() == "GeoJSON" );

    VSIUnlink( memPath.c_str() );
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
    const auto failRes = tool.execute( byName );
    CHECK_FALSE( failRes.success );
    CHECK( failRes.errorCode == "MODEL_NOT_FOUND" );
    CHECK( failRes.errorCategory == "validation" );
}

TEST_CASE( "ModelCatalog multi-criteria ranking and compatibility evaluation", "[agent][spatial][models]" )
{
    QTemporaryDir dir;
    QDir( dir.path() ).mkpath( "seg-s2" );
    QFile m1( dir.filePath( "seg-s2/model.json" ) );
    REQUIRE( m1.open( QIODevice::WriteOnly ) );
    m1.write( R"({
        "name": "seg-s2",
        "task": "segmentation",
        "input": "raster",
        "output": "raster",
        "accuracy": 0.92,
        "domain": {
            "sensors": ["Sentinel-2"],
            "resolution_range": [10.0, 20.0]
        },
        "runtime": {
            "gpu": true,
            "estimated_vram_mb": 1024,
            "cpu_fallback": true
        }
    })" );
    m1.close();

    QDir( dir.path() ).mkpath( "det-wv" );
    QFile m2( dir.filePath( "det-wv/model.json" ) );
    REQUIRE( m2.open( QIODevice::WriteOnly ) );
    m2.write( R"({
        "name": "det-wv",
        "task": "detection",
        "input": "raster",
        "output": "vector",
        "accuracy": 0.85,
        "domain": {
            "sensors": ["WorldView-3"],
            "resolution_range": [0.3, 2.0]
        },
        "runtime": {
            "gpu": false,
            "estimated_vram_mb": 0,
            "cpu_fallback": true
        }
    })" );
    m2.close();

    auto &catalog = sicnu::operators::ModelCatalog::instance();
    catalog.setDirectory( dir.path().toStdString() );

    sicnu::operators::ModelQueryCriteria criteria;
    criteria.task = "segmentation";
    criteria.sensor = "Sentinel-2";
    criteria.resolutionMeters = 10.0;
    criteria.gpuAvailable = true;
    criteria.maxVramMb = 2048;

    const auto ranked = catalog.rankModels( criteria );
    REQUIRE( ranked.size() == 2 );
    CHECK( ranked[0].model.name == "seg-s2" );
    CHECK( ranked[0].compatible );
    CHECK( ranked[0].score > 0.8 );
    CHECK_FALSE( ranked[1].compatible ); // task mismatch

    sicnu::agent::spatial_tools::ModelCatalogTool tool;
    Json::Value query( Json::objectValue );
    query["task"] = "segmentation";
    query["sensor"] = "Sentinel-2";
    query["gpu_available"] = true;
    const auto res = tool.execute( query );
    REQUIRE( res.success );
    REQUIRE( res.output["candidates"].isArray() );
    CHECK( res.output["candidates"].size() == 2 );
    CHECK( res.output["candidates"][0]["model"]["name"].asString() == "seg-s2" );
}

TEST_CASE( "SpatialToolResult supports structured error codes and categorization", "[agent][spatial]" )
{
    using namespace sicnu::agent::spatial_tools;
    SpatialToolResult r = SpatialToolResult::failure( "File missing", "FILE_NOT_FOUND", "io", false );
    CHECK_FALSE( r.success );
    CHECK( r.error == "File missing" );
    CHECK( r.errorCode == "FILE_NOT_FOUND" );
    CHECK( r.errorCategory == "io" );
    CHECK_FALSE( r.retryable );

    Json::Value json = r.toJson();
    CHECK_FALSE( json["success"].asBool() );
    CHECK( json["error"]["code"].asString() == "FILE_NOT_FOUND" );
    CHECK( json["error"]["category"].asString() == "io" );
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

// ——— Unified resolver regression tests (GH #560) ————————————————

TEST_CASE( "RasterInspectTool accepts /vsimem raster (GH #560 regression)", "[agent][spatial][raster][vsimem]" )
{
    const std::string vsiPath = "/vsimem/test_raster_inspect.tif";
    // Create a small GTiff directly in /vsimem (mirrors createTestRaster)
    ensureGdalDrivers();
    {
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDataset *ds = driver->Create( vsiPath.c_str(), 8, 8, 2, GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        double gt[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
        ds->SetGeoTransform( gt );
        OGRSpatialReference srs;
        srs.importFromEPSG( 32650 );
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        ds->SetProjection( wkt );
        CPLFree( wkt );
        GDALRasterBand *nir = ds->GetRasterBand( 1 );
        nir->SetNoDataValue( -9999.0 );
        nir->SetMetadataItem( "SICNU_BAND_ROLE", "NIR", nullptr );
        float row[8];
        for ( int y = 0; y < 8; ++y )
        {
            for ( int x = 0; x < 8; ++x )
                row[x] = static_cast<float>( y * 8 + x );
            nir->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
        }
        GDALClose( ds );
    }
    VsiGuard guard( vsiPath );

    sicnu::agent::spatial_tools::RasterInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = vsiPath;
    input["stats"] = true;
    const auto result = tool.execute( input );
    REQUIRE( result.success );
    CHECK( result.output["driver"].asString() == "GTiff" );
    CHECK( result.output["size"]["width"].asInt() == 8 );
}

TEST_CASE( "VectorInspectTool accepts /vsimem vector (issue #560 regression)", "[agent][spatial][vector][vsimem]" )
{
    const std::string vsiPath = "/vsimem/test_vector_inspect.geojson";
    ensureGdalDrivers();
    {
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GeoJSON" );
        REQUIRE( driver != nullptr );
        GDALDataset *ds = driver->Create( vsiPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr );
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
    }
    VsiGuard guard( vsiPath );

    sicnu::agent::spatial_tools::VectorInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = vsiPath;
    input["max_features"] = 2;
    const auto result = tool.execute( input );
    REQUIRE( result.success );
    CHECK( result.output["layerCount"].asInt() == 1 );
}

TEST_CASE( "VectorInspectTool does not reject PG connection string at existence stage", "[agent][spatial][vector][resolver]" )
{
    sicnu::agent::spatial_tools::VectorInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = "PG:dbname=nonexistent_db_xyz";
    const auto result = tool.execute( input );
    // Must fail (no real DB), but NOT via the local-file existence gate
    CHECK_FALSE( result.success );
    CHECK( result.error.find( "not found" ) == std::string::npos );
    if ( !result.errorCode.empty() )
        CHECK( result.errorCode == "provider_open_failed" );
}

TEST_CASE( "Missing local raster still reports not found with local_file_not_found", "[agent][spatial][raster][resolver]" )
{
    sicnu::agent::spatial_tools::RasterInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = "/definitely/not/a/raster_for_resolver_test.tif";
    const auto result = tool.execute( input );
    CHECK_FALSE( result.success );
    CHECK( result.error.find( "not found" ) != std::string::npos );
    if ( !result.errorCode.empty() )
        CHECK( result.errorCode == "local_file_not_found" );
}

TEST_CASE( "Missing local vector still reports not found with local_file_not_found", "[agent][spatial][vector][resolver]" )
{
    sicnu::agent::spatial_tools::VectorInspectTool tool;
    Json::Value input( Json::objectValue );
    input["path"] = "/definitely/not/a/vector_for_resolver_test.geojson";
    const auto result = tool.execute( input );
    CHECK_FALSE( result.success );
    CHECK( result.error.find( "not found" ) != std::string::npos );
    if ( !result.errorCode.empty() )
        CHECK( result.errorCode == "local_file_not_found" );
}
