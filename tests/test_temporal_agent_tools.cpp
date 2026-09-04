// tests/test_temporal_agent_tools.cpp — Processing registration of the
// temporal operators (goal §31/§32) and the temporal:* agent tools
// (goal §34/§35).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <array>
#include <string>

#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"
#include "agent/tool_catalog/agent_tool.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "data/data_manager.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::agent::spatial_tools;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_agent_tools";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

const std::array<const char *, 6> kTemporalOperatorIds = {
    "rs:temporal_summary",   "rs:temporal_composite", "rs:temporal_index_series",
    "rs:temporal_trend",     "rs:temporal_anomaly",   "rs:temporal_extract_series",
};

bool writeTinyScene( const QString &path, const QString &date, float value )
{
    ensureGdalInit();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    double gt[6] = {500000, 30, 0, 4500000, 0, -30};
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", date.toUtf8().constData(), nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999 );
    GDALSetMetadataItem( band, "SICNU_BAND_ROLE", "red", nullptr );
    const float values[4] = {value, value, value, value};
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 2, const_cast<float *>( values ), 2, 2,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

} // namespace

TEST_CASE( "Temporal operators register in the operator registry", "[temporal][registry]" )
{
    ensureApp();
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    for ( const char *id : kTemporalOperatorIds )
    {
        INFO( "operator: " << id );
        REQUIRE( registry.hasOperator( id ) );
        auto op = registry.create( id );
        REQUIRE( op != nullptr );
        REQUIRE( op->group() == "temporal" );
        // streaming memory policy declared, never FullRaster
        const auto policy = op->memoryPolicy();
        REQUIRE( ( policy == sicnu::operators::RSOperatorMemoryPolicy::Streaming ||
                   policy == sicnu::operators::RSOperatorMemoryPolicy::MultiPassStreaming ) );
        const Json::Value schema = op->schema();
        REQUIRE( schema.isObject() );
        REQUIRE( schema.isMember( "properties" ) );
        REQUIRE( schema["properties"].isMember( "output" ) );
        REQUIRE( schema.isMember( "required" ) );
        // scientific contract metadata present
        const Json::Value meta = op->metadata();
        REQUIRE( meta.isObject() );
        REQUIRE( meta.isMember( "purpose" ) );
        REQUIRE( meta.isMember( "memoryPolicy" ) );
        REQUIRE( meta["deterministic"].asBool() );
        REQUIRE( meta["supportsCancellation"].asBool() );
        REQUIRE( meta["largeRasterSafe"].asBool() );
    }
}

TEST_CASE( "Temporal operators mirror into AtomicAlgorithmRegistry (ADR 0120)",
           "[temporal][registry][atomic]" )
{
    ensureApp();
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    registry.reset();
    registry.initialize();
    for ( const char *id : kTemporalOperatorIds )
    {
        INFO( "atomic adapter: " << id );
        auto adapter = registry.findAdapter( id );
        REQUIRE( adapter != nullptr );
        const auto &desc = adapter->descriptor();
        REQUIRE( desc.id == id );
        REQUIRE( desc.group == "temporal" );
        REQUIRE( !desc.inputs.empty() );
        REQUIRE( !desc.outputs.empty() );
        REQUIRE( desc.agentMetadata.largeRasterSafe );
        REQUIRE( desc.agentMetadata.supportsCancellation );
        // schema export round-trips (agent tool definitions)
        const Json::Value inputSchema = desc.toInputSchema();
        REQUIRE( inputSchema.isObject() );
        REQUIRE( inputSchema.isMember( "properties" ) );
    }
}

