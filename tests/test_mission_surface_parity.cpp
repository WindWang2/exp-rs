// Track glm53-mission-workbench-12 — cross-surface identity gate for the
// mission capability set: no duplicate ids, no phantom capabilities, no Pi
// name collisions, and one projection shared by the desktop model and the
// MCP/Pi agent surface.
//
// The last case is the important one: it parses the *real* Pi bridge and the
// *real* MCP allow-list in the source tree, so forgetting to wire a surface
// fails this test instead of silently shipping a tool the agent cannot reach.
#include <catch2/catch_test_macros.hpp>

#ifndef SICNU_MISSION_SOURCE_DIR
#error "SICNU_MISSION_SOURCE_DIR must be defined for the mission parity gate"
#endif

#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_timeline_model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

using namespace sicnu::app;

namespace
{

QString readSourceFile( const QString &relativePath )
{
    QFile file( QStringLiteral( SICNU_MISSION_SOURCE_DIR ) + QLatin1Char( '/' ) + relativePath );
    if ( !file.open( QIODevice::ReadOnly ) )
        return QString();
    return QString::fromUtf8( file.readAll() );
}

/// The EXP_RS_TOOL_CATEGORIES block of pi/exp-rs-spatial.ts, verbatim.
QString piCategoryBlock()
{
    const QString source = readSourceFile( QStringLiteral( "pi/exp-rs-spatial.ts" ) );
    const int at = source.indexOf( QStringLiteral( "EXP_RS_TOOL_CATEGORIES" ) );
    if ( at < 0 )
        return QString();
    // The array literal follows the declaration within a few hundred chars.
    return source.mid( at, 600 );
}

} // namespace

TEST_CASE( "mission surface ids are unique across every surface", "[mission][parity]" )
{
    const QVector<MissionSurfaceEntry> registry = missionSurfaceRegistry();
    REQUIRE_FALSE( registry.isEmpty() );
    REQUIRE( missionDuplicateSurfaceIds().isEmpty() );

    const QStringList ids = missionSurfaceIds( registry );
    REQUIRE( ids.size() == registry.size() );

    // Each surface keeps its own namespace, so an agent tool id can never be
    // mistaken for a CommandRegistry id.
    for ( const MissionSurfaceEntry &entry : registry )
    {
        if ( entry.surface == MissionSurface::AgentTool )
        {
            REQUIRE( entry.id.startsWith( QStringLiteral( "mission:" ) ) );
        }
        else
        {
            REQUIRE_FALSE( entry.id.contains( QLatin1Char( ':' ) ) );
        }
    }

    // The gate also has to *detect* duplicates, not merely report none.
    QVector<MissionSurfaceEntry> injected = registry;
    injected.push_back( registry.first() );
    REQUIRE( missionDuplicateSurfaceIds( injected ) == QStringList{ registry.first().id } );
}

TEST_CASE( "mission capabilities are never phantoms", "[mission][parity]" )
{
    REQUIRE( missionPhantomSurfaceIds().isEmpty() );

    // An advertised id without a description is a phantom: the agent would see
    // a name it cannot explain.
    QVector<MissionSurfaceEntry> injected = missionSurfaceRegistry();
    injected.push_back( { QStringLiteral( "mission:ghost" ), MissionSurface::AgentTool, QString() } );
    REQUIRE( missionPhantomSurfaceIds( injected ) == QStringList{ QStringLiteral( "mission:ghost" ) } );
}

TEST_CASE( "pi tool names are unique under the documented mapping rule", "[mission][parity]" )
{
    REQUIRE( missionPiToolName( QStringLiteral( "mission:context" ) )
             == QStringLiteral( "exprs_mission_context" ) );
    REQUIRE( missionPiToolName( QStringLiteral( "rs:spectral_index" ) )
             == QStringLiteral( "exprs_rs_spectral_index" ) );

    const QStringList names = missionPiToolNames();
    REQUIRE( names.size() == missionSurfaceIds( MissionSurface::AgentTool ).size() );
    REQUIRE( missionDuplicatePiToolNames().isEmpty() );

    // Two distinct tool ids that both contain ':' must not collapse onto one
    // Pi name ("mission:context" vs "mission.context").
    const QVector<MissionSurfaceEntry> collapsing = {
        { QStringLiteral( "mission:context" ), MissionSurface::AgentTool, QStringLiteral( "a" ) },
        { QStringLiteral( "mission.context" ), MissionSurface::AgentTool, QStringLiteral( "b" ) },
    };
    REQUIRE( missionPiToolName( collapsing.at( 0 ).id ) == missionPiToolName( collapsing.at( 1 ).id ) );
    REQUIRE( missionDuplicateSurfaceIds( collapsing ).isEmpty() );
}

