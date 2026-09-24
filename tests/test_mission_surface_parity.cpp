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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <functional>

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

// ---------------------------------------------------------------------------
// #1300 shifted left: the task-space test targets embed the mission sources
// directly (Qt Core only, no leaf library), so the embed list is a
// hand-maintained copy of the modules' TU graph. #1300 was a link failure
// days after the code was fine — a companion source missing from the embed
// list. This gate parses the REAL embed list and the REAL include closure of
// the task-space TUs and fails on either direction of the drift, while the
// cause is still one commit deep.
// ---------------------------------------------------------------------------

namespace
{

QString stripCmakeComments( const QString &text )
{
    QString out;
    out.reserve( text.size() );
    for ( const QStringView line : QStringView( text ).split( QLatin1Char( '\n' ) ) )
    {
        const int hash = line.indexOf( QLatin1Char( '#' ) );
        out += hash >= 0 ? line.left( hash ) : line;
        out += QLatin1Char( '\n' );
    }
    return out;
}

/// Basenames of the *.cpp entries of `<command>(<name> ...)` calls in CMake
/// text. The name match is word-bounded so a future
/// SICNU_MISSION_TASK_SPACE_SOURCES_EXTRA cannot satisfy the lookup.
QStringList cmakeCallCppEntries( const QString &cmakeText, const QString &command,
                                 const QString &name )
{
    const QString clean = stripCmakeComments( cmakeText );
    QStringList basenames;
    const QRegularExpression head( QStringLiteral( "\\b%1\\(\\s*%2\\b" )
                                       .arg( QRegularExpression::escape( command ),
                                             QRegularExpression::escape( name ) ) );
    auto headIt = head.globalMatch( clean );
    while ( headIt.hasNext() )
    {
        const auto match = headIt.next();
        const int open = clean.indexOf( QLatin1Char( '(' ), match.capturedStart( 0 ) );
        if ( open < 0 )
            continue;
        int depth = 0;
        int i = open;
        for ( ; i < clean.size(); ++i )
        {
            if ( clean.at( i ) == QLatin1Char( '(' ) )
                ++depth;
            else if ( clean.at( i ) == QLatin1Char( ')' ) )
            {
                --depth;
                if ( depth == 0 )
                    break;
            }
        }
        const QString body = clean.mid( open, i - open + 1 );
        const QRegularExpression tokenRe( QStringLiteral( "\\S+" ) );
        auto it = tokenRe.globalMatch( body );
        while ( it.hasNext() )
        {
            const QString token = it.next().captured( 0 );
            if ( token.endsWith( QStringLiteral( ".cpp" ) ) )
                basenames.push_back( QFileInfo( token ).fileName() );
        }
    }
    basenames.sort();
    basenames.removeAll( QString() );
    basenames.removeDuplicates();
    return basenames;
}

/// Transitive quoted-include closure of @p roots over the source tree; every
/// reachable app/workbench header with a sibling .cpp contributes that TU.
QStringList requiredWorkbenchTus( const QString &sourceDir, const QStringList &roots )
{
    QSet<QString> visited;
    QStringList required;
    const QRegularExpression includeRe( QStringLiteral( "#include\\s+\"([^\"]+)\"" ) );

    std::function<void( const QString & )> visit = [&]( const QString &rel ) {
        if ( visited.contains( rel ) )
            return;
        visited.insert( rel );
        const QString text = readSourceFile( rel );
        if ( text.isEmpty() )
            return;
        const QString dir = QFileInfo( rel ).path();
        auto it = includeRe.globalMatch( text );
        while ( it.hasNext() )
        {
            QString inc = it.next().captured( 1 );
            // Try includer-relative first, then src/-rooted.
            QStringList candidates;
            if ( !dir.isEmpty() && dir != QLatin1String( "." ) )
                candidates << dir + QLatin1Char( '/' ) + inc;
            candidates << QStringLiteral( "src/" ) + inc;
            for ( const QString &candidate : candidates )
            {
                const QString clean = QDir::cleanPath( candidate );
                if ( !QFileInfo::exists( sourceDir + QLatin1Char( '/' ) + clean ) )
                    continue;
                if ( clean.startsWith( QStringLiteral( "src/app/workbench/" ) )
                     && clean.endsWith( QStringLiteral( ".h" ) ) )
                {
                    const int prefixLen =
                        int( QStringLiteral( "src/app/workbench/" ).size() );
                    const QString tu =
                        clean.mid( prefixLen ).chopped( 2 ) + QStringLiteral( ".cpp" );
                    if ( QFileInfo::exists( sourceDir + QLatin1String( "/src/app/workbench/" )
                                            + tu ) && !required.contains( tu ) )
                        required.push_back( tu );
                }
                visit( clean );
                break;
            }
        }
    };
    for ( const QString &root : roots )
        visit( root );

    required.sort();
    return required;
}

} // namespace

