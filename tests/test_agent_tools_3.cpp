// tests/test_agent_tools_3.cpp
// Spatial Scientist 3.0 — tool-level tests: capability ranking, workflow
// preflight, symbology apply/rollback, workspace command stack.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/commands/workspace_commands.h"
#include "agent/spatial_tools/temporal_workspace_tools.h"
#include "agent/contracts/spatial_contracts.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/tool_catalog/surface_registry.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfields.h>
#include <qgsfield.h>
#include <qgsgeometry.h>

#include <QCoreApplication>
#include <QDir>

#include "operators/framework/model_catalog.h"

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using namespace sicnu::agent::spatial_tools;

namespace {

SpatialToolResult callTool( const std::string &name, const Json::Value &input )
{
  const auto tool = SpatialToolRegistry::instance().find( name );
  if ( !tool )
    return SpatialToolResult::failure( "tool not registered: " + name, "NOT_FOUND", "validation" );
  return ( *tool )->execute( input );
}

QgsVectorLayer *makeClassifiedLayer( const QString &name )
{
  auto *layer = new QgsVectorLayer( QStringLiteral( "Polygon?crs=EPSG:4326" ), name,
                                    QStringLiteral( "memory" ) );
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "class" ), QMetaType::Type::QString ) );
  fields.append( QgsField( QStringLiteral( "area" ), QMetaType::Type::Double ) );
  layer->dataProvider()->addAttributes( fields.toList() );
  layer->updateFields();

  const char *classes[] = { "water", "forest", "urban", "farm" };
  for ( int i = 0; i < 16; ++i )
  {
    QgsFeature feature( layer->fields() );
    feature.setAttribute( QStringLiteral( "class" ), QString( classes[i % 4] ) );
    feature.setAttribute( QStringLiteral( "area" ), 10.0 * ( i + 1 ) );
    const QString wkt = QStringLiteral( "Polygon((%1 0, %1 1, %2 1, %2 0, %1 0))" )
                          .arg( i )
                          .arg( i + 1 );
    feature.setGeometry( QgsGeometry::fromWkt( wkt ) );
    layer->dataProvider()->addFeature( feature );
  }
  layer->updateExtents();
  return layer;
}

} // namespace

TEST_CASE( "Spatial Scientist 3.0 tool families are registered", "[agent3][registry]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  for ( const char *tool : { "spatial:workspace_summary", "spatial:layer_summary",
                             "spatial:sample_pixels", "spatial:sample_features",
                             "spatial:compare_rasters", "spatial:assess_result",
                             "spatial:search_capabilities", "spatial:select_model",
                             "workflow:preflight", "cartography:compose",
                             "cartography:preflight", "cartography:repair",
                             "cartography:list_components", "cartography:get_component",
                             "cartography:list_templates", "cartography:instantiate_template",
                             "cartography:chart_create", "cartography:chart_get",
                             "cartography:chart_list", "cartography:chart_delete",
                             "symbology:describe", "symbology:apply_categorical",
                             "symbology:apply_graduated", "symbology:apply_raster_ramp",
                             "workspace:undo", "workspace:redo", "workspace:history" } )
  {
    INFO( "tool: " << tool );
    CHECK( SpatialToolRegistry::instance().find( tool ).has_value() );
  }

  // Catalog surfaces the new groups through the spatial provider.
  auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
  catalog.initializeDefaults();
  CHECK( catalog.findTool( "cartography:compose" ).has_value() );
  CHECK( catalog.findTool( "symbology:apply_categorical" ).has_value() );
  CHECK( catalog.findTool( "workflow:preflight" ).has_value() );
  CHECK( catalog.findTool( "workspace:undo" ).has_value() );
}

