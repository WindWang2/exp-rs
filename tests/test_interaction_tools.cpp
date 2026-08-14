// tests/test_interaction_tools.cpp
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

#include <chrono>
#include <thread>
#include <memory>
#include <cmath>

#include "agent/interaction_tool_registry.h"
#include "agent/view_control_service.h"
#include "agent/mcp_server.h"
#include "processing/framework/tool_call_dispatcher.h"
#include "processing/framework/json_params_converter.h"

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrectangle.h>

using namespace sicnu::agent;
using namespace sicnu::processing;

// Helper to ensure QgsApplication / QApplication is initialized
struct TestAppFixture {
  TestAppFixture() {
    if ( !QCoreApplication::instance() ) {
      static int argc = 1;
      static char appName[] = "test_interaction_tools";
      static char *argv[] = { appName, nullptr };
      s_app = new QApplication( argc, argv );
      QgsApplication::initQgis();
    }
  }

  static QApplication *s_app;
};
QApplication *TestAppFixture::s_app = nullptr;

// Helper subclass of McpServer to expose handlers for testing
class TestInteractionMcpServer : public McpServer {
public:
  TestInteractionMcpServer() : McpServer() {}

  QVariantMap testListInteractionTools() { return handleListInteractionTools(); }
  QVariantMap testGetInteractionSchema( const QString &name ) { return handleGetInteractionSchema( name ); }
};

TEST_CASE( "InteractionToolRegistry discovery and schema export", "[agent][interaction][discovery]" )
{
  TestAppFixture fixture;
  auto &registry = InteractionToolRegistry::instance();
  registry.reset();

  ViewControlService service;
  registry.registerBuiltinTools( &service );

  SECTION( "builtin tools are registered" )
  {
    REQUIRE( registry.toolCount() >= 8 );
    REQUIRE( registry.hasTool( "view:get_state" ) );
    REQUIRE( registry.hasTool( "view:set_extent" ) );
    REQUIRE( registry.hasTool( "view:zoom_to_layer" ) );
    REQUIRE( registry.hasTool( "view:zoom_to_asset" ) );
    REQUIRE( registry.hasTool( "view:fit_all" ) );
    REQUIRE( registry.hasTool( "view:set_scale" ) );
    REQUIRE( registry.hasTool( "roi:set" ) );
    REQUIRE( registry.hasTool( "roi:clear" ) );
    REQUIRE( registry.hasTool( "canvas:draw_roi" ) );
  }

  SECTION( "findTool supports underscore name normalization" )
  {
    auto toolOpt = registry.findTool( "view_get_state" );
    REQUIRE( toolOpt.has_value() );
    REQUIRE( toolOpt->name == "view:get_state" );
    REQUIRE( toolOpt->category == "view" );

    auto roiOpt = registry.findTool( "roi_set" );
    REQUIRE( roiOpt.has_value() );
    REQUIRE( roiOpt->name == "roi:set" );
  }

  SECTION( "exportOpenAiToolDefinitions formats valid OpenAI function array" )
  {
    const Json::Value definitions = registry.exportOpenAiToolDefinitions();
    REQUIRE( definitions.isArray() );
    REQUIRE( definitions.size() >= 8 );

    bool foundSetExtent = false;
    for ( const auto &item : definitions )
    {
      REQUIRE( item.isMember( "type" ) );
      REQUIRE( item["type"].asString() == "function" );
      REQUIRE( item.isMember( "function" ) );
      const Json::Value &func = item["function"];
      REQUIRE( func.isMember( "name" ) );
      REQUIRE( func.isMember( "description" ) );
      REQUIRE( func.isMember( "parameters" ) );

      if ( func["name"].asString() == "view_set_extent" || func["name"].asString() == "view:set_extent" )
      {
        foundSetExtent = true;
        const Json::Value &params = func["parameters"];
        REQUIRE( params.isMember( "type" ) );
        REQUIRE( params["type"].asString() == "object" );
        REQUIRE( params.isMember( "properties" ) );
        REQUIRE( params["properties"].isMember( "bbox" ) );
        REQUIRE( params["properties"].isMember( "extent" ) );
      }
    }
    REQUIRE( foundSetExtent );
  }

  SECTION( "exportSystemPromptCatalog creates Markdown documentation table" )
  {
    const std::string catalog = registry.exportSystemPromptCatalog();
    REQUIRE_FALSE( catalog.empty() );
    REQUIRE( catalog.find( "| Category | Tool Name | Description |" ) != std::string::npos );
    REQUIRE( catalog.find( "`view:set_extent`" ) != std::string::npos );
    REQUIRE( catalog.find( "`roi:set`" ) != std::string::npos );
  }

  SECTION( "dynamic registration and unregistration" )
  {
    InteractionToolDefinition customTool;
    customTool.name = "custom:ping";
    customTool.displayName = "Custom Ping";
    customTool.category = "custom";
    customTool.description = "Test custom tool";
    customTool.inputSchema = Json::Value( Json::objectValue );
    customTool.handler = []( const Json::Value & ) {
      Json::Value res( Json::objectValue );
      res["status"] = "success";
      res["pong"] = true;
      return res;
    };

    registry.registerTool( customTool );
    REQUIRE( registry.hasTool( "custom:ping" ) );

    const Json::Value execRes = registry.execute( "custom:ping", Json::Value() );
    REQUIRE( execRes["status"].asString() == "success" );
    REQUIRE( execRes["pong"].asBool() == true );

    REQUIRE( registry.unregisterTool( "custom:ping" ) );
    REQUIRE_FALSE( registry.hasTool( "custom:ping" ) );
  }
}