TEST_CASE( "the task-space embed list covers the transitive mission include closure",
           "[mission][parity][build-wiring]" )
{
    const QStringList taskSpaceTestTus{
        QStringLiteral( "tests/test_mission_stage.cpp" ),
        QStringLiteral( "tests/test_mission_surface_parity.cpp" ),
        QStringLiteral( "tests/test_mission_scale_benchmark.cpp" ),
    };

    const QString testsCmake =
        readSourceFile( QStringLiteral( "tests/CMakeLists.txt" ) );
    REQUIRE_FALSE( testsCmake.isEmpty() );
    const QStringList embedded = cmakeCallCppEntries(
        testsCmake, QStringLiteral( "set" ),
        QStringLiteral( "SICNU_MISSION_TASK_SPACE_SOURCES" ) );
    // The #1300 regression, verbatim: the paged timeline model missing from
    // the embed list.
    REQUIRE( embedded.contains( QStringLiteral( "mission_stage.cpp" ) ) );
    REQUIRE( embedded.contains( QStringLiteral( "mission_projection.cpp" ) ) );
    REQUIRE( embedded.contains( QStringLiteral( "mission_timeline_model.cpp" ) ) );

    // Closure roots: the three test TUs plus the embedded task-space sources
    // (as src/-rooted paths for the include walk).
    const QStringList required = requiredWorkbenchTus(
        SICNU_MISSION_SOURCE_DIR,
        taskSpaceTestTus + QStringList{ QStringLiteral( "src/app/workbench/mission_stage.cpp" ),
                                        QStringLiteral( "src/app/workbench/mission_projection.cpp" ),
                                        QStringLiteral(
                                            "src/app/workbench/mission_timeline_model.cpp" ) } );

    // Every companion TU of the closure is embedded...
    for ( const QString &tu : required )
        REQUIRE( embedded.contains( tu ) );
    REQUIRE_FALSE( required.isEmpty() );
    // ...and the list carries no dead entries (a stale .cpp is silent
    // duplicate compile cost and hides the drift the gate exists for).
    for ( const QString &tu : embedded )
        REQUIRE( required.contains( tu ) );

    // Lethality self-check: the gate must kill the #1300 mutation, not merely
    // report today's green state. Remove the timeline model from the parsed
    // list and the same comparison must flag exactly that file.
    QStringList mutated = embedded;
    mutated.removeAll( QStringLiteral( "mission_timeline_model.cpp" ) );
    for ( const QString &tu : required )
    {
        if ( tu == QLatin1String( "mission_timeline_model.cpp" ) )
            REQUIRE_FALSE( mutated.contains( tu ) );
        else
            REQUIRE( mutated.contains( tu ) );
    }
}

TEST_CASE( "the shared mission-runtime source lists carry the task space",
           "[mission][parity][build-wiring]" )
{
    // cmake/SicnuMissionRuntimeSources.cmake drives the mission-runtime gate
    // build; a task-space source dropped there would quietly stop being
    // gate-checked while every other list still compiled it.
    const QString shared = readSourceFile(
        QStringLiteral( "cmake/SicnuMissionRuntimeSources.cmake" ) );
    REQUIRE_FALSE( shared.isEmpty() );

    for ( const QString &setName : { QStringLiteral( "SICNU_MISSION_RUNTIME_SOURCES_FULL" ),
                                     QStringLiteral( "SICNU_MISSION_RUNTIME_SOURCES_GATE" ) } )
    {
        const QStringList entries =
            cmakeCallCppEntries( shared, QStringLiteral( "set" ), setName );
        REQUIRE( entries.contains( QStringLiteral( "mission_stage.cpp" ) ) );
        REQUIRE( entries.contains( QStringLiteral( "mission_projection.cpp" ) ) );
        REQUIRE( entries.contains( QStringLiteral( "mission_timeline_model.cpp" ) ) );
    }
}

TEST_CASE( "the surface test's embed list covers its own mission include closure",
           "[mission][parity][build-wiring]" )
{
    // test_mission_timeline_surface embeds a second hand-maintained source
    // list (the panel pulls qgis_gui, so it cannot ride the task-space
    // trio). Same drift shape as #1300, same gate: every workbench companion
    // of its include closure must be embedded.
    const QString testsCmake =
        readSourceFile( QStringLiteral( "tests/CMakeLists.txt" ) );
    REQUIRE_FALSE( testsCmake.isEmpty() );
    const QStringList embedded = cmakeCallCppEntries(
        testsCmake, QStringLiteral( "add_executable" ),
        QStringLiteral( "test_mission_timeline_surface" ) );
    REQUIRE( embedded.contains( QStringLiteral( "mission_timeline_panel.cpp" ) ) );
    REQUIRE( embedded.contains( QStringLiteral( "mission_timeline_model.cpp" ) ) );

    QStringList roots{ QStringLiteral( "tests/test_mission_timeline_surface.cpp" ) };
    for ( const QString &tu : embedded )
        roots << QStringLiteral( "src/app/workbench/" ) + tu;
    const QStringList required =
        requiredWorkbenchTus( SICNU_MISSION_SOURCE_DIR, roots );
    REQUIRE_FALSE( required.isEmpty() );
    for ( const QString &tu : required )
        REQUIRE( embedded.contains( tu ) );
}