TEST_CASE( "temporal:* tools register and stay compact (§34/§35)", "[temporal][agent][tools]" )
{
    ensureApp();
    auto &registry = SpatialToolRegistry::instance();
    registry.reset();
    registry.registerBuiltinTools();

    CHECK( registry.find( "temporal:create_collection" ).has_value() );
    CHECK( registry.find( "temporal:describe_collection" ).has_value() );
    CHECK( registry.find( "temporal:list_scenes" ).has_value() );
    CHECK( registry.find( "temporal:preflight_collection" ).has_value() );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString a = dir.filePath( QStringLiteral( "a.tif" ) );
    const QString b = dir.filePath( QStringLiteral( "b.tif" ) );
    REQUIRE( writeTinyScene( a, QStringLiteral( "2025-01-01" ), 1.0f ) );
    REQUIRE( writeTinyScene( b, QStringLiteral( "2025-02-01" ), 2.0f ) );

    // create
    {
        auto tool = registry.find( "temporal:create_collection" );
        REQUIRE( tool.has_value() );
        Json::Value input( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        scenes.append( a.toStdString() );
        scenes.append( b.toStdString() );
        input["scenes"] = scenes;
        input["output"] = dir.filePath( QStringLiteral( "col.json" ) ).toStdString();
        const auto result = ( *tool )->execute( input );
        REQUIRE( result.success );
        REQUIRE( result.output["scene_count"].asInt() == 2 );
        REQUIRE( result.output["missing_times"].asInt() == 0 );
        REQUIRE( QFile::exists( dir.filePath( QStringLiteral( "col.json" ) ) ) );
    }
    // describe: compact by default — no per-scene dump
    {
        auto tool = registry.find( "temporal:describe_collection" );
        REQUIRE( tool.has_value() );
        Json::Value input( Json::objectValue );
        input["collection"] = dir.filePath( QStringLiteral( "col.json" ) ).toStdString();
        const auto result = ( *tool )->execute( input );
        REQUIRE( result.success );
        REQUIRE( result.output["scene_count"].asInt() == 2 );
        REQUIRE( result.output["grid_compatible"].asBool() );
        REQUIRE( result.output["time_range"].isArray() );
        REQUIRE( result.output["time_range"].size() == 2 );
        REQUIRE( !result.output.isMember( "scenes" ) ); // compact: scenes stay in list_scenes
        REQUIRE( !result.output.isMember( "preflight" ) );
    }
    // list_scenes paged
    {
        auto tool = registry.find( "temporal:list_scenes" );
        REQUIRE( tool.has_value() );
        Json::Value input( Json::objectValue );
        input["collection"] = dir.filePath( QStringLiteral( "col.json" ) ).toStdString();
        input["limit"] = 1;
        const auto result = ( *tool )->execute( input );
        REQUIRE( result.success );
        REQUIRE( result.output["total"].asInt() == 2 );
        REQUIRE( result.output["scenes"].size() == 1 );
        REQUIRE( result.output["scenes"][0]["time"].asString() == "2025-01-01" );
        REQUIRE( result.output["next_offset"].asInt() == 1 );
    }
    // preflight passes on a clean collection
    {
        auto tool = registry.find( "temporal:preflight_collection" );
        REQUIRE( tool.has_value() );
        Json::Value input( Json::objectValue );
        input["collection"] = dir.filePath( QStringLiteral( "col.json" ) ).toStdString();
        const auto result = ( *tool )->execute( input );
        REQUIRE( result.success );
        REQUIRE( result.output["ok"].asBool() );
    }
    // preflight fails closed on a broken collection (grid mismatch)
    {
        const QString c = dir.filePath( QStringLiteral( "c.tif" ) );
        REQUIRE( writeTinyScene( c, QStringLiteral( "2025-03-01" ), 3.0f ) );
        // shift origin by 10 m (sub-pixel)
        GDALDatasetH ds = GDALOpen( c.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        double gt[6] = {500010, 30, 0, 4500000, 0, -30};
        GDALSetGeoTransform( ds, gt );
        GDALClose( ds );

        auto tool = registry.find( "temporal:preflight_collection" );
        Json::Value input( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        scenes.append( a.toStdString() );
        scenes.append( c.toStdString() );
        input["scenes"] = scenes;
        const auto result = ( *tool )->execute( input );
        REQUIRE( !result.success );
        REQUIRE( result.errorCode == "TEMPORAL_PREFLIGHT_FAILED" );
        REQUIRE( result.output.isMember( "issues" ) );
    }
}

TEST_CASE( "temporal tools surface in the agent tool catalog", "[temporal][agent][catalog]" )
{
    ensureApp();
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.reset();
    catalog.initializeDefaults();
    const auto listTool = catalog.findTool( "temporal:describe_collection" );
    REQUIRE( listTool.has_value() );
    REQUIRE( listTool->category == sicnu::agent::tool_catalog::ToolCategory::Data );
    const auto schema = catalog.getSchema( "temporal:preflight_collection" );
    REQUIRE( !schema.empty() );
}

TEST_CASE( "temporal:register_collection preserves rich scene semantics and roundtrips (#729)",
           "[temporal][agent][semantics]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString scene1 = dir.filePath( QStringLiteral( "s1.tif" ) );
    const QString scene2 = dir.filePath( QStringLiteral( "s2.tif" ) );
    REQUIRE( writeTinyScene( scene1, QStringLiteral( "2025-01-01T00:00:00" ), 10.0f ) );
    REQUIRE( writeTinyScene( scene2, QStringLiteral( "2025-02-01T00:00:00" ), 20.0f ) );

    sicnu::data::DataManager dm;
    sicnu::temporal::setWorkspaceCatalog( &dm );

    auto &spatialRegistry = SpatialToolRegistry::instance();
    spatialRegistry.registerBuiltinTools();

    auto regTool = spatialRegistry.find( "temporal:register_collection" );
    REQUIRE( regTool.has_value() );

    // Build scene definitions with rich metadata: bands, mask_band, quality_band, modality, sensor, etc.
    Json::Value input( Json::objectValue );
    input["name"] = "rich_temporal_col";
    Json::Value scenes( Json::arrayValue );

    Json::Value sc1( Json::objectValue );
    sc1["path"] = scene1.toStdString();
    sc1["time"] = "2025-01-01T00:00:00";
    Json::Value bands1( Json::objectValue );
    bands1["red"] = 1;
    bands1["nir"] = 2;
    sc1["bands"] = bands1;
    sc1["mask_band"] = 3;
    sc1["quality_band"] = 4;
    sc1["modality"] = "optical";
    sc1["sensor"] = "MSI";
    sc1["radiometric_state"] = "toa_reflectance";
    sc1["resolution_m"] = 10.0;
    sc1["cloud_cover_percent"] = 5.5;
    scenes.append( sc1 );

    Json::Value sc2( Json::objectValue );
    sc2["path"] = scene2.toStdString();
    sc2["time"] = "2025-02-01T00:00:00";
    Json::Value bands2( Json::objectValue );
    bands2["red"] = 1;
    bands2["nir"] = 2;
    sc2["bands"] = bands2;
    sc2["mask_band"] = 3;
    sc2["quality_band"] = 4;
    sc2["modality"] = "optical";
    sc2["sensor"] = "MSI";
    sc2["radiometric_state"] = "toa_reflectance";
    sc2["resolution_m"] = 10.0;
    sc2["cloud_cover_percent"] = 1.2;
    scenes.append( sc2 );

    input["scenes"] = scenes;

    const auto result = ( *regTool )->execute( input );
    REQUIRE( result.success );
    const std::string collectionIdStr = result.output["collection_id"].asString();
    REQUIRE_FALSE( collectionIdStr.empty() );

    // Verify preservation in DataManager
    const auto optColId = sicnu::data::CollectionId::fromString( QString::fromStdString( collectionIdStr ) );
    REQUIRE( optColId.has_value() );
    const auto optRec = dm.temporalCollection( *optColId );
    REQUIRE( optRec.has_value() );

    sicnu::temporal::TemporalCollection savedCol;
    QString parseErr;
    REQUIRE( sicnu::temporal::collectionFromDescriptorText( optRec->descriptor, &savedCol, &parseErr ) );
    REQUIRE( savedCol.scenes().size() == 2 );

    const auto &savedSc1 = savedCol.scenes().at( 0 );
    CHECK( savedSc1.path == scene1 );
    CHECK( savedSc1.bandOverrides.at( QStringLiteral( "red" ) ) == 1 );
    CHECK( savedSc1.bandOverrides.at( QStringLiteral( "nir" ) ) == 2 );
    CHECK( savedSc1.maskBand == 3 );
    CHECK( savedSc1.qualityBand == 4 );
    CHECK( savedSc1.modality == QStringLiteral( "optical" ) );
    CHECK( savedSc1.sensor == QStringLiteral( "MSI" ) );
    CHECK( savedSc1.radiometricState == QStringLiteral( "toa_reflectance" ) );
    CHECK( savedSc1.resolutionMeters == Catch::Approx( 10.0 ) );
    CHECK( savedSc1.cloudCoverPercent == Catch::Approx( 5.5 ) );

    // Parity check with TemporalCollection::fromInlineScenes
    sicnu::temporal::TemporalCollection parsedCol;
    QString parseInlineErr;
    REQUIRE( sicnu::temporal::TemporalCollection::fromInlineScenes( input["scenes"], &parsedCol, &parseInlineErr ) );
    REQUIRE( parsedCol.scenes().size() == 2 );
    const auto &directSc1 = parsedCol.scenes().at( 0 );
    CHECK( directSc1.path == savedSc1.path );
    CHECK( directSc1.bandOverrides == savedSc1.bandOverrides );
    CHECK( directSc1.maskBand == savedSc1.maskBand );
    CHECK( directSc1.qualityBand == savedSc1.qualityBand );
    CHECK( directSc1.modality == savedSc1.modality );

    // Descriptor save & reload round-trip preservation
    const QString descPath = dir.filePath( QStringLiteral( "col_desc.json" ) );
    REQUIRE( savedCol.save( descPath ) );

    sicnu::temporal::TemporalCollection reloadedCol;
    QString loadErr;
    REQUIRE( sicnu::temporal::TemporalCollection::load( descPath, &reloadedCol, &loadErr ) );
    REQUIRE( reloadedCol.scenes().size() == 2 );
    const auto &reloadedSc1 = reloadedCol.scenes().at( 0 );
    CHECK( reloadedSc1.path == scene1 );
    CHECK( reloadedSc1.bandOverrides.at( QStringLiteral( "red" ) ) == 1 );
    CHECK( reloadedSc1.bandOverrides.at( QStringLiteral( "nir" ) ) == 2 );
    CHECK( reloadedSc1.maskBand == 3 );
    CHECK( reloadedSc1.qualityBand == 4 );
    CHECK( reloadedSc1.modality == QStringLiteral( "optical" ) );
    CHECK( reloadedSc1.sensor == QStringLiteral( "MSI" ) );
    CHECK( reloadedSc1.cloudCoverPercent == Catch::Approx( 5.5 ) );

    // Negative test cases for register_collection input validation
    {
        // 1. Empty scenes array
        Json::Value badInput( Json::objectValue );
        badInput["scenes"] = Json::Value( Json::arrayValue );
        const auto resEmpty = ( *regTool )->execute( badInput );
        CHECK_FALSE( resEmpty.success );
        CHECK( resEmpty.errorCode == "INVALID_PARAMETER" );

        // 2. Scene missing path
        Json::Value badScene( Json::objectValue );
        badScene["time"] = "2025-01-01T00:00:00";
        Json::Value badScenes( Json::arrayValue );
        badScenes.append( badScene );
        badInput["scenes"] = badScenes;
        const auto resNoPath = ( *regTool )->execute( badInput );
        CHECK_FALSE( resNoPath.success );
        CHECK( resNoPath.errorCode == "INVALID_PARAMETER" );

        // 3. Non-numeric band override
        Json::Value badBandScene( Json::objectValue );
        badBandScene["path"] = scene1.toStdString();
        Json::Value badBands( Json::objectValue );
        badBands["red"] = "not-a-number";
        badBandScene["bands"] = badBands;
        Json::Value badBandsScenes( Json::arrayValue );
        badBandsScenes.append( badBandScene );
        badInput["scenes"] = badBandsScenes;
        const auto resBadBands = ( *regTool )->execute( badInput );
        CHECK_FALSE( resBadBands.success );
        CHECK( resBadBands.errorCode == "INVALID_PARAMETER" );

        // 4. Non-existent local scene file
        Json::Value missingScene( Json::objectValue );
        missingScene["path"] = "/nonexistent/path/scene.tif";
        Json::Value missingScenes( Json::arrayValue );
        missingScenes.append( missingScene );
        badInput["scenes"] = missingScenes;
        const auto resMissing = ( *regTool )->execute( badInput );
        CHECK_FALSE( resMissing.success );
        CHECK( resMissing.errorCode == "INVALID_PARAMETER" );
    }

    sicnu::temporal::setWorkspaceCatalog( nullptr );
}

TEST_CASE( "Agent tool discovery finds temporal and SAR change tools via capability facets (#725)",
           "[agent][search][facets]" )
{
    ensureApp();
    sicnu::operators::rs::installRsOperatorProvider();
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    registry.reset();
    registry.initialize();
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.reset();
    catalog.initializeDefaults();

    // 1. Search for optical temporal tools
    {
        sicnu::agent::tool_catalog::SearchQuery q;
        q.temporal = true;
        const auto results = catalog.searchTools( q );
        REQUIRE_FALSE( results.empty() );
        bool foundTemporalSummary = false;
        for ( const auto &t : results )
        {
            if ( t.name == "rs:temporal_summary" || t.name == "temporal:describe_collection" )
                foundTemporalSummary = true;
        }
        CHECK( foundTemporalSummary );
    }

    // 2. Search for SAR change detection tools via capability facets
    {
        sicnu::agent::tool_catalog::SearchQuery q;
        q.taskFamily = "change-detection";
        q.modalities = { "sar" };
        q.temporal = true;
        q.largeRasterSafeOnly = true;
        const auto results = catalog.searchTools( q );
        REQUIRE_FALSE( results.empty() );
        bool foundSarChange = false;
        for ( const auto &t : results )
        {
            if ( t.name == "rs:change_log_ratio" )
                foundSarChange = true;
        }
        CHECK( foundSarChange );
    }
}

TEST_CASE( "parseCollection prefers workspace collection over inline scenes",
           "[temporal][collection]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString scene1 = dir.filePath( QStringLiteral( "s1.tif" ) );
    const QString scene2 = dir.filePath( QStringLiteral( "s2.tif" ) );
    const QString decoy = dir.filePath( QStringLiteral( "decoy.tif" ) );
    REQUIRE( writeTinyScene( scene1, QStringLiteral( "2025-01-01T00:00:00" ), 1.0f ) );
    REQUIRE( writeTinyScene( scene2, QStringLiteral( "2025-02-01T00:00:00" ), 2.0f ) );
    REQUIRE( writeTinyScene( decoy, QStringLiteral( "2024-01-01T00:00:00" ), 9.0f ) );

    sicnu::data::DataManager dm;
    sicnu::temporal::setWorkspaceCatalog( &dm );

    sicnu::temporal::TemporalCollection col;
    sicnu::temporal::TemporalSceneRef ref1;
    ref1.path = scene1;
    ref1.time = sicnu::temporal::parseAcquisitionTime( QStringLiteral( "2025-01-01T00:00:00" ) );
    ref1.timeSource = QStringLiteral( "explicit" );
    ref1.originalIndex = 0;
    sicnu::temporal::TemporalSceneRef ref2;
    ref2.path = scene2;
    ref2.time = sicnu::temporal::parseAcquisitionTime( QStringLiteral( "2025-02-01T00:00:00" ) );
    ref2.timeSource = QStringLiteral( "explicit" );
    ref2.originalIndex = 1;
    col.scenes() = { ref1, ref2 };

    QString saveErr;
    const sicnu::data::CollectionId colId =
        sicnu::temporal::saveCollectionToWorkspace( dm, QStringLiteral( "AuthoritativeCol" ), col, {}, &saveErr );
    REQUIRE( !colId.isNull() );
    REQUIRE( saveErr.isEmpty() );

    Json::Value params( Json::objectValue );
    params["collection"] = colId.toString().toStdString();
    Json::Value scenes( Json::arrayValue );
    scenes.append( decoy.toStdString() );
    params["scenes"] = scenes;

    const auto parsed = sicnu::operators::rs::temporal_input::parseCollection( params );
    REQUIRE( parsed.sceneCount() == 2 );
    CHECK( parsed.scenes().at( 0 ).path == scene1 );
    CHECK( parsed.scenes().at( 1 ).path == scene2 );

    sicnu::temporal::setWorkspaceCatalog( nullptr );
}