TEST_CASE( "Capability ranking returns bounded candidates with reasons", "[agent3][capability]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  const auto result = callTool( "spatial:search_capabilities", [] {
    Json::Value input( Json::objectValue );
    input["query"] = "compute NDVI spectral index";
    input["limit"] = 5;
    return input;
  }() );
  REQUIRE( result.success );
  CHECK( result.output["candidates"].isArray() );
  CHECK( result.output["candidates"].size() <= 5 );
  CHECK( result.output["total"].asInt() >= result.output["candidates"].size() );
  CHECK( result.output["context"].isObject() );

  // Every candidate conforms to the CapabilityCandidate contract.
  for ( const auto &candidate : result.output["candidates"] )
  {
    const auto problems = sicnu::agent::contracts::validateCapabilityCandidate( candidate );
    INFO( "candidate problems: " << problems.size() );
    CHECK( problems.empty() );
  }

  // A nonsense query still executes but may rank anything; a missing query
  // with no facets is a validation error.
  const auto empty = callTool( "spatial:search_capabilities", Json::Value( Json::objectValue ) );
  CHECK_FALSE( empty.success );
  CHECK( empty.errorCode == "INVALID_PARAMETER" );
}

TEST_CASE( "Model selection surfaces ranked candidates", "[agent3][models]" )
{
  // catch_discover_tests runs every CASE in its own process: registration
  // from other cases never happened here, so register before calling.
  SpatialToolRegistry::instance().registerBuiltinTools();
#ifdef CMAKE_SOURCE_DIR
  {
      const QDir sourceModels(
          QDir( QString::fromUtf8( CMAKE_SOURCE_DIR ) ).filePath( QStringLiteral( "models" ) ) );
      if ( sourceModels.exists() )
          sicnu::operators::ModelCatalog::instance().setDirectory(
              sourceModels.absolutePath().toStdString() );
  }
#endif
  const auto result = callTool( "spatial:select_model", [] {
    Json::Value input( Json::objectValue );
    input["task"] = "segmentation";
    input["gpu_available"] = false;
    return input;
  }() );
  REQUIRE( result.success );
  CHECK( result.output["candidates"].isArray() );
  for ( const auto &candidate : result.output["candidates"] )
  {
    CHECK( candidate["kind"].asString() == "model" );
    CHECK( candidate.isMember( "readiness" ) );
  }
}

TEST_CASE( "Workflow preflight reports machine-fixable issues", "[agent3][workflow]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();

  // Unknown operator → blocked with WF_UNKNOWN_OPERATOR.
  Json::Value broken( Json::objectValue );
  Json::Value steps( Json::arrayValue );
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operatorId"] = "rs:does_not_exist";
  steps.append( step );
  broken["id"] = "broken-wf";
  broken["steps"] = steps;
  auto result = callTool( "workflow:preflight", [ broken ] {
    Json::Value input( Json::objectValue );
    input["workflow"] = broken;
    return input;
  }() );
  REQUIRE( result.success );
  CHECK( result.output["verdict"].asString() == "blocked" );
  bool hasUnknownOp = false;
  for ( const auto &issue : result.output["issues"] )
    hasUnknownOp = hasUnknownOp || issue["code"].asString() == "WF_UNKNOWN_OPERATOR";
  CHECK( hasUnknownOp );

  // Schema-invalid workflow → WF_SCHEMA_INVALID.
  Json::Value garbage( Json::objectValue );
  garbage["id"] = "x";
  garbage["steps"] = "not-an-array";
  auto invalid = callTool( "workflow:preflight", [ garbage ] {
    Json::Value input( Json::objectValue );
    input["workflow"] = garbage;
    return input;
  }() );
  REQUIRE( invalid.success );
  CHECK( invalid.output["verdict"].asString() == "blocked" );
  CHECK( invalid.output["issues"][0]["code"].asString() == "WF_SCHEMA_INVALID" );

  // Registered operator with a valid (if partial) definition → no unknown-op.
  Json::Value good( Json::objectValue );
  Json::Value goodSteps( Json::arrayValue );
  Json::Value goodStep( Json::objectValue );
  goodStep["id"] = "ndvi";
  goodStep["operatorId"] = "rs:ndvi";
  goodSteps.append( goodStep );
  good["id"] = "good-wf";
  good["steps"] = goodSteps;
  auto ok = callTool( "workflow:preflight", [ good ] {
    Json::Value input( Json::objectValue );
    input["workflow"] = good;
    return input;
  }() );
  REQUIRE( ok.success );
  bool hasUnknownOperator = false;
  for ( const auto &issue : ok.output["issues"] )
    hasUnknownOperator = hasUnknownOperator || issue["code"].asString() == "WF_UNKNOWN_OPERATOR";
  CHECK_FALSE( hasUnknownOperator );
}