TEST_CASE( "Interaction tools schema validation and error reporting", "[agent][interaction][validation]" )
{
  TestAppFixture fixture;
  auto &registry = InteractionToolRegistry::instance();
  registry.reset();

  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 3857 ) );
  canvas.setExtent( QgsRectangle( 0, 0, 1000, 1000 ) );

  ViewControlService service( nullptr, &canvas );
  registry.registerBuiltinTools( &service );

  SECTION( "missing required parameters report structured errors" )
  {
    // view:set_scale requires 'scale'
    Json::Value emptyParams( Json::objectValue );
    Json::Value scaleRes = registry.execute( "view:set_scale", emptyParams );
    REQUIRE( scaleRes["status"].asString() == "error" );
    REQUIRE( scaleRes.isMember( "errorMessage" ) );
    REQUIRE_FALSE( scaleRes["errorMessage"].asString().empty() );

    // view:zoom_to_layer requires 'layer_id'
    Json::Value layerRes = registry.execute( "view:zoom_to_layer", emptyParams );
    REQUIRE( layerRes["status"].asString() == "error" );
    REQUIRE( layerRes.isMember( "errorMessage" ) );

    // view:zoom_to_asset requires 'asset_id'
    Json::Value assetRes = registry.execute( "view:zoom_to_asset", emptyParams );
    REQUIRE( assetRes["status"].asString() == "error" );
    REQUIRE( assetRes.isMember( "errorMessage" ) );
  }

  SECTION( "unregistered tool returns error status" )
  {
    Json::Value res = registry.execute( "view:unknown_action", Json::Value() );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res["errorMessage"].asString().find( "not registered" ) != std::string::npos );
  }
}

