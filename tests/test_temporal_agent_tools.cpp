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
#include "agent/spatial_tools/spatial_tool_registry.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "operators/framework/rs_operator_registry.h"
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
        REQUIRE( op->memoryPolicy() == sicnu::operators::RSOperatorMemoryPolicy::Streaming ||
                 op->memoryPolicy() ==
                     sicnu::operators::RSOperatorMemoryPolicy::MultiPassStreaming );
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
    auto &catalog = sicnu::agent::AgentToolCatalog::instance();
    catalog.reset();
    catalog.initializeDefaults();
    const auto listTool = catalog.findTool( "temporal:describe_collection" );
    REQUIRE( listTool.has_value() );
    REQUIRE( listTool->category == sicnu::agent::ToolCategory::Data );
    const auto schema = catalog.getSchema( "temporal:preflight_collection" );
    REQUIRE( !schema.empty() );
}