TEST_CASE( "Workspace command stack undo/redo roundtrip", "[agent3][commands]" )
{
  auto &stack = sicnu::agent::commands::WorkspaceCommandStack::instance();
  stack.clear();

  int value = 0;
  const QString txn = stack.beginTransaction( QStringLiteral( "increment" ) );
  REQUIRE( stack.addCommand(
    txn, sicnu::agent::commands::WorkspaceCommand{ QStringLiteral( "inc" ),
                                                   [ &value ] {
                                                     value += 1;
                                                     return true;
                                                   },
                                                   [ &value ] {
                                                     value -= 1;
                                                     return true;
                                                   } } ) );
  REQUIRE( stack.commit( txn ) );
  CHECK( value == 1 );
  CHECK( stack.undo() );
  CHECK( value == 0 );
  CHECK( stack.redo() );
  CHECK( value == 1 );

  // Failed redo rolls the transaction back.
  const QString failing = stack.beginTransaction( QStringLiteral( "boom" ) );
  REQUIRE( stack.addCommand(
    failing, sicnu::agent::commands::WorkspaceCommand{ QStringLiteral( "boom" ), [] { return false; },
                                                       [] { return true; } } ) );
  QString error;
  CHECK_FALSE( stack.commit( failing, &error ) );
  CHECK_FALSE( error.isEmpty() );
  CHECK( stack.history( 10 ).size() == 1 );
}

TEST_CASE( "Symbology tools apply and roll back renderers", "[agent3][symbology]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  QgsProject::instance()->clear();
  auto *layer = makeClassifiedLayer( QStringLiteral( "bench-landuse" ) );
  QgsProject::instance()->addMapLayer( layer );

  // describe → default renderer
  auto described = callTool( "symbology:describe", [] {
    Json::Value input( Json::objectValue );
    input["target"] = "bench-landuse";
    return input;
  }() );
  REQUIRE( described.success );
  CHECK( described.output["renderer"]["type"].asString() == "vector" );

  // apply_categorical → categories populated, undoable
  auto applied = callTool( "symbology:apply_categorical", [] {
    Json::Value input( Json::objectValue );
    input["target"] = "bench-landuse";
    input["field"] = "class";
    return input;
  }() );
  REQUIRE( applied.success );
  CHECK( applied.output["categories"].asInt() == 4 );
  CHECK( applied.output["undoable"].asBool() );

  // apply_graduated on the numeric field
  auto graduated = callTool( "symbology:apply_graduated", [] {
    Json::Value input( Json::objectValue );
    input["target"] = "bench-landuse";
    input["field"] = "area";
    input["classes"] = 4;
    return input;
  }() );
  REQUIRE( graduated.success );
  CHECK( graduated.output["classes"].asInt() == 4 );

  // Undo restores the previous (categorized) renderer.
  CHECK( sicnu::agent::commands::WorkspaceCommandStack::instance().undo() );
  auto afterUndo = callTool( "symbology:describe", [] {
    Json::Value input( Json::objectValue );
    input["target"] = "bench-landuse";
    return input;
  }() );
  REQUIRE( afterUndo.success );
  CHECK( afterUndo.output["renderer"]["renderer"].asString() == "categorizedSymbol" );

  QgsProject::instance()->removeAllMapLayers();
}