TEST_CASE( "Invalid extent and coordinate parameters validation", "[agent][interaction][extent]" )
{
  TestAppFixture fixture;
  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 3857 ) );
  canvas.setExtent( QgsRectangle( 0, 0, 1000, 1000 ) );

  ViewControlService service( nullptr, &canvas );

  SECTION( "xmin >= xmax is rejected" )
  {
    Json::Value params( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = 500.0;
    bbox["ymin"] = 100.0;
    bbox["xmax"] = 200.0; // xmax < xmin!
    bbox["ymax"] = 300.0;
    params["bbox"] = bbox;

    Json::Value res = service.setExtent( params );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res["errorMessage"].asString().find( "Invalid extent parameters" ) != std::string::npos );
  }

  SECTION( "ymin >= ymax is rejected" )
  {
    Json::Value params( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = 100.0;
    bbox["ymin"] = 600.0;
    bbox["xmax"] = 200.0;
    bbox["ymax"] = 300.0; // ymax < ymin!
    params["bbox"] = bbox;

    Json::Value res = service.setExtent( params );
    REQUIRE( res["status"].asString() == "error" );
  }

  SECTION( "non-numeric and non-finite values are rejected" )
  {
    Json::Value params( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = "invalid_number";
    bbox["ymin"] = 0.0;
    bbox["xmax"] = 100.0;
    bbox["ymax"] = 100.0;
    params["bbox"] = bbox;

    Json::Value res = service.setExtent( params );
    REQUIRE( res["status"].asString() == "error" );
  }

  SECTION( "invalid scale is rejected" )
  {
    Json::Value zeroScale( Json::objectValue );
    zeroScale["scale"] = 0.0;
    Json::Value resZero = service.setScale( zeroScale );
    REQUIRE( resZero["status"].asString() == "error" );

    Json::Value negScale( Json::objectValue );
    negScale["scale"] = -500.0;
    Json::Value resNeg = service.setScale( negScale );
    REQUIRE( resNeg["status"].asString() == "error" );
  }

  SECTION( "invalid ROI geometry WKT is rejected" )
  {
    Json::Value roiParams( Json::objectValue );
    roiParams["geometry"] = "INVALID_WKT_POLYGON((0 0))";
    Json::Value res = service.setRoi( roiParams );
    REQUIRE( res["status"].asString() == "error" );
  }
}

TEST_CASE( "CRS validation and coordinate transformation", "[agent][interaction][crs]" )
{
  TestAppFixture fixture;
  QgsMapCanvas canvas;
  // Canvas in Web Mercator EPSG:3857
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 3857 ) );
  canvas.setExtent( QgsRectangle( 0, 0, 10000, 10000 ) );

  ViewControlService service( nullptr, &canvas );

  SECTION( "invalid CRS string is rejected with clear error" )
  {
    Json::Value params( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = 10.0;
    bbox["ymin"] = 20.0;
    bbox["xmax"] = 30.0;
    bbox["ymax"] = 40.0;
    params["bbox"] = bbox;
    params["crs"] = "EPSG:99999999";

    Json::Value res = service.setExtent( params );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res["errorMessage"].asString().find( "Invalid or unsupported CRS" ) != std::string::npos );
  }

  SECTION( "valid CRS transformation reprojects extent to canvas CRS" )
  {
    // Provide extent in WGS84 (EPSG:4326)
    Json::Value params( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = 100.0;
    bbox["ymin"] = 20.0;
    bbox["xmax"] = 105.0;
    bbox["ymax"] = 25.0;
    params["bbox"] = bbox;
    params["crs"] = "EPSG:4326";

    Json::Value res = service.setExtent( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["crs"].asString() == "EPSG:3857" );

    // Verify canvas extent was reprojected into 3857 meter coordinates
    const QgsRectangle ext = canvas.extent();
    REQUIRE( ext.xMinimum() > 1000000.0 );
    REQUIRE( ext.xMaximum() > ext.xMinimum() );
    REQUIRE( ext.yMinimum() > 1000000.0 );
    REQUIRE( ext.yMaximum() > ext.yMinimum() );
  }

  SECTION( "ROI set and clear with CRS transformation" )
  {
    Json::Value roiParams( Json::objectValue );
    roiParams["geometry"] = "POLYGON((100 20, 105 20, 105 25, 100 25, 100 20))";
    roiParams["crs"] = "EPSG:4326";

    Json::Value res = service.setRoi( roiParams );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["crs"].asString() == "EPSG:3857" );
    REQUIRE_FALSE( service.lastRoiWkt().isEmpty() );

    Json::Value state = service.getState();
    REQUIRE( state["status"].asString() == "success" );
    REQUIRE( state.isMember( "roi" ) );
    REQUIRE( state["roi"].isObject() );
    REQUIRE( state["roi"]["crs"].asString() == "EPSG:3857" );

    // Clear ROI
    Json::Value clearRes = service.clearRoi();
    REQUIRE( clearRes["status"].asString() == "success" );
    REQUIRE( service.lastRoiWkt().isEmpty() );

    Json::Value clearedState = service.getState();
    REQUIRE( clearedState["roi"].isNull() );
  }
}