TEST_CASE( "the desktop model and the agent tool render the same bytes", "[mission][parity]" )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-1" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/demo.qgz" ) );

    MissionTask task;
    task.id = QStringLiteral( "t1" );
    task.stage = MissionStage::Analyze;
    task.title = QStringLiteral( "NDVI over AOI" );
    task.capabilityId = QStringLiteral( "rs:spectral_index" );
    task.inputRefIds = QStringList{ QStringLiteral( "layer-1" ) };
    task.outputRefIds = QStringList{ QStringLiteral( "artifact-1" ) };
    task.run.kind = QStringLiteral( "task_center" );
    task.run.id = QStringLiteral( "tc-9" );
    REQUIRE( timeline.addTask( task ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "2026-09-20T00:00:00Z" ) ).applied );

    MissionTimelineModel model;
    model.setTimeline( timeline );
    REQUIRE( model.rowCount() == 1 );

    // GUI path (model row projection) vs agent path (MCP mission:timeline row).
    const QByteArray fromGui = missionCanonicalJson( model.projectionAt( 0 ) );
    const QByteArray fromAgent =
        missionCanonicalJson( missionTaskProjectionJson( *timeline.task( QStringLiteral( "t1" ) ) ) );
    REQUIRE_FALSE( fromGui.isEmpty() );
    REQUIRE( fromGui == fromAgent );

    // And the timeline envelope exposes the same cursor the UI consumes.
    const QJsonObject projection = missionTimelineProjectionJson( timeline, 32, 0 );
    REQUIRE( projection.value( QStringLiteral( "mission_id" ) ).toString()
             == QStringLiteral( "m-1" ) );
    REQUIRE( projection.value( QStringLiteral( "current_stage" ) ).toString()
             == QStringLiteral( "analyze" ) );
    REQUIRE( projection.value( QStringLiteral( "task_count" ) ).toInt() == 1 );
    REQUIRE( projection.value( QStringLiteral( "task_truncated" ) ).toBool() == false );

    // Cursor semantics: events after seq N only.
    const quint64 seq = timeline.lastEventSeq();
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Succeeded,
                                  QStringLiteral( "2026-09-20T00:01:00Z" ) ).applied );
    const QJsonObject since = missionTimelineProjectionJson( timeline, 32, seq );
    REQUIRE( since.value( QStringLiteral( "events" ) ).toArray().size() == 1 );
    const QJsonObject all = missionTimelineProjectionJson( timeline, 32, 0 );
    REQUIRE( all.value( QStringLiteral( "events" ) ).toArray().size() == 3 );

    // Bounding: maxItems caps the task list and says so.
    const QJsonObject bounded = missionTimelineProjectionJson( timeline, 0, 0 );
    REQUIRE( bounded.value( QStringLiteral( "tasks" ) ).toArray().isEmpty() );
    REQUIRE( bounded.value( QStringLiteral( "task_count" ) ).toInt() == 1 );
    REQUIRE( bounded.value( QStringLiteral( "task_truncated" ) ).toBool() == true );
}

TEST_CASE( "the mission family is wired into the MCP allow-list and the Pi bridge",
           "[mission][parity]" )
{
    const QString allowList = readSourceFile(
        QStringLiteral( "src/agent/tool_catalog/surface_registry.cpp" ) );
    REQUIRE_FALSE( allowList.isEmpty() );
    REQUIRE( allowList.contains( QStringLiteral( "\"mission:\"" ) ) );

    const QString categories = piCategoryBlock();
    REQUIRE_FALSE( categories.isEmpty() );
    REQUIRE( categories.contains( QStringLiteral( "mission" ) ) );
}