TEST_CASE( "#1155: temporal:ingest_stac refuses a deeply-nested depth bomb cleanly",
           "[d16][agent][robustness]" )
{
    // The 'result' file path is workspace content: a hostile document nested
    // past jsoncpp's useful recursion bound must be a typed failure, never a
    // stack overflow / escaping exception (the pre-#1155 default reader).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QString bomb;
    for ( int i = 0; i < 1000; ++i )
        bomb += QLatin1Char( '[' );
    bomb += QStringLiteral( "1" );
    for ( int i = 0; i < 1000; ++i )
        bomb += QLatin1Char( ']' );
    const QString bombPath = QDir( dir.path() ).filePath( QStringLiteral( "bomb.json" ) );
    {
        QFile f( bombPath );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( bomb.toUtf8() );
        f.close();
    }

    sicnu::agent::spatial_tools::TemporalIngestStacTool tool;
    Json::Value input( Json::objectValue );
    input["result"] = bombPath.toStdString();
    input["name"] = "bombed";
    const sicnu::agent::spatial_tools::SpatialToolResult result = tool.execute( input );
    REQUIRE_FALSE( result.success );
    REQUIRE( result.error.find( "invalid STAC result JSON" ) != std::string::npos );

    // The same tool still ingests a well-formed document afterwards (two
    // minimal Sentinel-2-style features — a collection needs >= 2 scenes —
    // register=false keeps it catalog-free).
    const QString goodPath = QDir( dir.path() ).filePath( QStringLiteral( "good.json" ) );
    {
        QFile f( goodPath );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( QByteArrayLiteral( R"({"features":[)"
                                   R"({"id":"i1","type":"Feature","properties":{"datetime":"2026-08-01T10:00:00Z","platform":"Sentinel-2B","eo:cloud_cover":12.5},"assets":{"green":{"href":"https://example.com/i1_B03.tif","type":"image/tiff; application=geotiff"}}},)"
                                   R"({"id":"i2","type":"Feature","properties":{"datetime":"2026-08-11T10:00:00Z","platform":"Sentinel-2B","eo:cloud_cover":30.0},"assets":{"green":{"href":"https://example.com/i2_B03.tif","type":"image/tiff; application=geotiff"}}}]})" ) );
        f.close();
    }
    Json::Value okInput( Json::objectValue );
    okInput["result"] = goodPath.toStdString();
    okInput["name"] = "post-bomb-series";
    okInput["register"] = false;
    const sicnu::agent::spatial_tools::SpatialToolResult ok = tool.execute( okInput );
    REQUIRE( ok.success );
    REQUIRE( ok.output["scene_count"].asInt() == 2 );
}

// ── RS14-15 Explainable Workflow: explain:step surface ────────────────────
// The tool object is exercised by the narrow explain lane
// (test_explain_agent_tool compiles the TU directly); what needs the full
// agent link set is the REGISTRY and CATALOG surface: the tool must be
// registered under its single namespace and listed by the spatial tool
// provider (no ghost surface), and the MCP allow-prefix table must accept
// the family so tools/call can route it.
TEST_CASE( "explain:step registers into the spatial tool registry (RS14-15)",
           "[d16][agent][explain]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    const auto tool = SpatialToolRegistry::instance().find( "explain:step" );
    REQUIRE( tool.has_value() );
    CHECK( tool.value()->name() == "explain:step" );
}

TEST_CASE( "explain:step is listed by the spatial tool provider (RS14-15)",
           "[d16][agent][explain]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    sicnu::agent::spatial_tools::SpatialToolProvider provider;
    const auto tools = provider.provideTools();
    bool listed = false;
    for ( const auto &agentTool : tools )
        listed = listed || agentTool.name == "explain:step";
    CHECK( listed );
}

TEST_CASE( "explain: family is an allowed MCP surface prefix (RS14-15)",
           "[d16][agent][explain]" )
{
    const QStringList prefixes = surfaceAllowedPrefixes();
    CHECK( prefixes.contains( QStringLiteral( "explain:" ) ) );
}