TEST_CASE( "GUI thread safety and cross-thread invocation", "[agent][interaction][threading]" )
{
  TestAppFixture fixture;
  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  canvas.setExtent( QgsRectangle( 100, 20, 110, 30 ) );

  ViewControlService service( nullptr, &canvas );

  SECTION( "calling ViewControlService from background thread marshals safely" )
  {
    Json::Value capturedState;
    Json::Value capturedSetExtent;
    Json::Value capturedSetScale;
    std::atomic<bool> done{ false };
    std::thread worker( [&]() {
      // 1. Query state from background thread
      capturedState = service.getState();

      // 2. Set extent from background thread
      Json::Value extentParams( Json::objectValue );
      Json::Value bbox( Json::objectValue );
      bbox["xmin"] = 101.0;
      bbox["ymin"] = 21.0;
      bbox["xmax"] = 109.0;
      bbox["ymax"] = 29.0;
      extentParams["bbox"] = bbox;
      capturedSetExtent = service.setExtent( extentParams );

      // 3. Set scale from background thread
      Json::Value scaleParams( Json::objectValue );
      scaleParams["scale"] = 25000.0;
      capturedSetScale = service.setScale( scaleParams );

      done = true;
    } );

    while ( !done )
    {
      QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
      std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    worker.join();

    REQUIRE( capturedState["status"].asString() == "success" );
    REQUIRE( capturedState["crs"].asString() == "EPSG:4326" );
    REQUIRE( capturedSetExtent["status"].asString() == "success" );
    REQUIRE( capturedSetExtent["extent"]["xmin"].asDouble() <= 101.0 );
    REQUIRE( capturedSetExtent["extent"]["xmax"].asDouble() >= 109.0 );
    REQUIRE( capturedSetScale["status"].asString() == "success" );
    REQUIRE( capturedSetScale["scale"].asDouble() > 0.0 );
    REQUIRE( canvas.scale() > 0.0 );
  }
}

TEST_CASE( "ToolCallDispatcher interaction namespace routing and dispatch", "[agent][interaction][dispatcher]" )
{
  TestAppFixture fixture;
  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  canvas.setExtent( QgsRectangle( 10, 10, 20, 20 ) );

  ViewControlService service( nullptr, &canvas );
  auto &registry = InteractionToolRegistry::instance();
  registry.reset();
  registry.registerBuiltinTools( &service );

  ToolCallDispatcher dispatcher;
  dispatcher.setInteractionActionHandler(
    [&]( const std::string &name, const Json::Value &args ) {
      return registry.execute( name, args );
    } );

  SECTION( "ToolCallDispatcher classifies view: and roi: actions as ToolCall" )
  {
    Json::Value viewEnvelope( Json::objectValue );
    viewEnvelope["name"] = "view:get_state";
    viewEnvelope["parameters"] = Json::Value( Json::objectValue );

    REQUIRE( dispatcher.classify( viewEnvelope ) == ToolCallClassification::ToolCall );
    REQUIRE( dispatcher.rejectionReason( viewEnvelope ).isEmpty() );

    Json::Value roiEnvelope( Json::objectValue );
    roiEnvelope["name"] = "roi:set";
    Json::Value roiArgs( Json::objectValue );
    roiArgs["geometry"] = "POLYGON((11 11, 15 11, 15 15, 11 15, 11 11))";
    roiEnvelope["parameters"] = roiArgs;

    REQUIRE( dispatcher.classify( roiEnvelope ) == ToolCallClassification::ToolCall );
    REQUIRE( dispatcher.rejectionReason( roiEnvelope ).isEmpty() );
  }

  SECTION( "ToolCallDispatcher dispatches view: and roi: calls synchronously" )
  {
    Json::Value extentEnvelope( Json::objectValue );
    extentEnvelope["name"] = "view:set_extent";
    Json::Value args( Json::objectValue );
    Json::Value bbox( Json::objectValue );
    bbox["xmin"] = 12.0;
    bbox["ymin"] = 12.0;
    bbox["xmax"] = 18.0;
    bbox["ymax"] = 18.0;
    args["bbox"] = bbox;
    extentEnvelope["parameters"] = args;

    const Json::Value result = dispatcher.dispatchAndAwait( extentEnvelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result["crs"].asString() == "EPSG:4326" );
    REQUIRE( canvas.extent().contains( QgsRectangle( 12.0, 12.0, 18.0, 18.0 ) ) );
  }

  SECTION( "ToolCallDispatcher preserves legacy canvas:draw_roi compatibility" )
  {
    Json::Value canvasEnvelope( Json::objectValue );
    canvasEnvelope["name"] = "canvas:draw_roi";
    Json::Value args( Json::objectValue );
    args["geometry"] = "POLYGON((12 12, 14 12, 14 14, 12 14, 12 12))";
    canvasEnvelope["parameters"] = args;

    const Json::Value result = dispatcher.dispatchAndAwait( canvasEnvelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE_FALSE( service.lastRoiWkt().isEmpty() );
  }
}

TEST_CASE( "MCP Server interaction tool discovery and schemas", "[agent][interaction][mcp]" )
{
  TestAppFixture fixture;
  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  ViewControlService service( nullptr, &canvas );

  auto &registry = InteractionToolRegistry::instance();
  registry.reset();
  registry.registerBuiltinTools( &service );

  TestInteractionMcpServer mcpServer;

  SECTION( "MCP list_interaction_tools returns registered GIS tools" )
  {
    const QVariantMap listRes = mcpServer.testListInteractionTools();
    REQUIRE( listRes.contains( "tools" ) );
    const QVariantList tools = listRes["tools"].toList();
    REQUIRE( tools.size() >= 8 );

    bool foundGetState = false;
    for ( const QVariant &t : tools )
    {
      const QVariantMap map = t.toMap();
      if ( map["name"].toString() == "view:get_state" )
      {
        foundGetState = true;
        REQUIRE( map["category"].toString() == "view" );
        REQUIRE( map.contains( "inputSchema" ) );
      }
    }
    REQUIRE( foundGetState );
  }

  SECTION( "MCP get_interaction_schema returns parameter schema" )
  {
    const QVariantMap schemaRes = mcpServer.testGetInteractionSchema( "view:set_extent" );
    REQUIRE( schemaRes["name"].toString() == "view:set_extent" );
    REQUIRE( schemaRes.contains( "inputSchema" ) );
    const QVariantMap schemaMap = schemaRes["inputSchema"].toMap();
    REQUIRE( schemaMap.contains( "properties" ) );
    const QVariantMap props = schemaMap["properties"].toMap();
    REQUIRE( props.contains( "bbox" ) );
    REQUIRE( props.contains( "extent" ) );
  }

  SECTION( "MCP get_interaction_schema throws for unknown tool" )
  {
    REQUIRE_THROWS( mcpServer.testGetInteractionSchema( "unknown:tool_name" ) );
  }
}
