// test_edit_agent_state.cpp — F11 Package F: agent-visible editing facts.
//
// Oracles:
//   * payload facts are compared against hand-set session state;
//   * the read-only surface is proven by BEHAVIOR: executing the tool must
//     leave feature counts, edit buffers and stacks identical (no write
//     path exists on the type itself);
//   * catalog exposure goes through the real SpatialToolProvider.
#include <catch2/catch_test_macros.hpp>

#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"
#include "agent/tool_catalog/agent_tool.h"
#include "editing/rs_edit_agent_tool.h"
#include "editing/rs_edit_session.h"
#include "editing/rs_snapping_controller.h"

#include <QApplication>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <json/json.h>

using namespace sicnu::agent::spatial_tools;

namespace
{

struct AgentFixture
{
    AgentFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_agent_state";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        project = QgsProject::instance();
        project->clear();
    }
    ~AgentFixture() { project->clear(); }
    QgsProject *project = nullptr;
};

qlonglong liveCount( QgsVectorLayer *layer )
{
    qlonglong n = 0;
    QgsFeatureIterator it = layer->getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
        ++n;
    return n;
}

} // namespace

TEST_CASE( "editing:state payload mirrors live session facts",
           "[editing][agent][f11][oracle]" )
{
    AgentFixture fx;
    QgsMapCanvas canvas;
    RsEditSession session;
    RsSnappingController snapping( &canvas );

    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326&field=class:int" ),
                          QStringLiteral( "samples" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.startEditing() );
    REQUIRE( session.attachLayer( &layer ).isEmpty() );
    REQUIRE( snapping.enabled() == canvas.snappingUtils()->config().enabled() );
    snapping.enableVertexSegment( 12.0, Qgis::MapToolUnit::Pixels, true );

    RsEditAgentTool::Sources sources;
    sources.session = &session;
    sources.snapping = &snapping;
    RsEditAgentTool tool( sources );

    // Empty session snapshot: no facts, no crash.
    {
        RsEditAgentTool detached( RsEditAgentTool::Sources{} );
        const SpatialToolResult r = detached.execute( Json::Value( Json::nullValue ) );
        REQUIRE( r.success );
        CHECK( r.output["editing"]["dirty"].asBool() == false );
        CHECK( r.output["editing"]["layers"].size() == 0 );
        CHECK( r.output["editing"]["snapping"]["enabled"].asBool() == false );
    }

    // Live facts.
    const SpatialToolResult r = tool.execute( Json::Value( Json::nullValue ) );
    REQUIRE( r.success );
    const Json::Value &editing = r.output["editing"];
    CHECK( editing["layers"].size() == 1 );
    const Json::Value &entry = editing["layers"][0];
    CHECK( entry["id"].asString() == layer.id().toStdString() );
    CHECK( entry["name"].asString() == "samples" );
    CHECK( entry["editing"].asBool() );
    CHECK( entry["modified"].asBool() == false );
    CHECK( entry["locked"].asBool() == false );
    CHECK( entry["undoDepth"].asInt64() == 0 );
    CHECK( entry["featureCount"].asInt64() == 0 );
    CHECK( editing["snapping"]["tolerance"].asDouble() == 12.0 );
    CHECK( editing["snapping"]["intersectionSnapping"].asBool() == true );

    // Facts change with the session: add a feature under a command → dirty.
    layer.beginEditCommand( QStringLiteral( "add" ) );
    QgsFeature f( layer.fields() );
    f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 1, 1 ) ) );
    layer.addFeature( f );
    layer.endEditCommand();
    layer.selectByIds( QgsFeatureIds() << f.id() );

    const SpatialToolResult r2 = tool.execute( Json::Value( Json::nullValue ) );
    REQUIRE( r2.success );
    const Json::Value &editing2 = r2.output["editing"];
    CHECK( editing2["dirty"].asBool() == true );
    CHECK( editing2["layers"][0]["modified"].asBool() == true );
    CHECK( editing2["layers"][0]["featureCount"].asInt64() == 1 );
    CHECK( editing2["layers"][0]["selectedCount"].asInt() == 1 );
    CHECK( editing2["layers"][0]["undoDepth"].asInt64() == 1 );

    // Lock surfaces.
    REQUIRE( session.setLocked( layer.id(), true ) );
    const SpatialToolResult r3 = tool.execute( Json::Value( Json::nullValue ) );
    CHECK( r3.output["editing"]["layers"][0]["locked"].asBool() == true );
}

TEST_CASE( "editing:state is behaviorally read-only", "[editing][agent][f11][negative]" )
{
    AgentFixture fx;
    RsEditSession session;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "samples" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.startEditing() );
    QgsFeature f( layer.fields() );
    f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 2, 2 ) ) );
    layer.addFeature( f );
    REQUIRE( session.attachLayer( &layer ).isEmpty() );

    RsEditAgentTool::Sources sources;
    sources.session = &session;
    RsEditAgentTool tool( sources );

    const qlonglong countBefore = liveCount( &layer );
    const bool modifiedBefore = layer.isModified();
    const int undoBefore = layer.undoStack()->count();

    for ( int i = 0; i < 3; ++i )
    {
        const SpatialToolResult r = tool.execute( Json::Value( Json::nullValue ) );
        REQUIRE( r.success );
    }

    CHECK( liveCount( &layer ) == countBefore );
    CHECK( layer.isModified() == modifiedBefore );
    CHECK( layer.undoStack()->count() == undoBefore );
    // Locked state is also untouched (the tool has no lock API).
    CHECK_FALSE( session.isLocked( layer.id() ) );
}

TEST_CASE( "editing:state rejects malformed input and validates via schema",
           "[editing][agent][f11][negative]" )
{
    AgentFixture fx;
    RsEditAgentTool tool( RsEditAgentTool::Sources{} );

    // Non-object input is rejected.
    Json::Value arrayInput( Json::arrayValue );
    arrayInput.append( 1 );
    const SpatialToolResult r = tool.execute( arrayInput );
    CHECK_FALSE( r.success );
    CHECK( r.errorCode == "INVALID_PARAMETER" );

    // Schema declares an object with no required fields.
    const Json::Value schema = tool.inputSchema();
    CHECK( schema["type"].asString() == "object" );

    // Round-trip validation via the shared validator must accept {}.
    const std::string err = validateAgainstRequired( Json::Value( Json::objectValue ), schema );
    CHECK( err.empty() );
}

TEST_CASE( "editing:state joins the agent catalog with an editing group",
           "[editing][agent][f11]" )
{
    AgentFixture fx;
    SpatialToolRegistry &registry = SpatialToolRegistry::instance();
    registry.reset();
    RsEditAgentTool::Sources sources;
    auto *tool = new RsEditAgentTool( sources );
    REQUIRE( registry.registerTool( SpatialToolPtr( tool ) ) );

    SpatialToolProvider provider;
    const std::vector<sicnu::agent::tool_catalog::AgentTool> catalog = provider.provideTools();

    bool found = false;
    for ( const auto &entry : catalog )
    {
      if ( entry.name == RsEditAgentTool::kToolId )
      {
        found = true;
        CHECK( entry.group == "editing" );
      }
    }
    CHECK( found );
    registry.reset();
}
