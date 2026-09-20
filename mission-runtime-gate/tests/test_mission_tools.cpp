// tests/test_mission_tools.cpp
//
// Mission Runtime 13.0 — real `mission:*` agent tool gates (WP2/WP4).
//
// Oracles covered (.planning/glm53-mission-runtime-13/ORACLES.md):
//   O7 real tools in a fresh process — read, legal transition, illegal
//      transition rejected without mutation, no fake Running, retry lineage,
//      run binding/unbinding, reference reconciliation
//   O8 no fake Running — start is refused for an unverifiable run authority
//   O9 surface parity — the tool payload equals the shared projection the
//      desktop model renders (and does so across a process boundary)

#include "agent/spatial_tools/mission_tools.h"
#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_runtime_store.h"
#include "app/workbench/mission_run_authority.h"
#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_tool_authority.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCryptographicHash>
#include <QCoreApplication>

#include <cstdio>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::app;

namespace
{

QString uniqueStem()
{
    return QStringLiteral( "mission-%1" )
        .arg( QString::number( QRandomGenerator::global()->generate(), 16 ) );
}

MissionTask makeTask( const QString &id, MissionStage stage, const QString &title,
                      MissionTaskStatus status = MissionTaskStatus::Pending,
                      const QStringList &inputs = {}, const QStringList &outputs = {} )
{
    MissionTask t;
    t.id = id;
    t.stage = stage;
    t.title = title;
    t.status = status;
    t.capabilityId = QStringLiteral( "rs:spectral_index" );
    t.inputRefIds = inputs;
    t.outputRefIds = outputs;
    return t;
}

/// A project on disk whose runtime the tools operate on.
struct Fixture
{
    QTemporaryDir dir;
    QString project;
    QString missionId;

    explicit Fixture( const QString &id )
        : missionId( id )
    {
        project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );
        MissionRuntimeState state;
        state.context.missionId = missionId;
        state.context.missionName = QStringLiteral( "Tool gate mission" );
        state.timeline.setMissionId( missionId );
        state.timeline.addTask( makeTask( QStringLiteral( "import-1" ), MissionStage::Import,
                                          QStringLiteral( "Import scene" ),
                                          MissionTaskStatus::Succeeded ) );
        state.timeline.addTask( makeTask( QStringLiteral( "pre-1" ), MissionStage::Preprocess,
                                          QStringLiteral( "Atmos correction" ),
                                          MissionTaskStatus::Failed,
                                          { QStringLiteral( "layer-a" ) } ) );
        state.timeline.addTask( makeTask( QStringLiteral( "ana-1" ), MissionStage::Analyze,
                                          QStringLiteral( "NDVI" ), MissionTaskStatus::Pending,
                                          { QStringLiteral( "layer-a" ) },
                                          { QStringLiteral( "artifact-ndvi" ) } ) );
        QDomDocument doc;
        QString err;
        REQUIRE( saveMissionRuntime( project, doc, state, &err ) );
    }
};

/// Point the process-wide host at @p project with the store-backed authority.
void installHost( const QString &project,
                  sicnu::app::MissionRunStatusResolver runResolver = nullptr,
                  sicnu::app::MissionRefResolver refResolver = nullptr )
{
    auto &host = sicnu::agent::spatial_tools::MissionToolHost::instance();
    host.setAuthority( std::make_shared<sicnu::app::MissionStoreAuthority>() );
    host.setProjectPathProvider( [project]() -> QString { return project; } );
    host.setProjectDocumentProvider( []() -> QDomDocument { return QDomDocument(); } );
    host.setRunStatusResolver( runResolver );
    host.setRefResolverProvider(
        refResolver ? sicnu::agent::spatial_tools::MissionToolHost::RefResolverProvider(
                          [refResolver]() -> sicnu::app::MissionRefResolver { return refResolver; } )
                    : sicnu::agent::spatial_tools::MissionToolHost::RefResolverProvider{} );
}

void clearHost()
{
    auto &host = sicnu::agent::spatial_tools::MissionToolHost::instance();
    host.setAuthority( nullptr );
    host.setProjectPathProvider( nullptr );
    host.setRunStatusResolver( nullptr );
    host.setRefResolverProvider( nullptr );
    host.setProjectDocumentProvider( nullptr );
}

Json::Value parseToolOutput( const sicnu::agent::spatial_tools::SpatialToolResult &result )
{
    REQUIRE( result.success );
    return result.output;
}

QJsonObject toQJsonObject( const Json::Value &value )
{
    const std::string text = Json::FastWriter().write( value );
    return QJsonDocument::fromJson( QByteArray::fromRawData( text.data(),
                                                              static_cast<int>( text.size() ) ) )
        .object();
}

sicnu::agent::spatial_tools::MissionAdvanceTool advanceTool()
{
    return sicnu::agent::spatial_tools::MissionAdvanceTool();
}

Json::Value advanceInput( const QString &taskId, const QString &action,
                          const QString &runKind = {}, const QString &runId = {},
                          const QString &errorCode = {}, const QString &errorMessage = {} )
{
    Json::Value input( Json::objectValue );
    input[ "task_id" ] = taskId.toStdString();
    input[ "action" ] = action.toStdString();
    if ( !runKind.isEmpty() )
        input[ "run_kind" ] = runKind.toStdString();
    if ( !runId.isEmpty() )
        input[ "run_id" ] = runId.toStdString();
    if ( !errorCode.isEmpty() )
        input[ "error_code" ] = errorCode.toStdString();
    if ( !errorMessage.isEmpty() )
        input[ "error_message" ] = errorMessage.toStdString();
    return input;
}

} // namespace

// ── the tools are real, typed objects ────────────────────────────────────

TEST_CASE( "mission tools expose typed schemas", "[mission][tools]" )
{
    sicnu::agent::spatial_tools::MissionContextTool contextTool;
    sicnu::agent::spatial_tools::MissionTimelineTool timelineTool;
    sicnu::agent::spatial_tools::MissionAdvanceTool advance;

    CHECK( contextTool.name() == "mission:context" );
    CHECK( timelineTool.name() == "mission:timeline" );
    CHECK( advance.name() == "mission:advance" );
    const std::vector<sicnu::agent::spatial_tools::SpatialTool *> all = {
        &contextTool, &timelineTool, &advance };
    for ( sicnu::agent::spatial_tools::SpatialTool *tool : all )
    {
        CHECK_FALSE( tool->displayName().empty() );
        CHECK_FALSE( tool->description().empty() );
        const Json::Value schema = tool->inputSchema();
        CHECK( schema.isObject() );
        CHECK( schema[ "type" ] == "object" );
        CHECK( schema.isMember( "properties" ) );
        CHECK( tool->outputSchema().isObject() );
    }

    // The advance schema requires the identity + action and validates.
    const Json::Value schema = advance.inputSchema();
    CHECK( schema[ "required" ].size() == 2 );
    CHECK_FALSE( sicnu::agent::spatial_tools::validateAgainstRequired(
                     Json::Value( Json::objectValue ), schema ).empty() );
    CHECK( sicnu::agent::spatial_tools::validateAgainstRequired(
               advanceInput( QStringLiteral( "t" ), QStringLiteral( "start" ) ), schema ).empty() );
    // The required gate is presence-based (the documented contract); an
    // empty id passes it and is rejected by the tool itself.
    CHECK( sicnu::agent::spatial_tools::validateAgainstRequired(
               advanceInput( QString(), QStringLiteral( "start" ) ), schema ).empty() );
    const auto emptyId = advance.execute( advanceInput( QString(), QStringLiteral( "start" ) ) );
    CHECK_FALSE( emptyId.success );
    CHECK( emptyId.errorCode == "INVALID_PARAMETER" );
}

// ── read surfaces ────────────────────────────────────────────────────────

TEST_CASE( "mission:context reads the mission projection", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-ctx" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionContextTool tool;
    Json::Value input( Json::objectValue );
    input[ "max_items" ] = 8;
    const auto result = tool.execute( input );
    REQUIRE( result.success );

    const QJsonObject out = toQJsonObject( parseToolOutput( result ) );
    CHECK( out.value( QStringLiteral( "mission_id" ) ).toString() == fx.missionId );
    // pre-1 is Failed, so the earliest unsettled stage is preprocess.
    CHECK( out.value( QStringLiteral( "current_stage" ) ).toString() == QLatin1String( "preprocess" ) );
    CHECK( out.value( QStringLiteral( "task_count" ) ).toInt() == 3 );
    CHECK( out.value( QStringLiteral( "timeline_present" ) ).toBool() );
    CHECK( out.value( QStringLiteral( "project_ref" ) ).toString() == fx.project );
    CHECK( out.value( QStringLiteral( "mission_name" ) ).toString()
           == QLatin1String( "Tool gate mission" ) );

    // Bounded object projection: counts plus capped id lists.
    const QJsonObject counts = out.value( QStringLiteral( "counts" ) ).toObject();
    CHECK( counts.value( QStringLiteral( "layers" ) ).toInt() == 0 );
    CHECK( counts.value( QStringLiteral( "results" ) ).toInt() == 0 );
    CHECK( out.value( QStringLiteral( "layers" ) ).toArray().isEmpty() );
    clearHost();
}

TEST_CASE( "mission:timeline reads the shared projection with a cursor", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-tl" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionTimelineTool tool;

    Json::Value input( Json::objectValue );
    input[ "max_items" ] = 32;
    const auto full = tool.execute( input );
    REQUIRE( full.success );
    const QJsonObject payload = toQJsonObject( parseToolOutput( full ) );

    // The payload equals the shared projection function (the same function
    // the desktop model renders) — parity by construction.
    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    const QJsonObject expected =
        missionTimelineProjectionJson( state.timeline, 32, 0 );
    for ( const QString &key : { QStringLiteral( "mission_id" ), QStringLiteral( "current_stage" ),
                                 QStringLiteral( "task_count" ), QStringLiteral( "revision" ),
                                 QStringLiteral( "last_event_seq" ), QStringLiteral( "stages" ) } )
    {
        CHECK( payload.value( key ) == expected.value( key ) );
    }
    CHECK( payload.value( QStringLiteral( "tasks" ) ).toArray().size()
           == expected.value( QStringLiteral( "tasks" ) ).toArray().size() );
    CHECK( payload.value( QStringLiteral( "reconciliation_available" ) ).toBool() == false );

    // Cursor semantics: only events after since_seq.
    Json::Value cursorInput( Json::objectValue );
    cursorInput[ "since_seq" ] = static_cast<Json::Int64>( state.timeline.lastEventSeq() );
    const auto tail = tool.execute( cursorInput );
    REQUIRE( tail.success );
    CHECK( toQJsonObject( parseToolOutput( tail ) ).value( QStringLiteral( "events" ) ).toArray().isEmpty() );

    // max_items 0 = stage summary only.
    Json::Value metaOnly( Json::objectValue );
    metaOnly[ "max_items" ] = 0;
    const auto meta = tool.execute( metaOnly );
    REQUIRE( meta.success );
    const QJsonObject metaOut = toQJsonObject( parseToolOutput( meta ) );
    CHECK( metaOut.value( QStringLiteral( "tasks" ) ).toArray().isEmpty() );
    CHECK( metaOut.value( QStringLiteral( "task_truncated" ) ).toBool() );
    clearHost();
}

// ── mutations: legal, illegal, lineage ───────────────────────────────────

TEST_CASE( "mission:advance performs a legal transition and persists it", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-adv" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionAdvanceTool tool;
    const auto started = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                     QStringLiteral( "start" ),
                                                     QStringLiteral( "task_center" ),
                                                     QStringLiteral( "7001" ) ) );
    REQUIRE( started.success );
    const QJsonObject out = toQJsonObject( parseToolOutput( started ) );
    CHECK( out.value( QStringLiteral( "applied" ) ).toBool() );
    CHECK( out.value( QStringLiteral( "reason" ) ).toString() == QLatin1String( "ok" ) );
    const QJsonObject task = out.value( QStringLiteral( "task" ) ).toObject();
    CHECK( task.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "running" ) );
    CHECK( task.value( QStringLiteral( "attempts" ) ).toInt() == 1 );
    CHECK( task.value( QStringLiteral( "run_kind" ) ).toString() == QLatin1String( "task_center" ) );
    CHECK( task.value( QStringLiteral( "run_id" ) ).toString() == QLatin1String( "7001" ) );

    // Persisted through the single authority: a cold reload sees it.
    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    CHECK( state.timeline.task( QStringLiteral( "ana-1" ) )->status == MissionTaskStatus::Running );
    CHECK( state.timeline.task( QStringLiteral( "ana-1" ) )->attempts == 1 );

    // fail records the error pair.
    const auto failed = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                    QStringLiteral( "fail" ), {}, {},
                                                    QStringLiteral( "GDAL_OPEN_FAILED" ),
                                                    QStringLiteral( "raster missing" ) ) );
    REQUIRE( failed.success );
    const QJsonObject failedTask =
        toQJsonObject( parseToolOutput( failed ) ).value( QStringLiteral( "task" ) ).toObject();
    CHECK( failedTask.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" ) );
    CHECK( failedTask.value( QStringLiteral( "error_code" ) ).toString()
           == QLatin1String( "GDAL_OPEN_FAILED" ) );
    CHECK( failedTask.value( QStringLiteral( "error_message" ) ).toString()
           == QLatin1String( "raster missing" ) );
    clearHost();
}

TEST_CASE( "mission:advance rejects an illegal transition without mutating", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-illegal" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionAdvanceTool tool;
    const auto result = tool.execute( advanceInput( QStringLiteral( "import-1" ),
                                                    QStringLiteral( "start" ),
                                                    QStringLiteral( "task_center" ),
                                                    QStringLiteral( "9100" ) ) );
    // A settled task cannot be restarted: a structured answer, not a crash.
    REQUIRE( result.success );
    const QJsonObject out = toQJsonObject( parseToolOutput( result ) );
    CHECK_FALSE( out.value( QStringLiteral( "applied" ) ).toBool() );
    CHECK( out.value( QStringLiteral( "reason" ) ).toString()
           == QLatin1String( "illegal_transition:succeeded->running" ) );

    // Nothing was persisted: the revision on disk is unchanged.
    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    const quint64 revision = state.timeline.revision();
    (void) tool.execute( advanceInput( QStringLiteral( "nope" ), QStringLiteral( "retry" ) ) );
    MissionRuntimeState after;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), after, &err ) );
    CHECK( after.timeline.revision() == revision );

    // Unknown task and unknown action are validation failures.
    const auto unknown = tool.execute( advanceInput( QStringLiteral( "nope" ),
                                                     QStringLiteral( "retry" ) ) );
    CHECK_FALSE( unknown.success );
    CHECK( unknown.errorCode == "unknown_task" );
    const auto badAction = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                       QStringLiteral( "launch" ) ) );
    CHECK_FALSE( badAction.success );
    CHECK( badAction.errorCode == "INVALID_PARAMETER" );
    clearHost();
}

TEST_CASE( "mission:advance keeps the retry lineage", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-retry" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionAdvanceTool tool;
    const auto retried = tool.execute( advanceInput( QStringLiteral( "pre-1" ),
                                                     QStringLiteral( "retry" ) ) );
    REQUIRE( retried.success );
    const QJsonObject out = toQJsonObject( parseToolOutput( retried ) );
    CHECK( out.value( QStringLiteral( "applied" ) ).toBool() );
    const QJsonObject task = out.value( QStringLiteral( "task" ) ).toObject();
    CHECK( task.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "pending" ) );
    CHECK( task.value( QStringLiteral( "retry_of" ) ).toString() == QLatin1String( "pre-1" ) );

    // A requeue alone does not inflate attempts; starting does.
    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    CHECK( state.timeline.task( QStringLiteral( "pre-1" ) )->attempts == 0 );

    const auto started = tool.execute( advanceInput( QStringLiteral( "pre-1" ),
                                                     QStringLiteral( "start" ),
                                                     QStringLiteral( "task_center" ),
                                                     QStringLiteral( "7002" ) ) );
    REQUIRE( started.success );
    const QJsonObject startedTask =
        toQJsonObject( parseToolOutput( started ) ).value( QStringLiteral( "task" ) ).toObject();
    CHECK( startedTask.value( QStringLiteral( "attempts" ) ).toInt() == 1 );
    CHECK( startedTask.value( QStringLiteral( "retry_of" ) ).toString() == QLatin1String( "pre-1" ) );
    clearHost();
}

// ── O8: no fake Running ──────────────────────────────────────────────────

TEST_CASE( "mission:advance refuses to claim Running without a run authority", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-run" ) );
    installHost( fx.project );

    sicnu::agent::spatial_tools::MissionAdvanceTool tool;
    const auto result = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                    QStringLiteral( "start" ) ) );
    REQUIRE( result.success );
    const QJsonObject out = toQJsonObject( parseToolOutput( result ) );
    CHECK_FALSE( out.value( QStringLiteral( "applied" ) ).toBool() );
    CHECK( out.value( QStringLiteral( "reason" ) ).toString()
           == QLatin1String( "stale_run_reference" ) );

    // Nothing persisted.
    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    CHECK( state.timeline.task( QStringLiteral( "ana-1" ) )->status == MissionTaskStatus::Pending );
    clearHost();
}

TEST_CASE( "mission:advance verifies a pre-existing run authority", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-verify" ) );
    sicnu::agent::spatial_tools::MissionAdvanceTool tool;

    // Bind a run authority out-of-band (as the shell would when it hands a
    // task to TaskCenter).
    {
        installHost( fx.project );
        const auto bound = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                       QStringLiteral( "bind_run" ),
                                                       QStringLiteral( "task_center" ),
                                                       QStringLiteral( "8001" ) ) );
        REQUIRE( bound.success );
        clearHost();
    }

    // A resolver that reports the run as gone: start is refused.
    installHost( fx.project,
                 []( const sicnu::app::MissionRunRef & ) -> sicnu::app::MissionRunStatus {
                     sicnu::app::MissionRunStatus s;
                     s.liveness = sicnu::app::MissionRunLiveness::Unknown;
                     return s;
                 } );
    const auto refused = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                     QStringLiteral( "start" ) ) );
    REQUIRE( refused.success );
    CHECK( toQJsonObject( parseToolOutput( refused ) ).value( QStringLiteral( "reason" ) ).toString()
           == QLatin1String( "run_authority_unresolved" ) );

    // The same authority alive: start proceeds.
    installHost( fx.project,
                 []( const sicnu::app::MissionRunRef & ) -> sicnu::app::MissionRunStatus {
                     sicnu::app::MissionRunStatus s;
                     s.liveness = sicnu::app::MissionRunLiveness::Alive;
                     return s;
                 } );
    const auto started = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                     QStringLiteral( "start" ) ) );
    REQUIRE( started.success );
    CHECK( toQJsonObject( parseToolOutput( started ) ).value( QStringLiteral( "task" ) ).toObject()
               .value( QStringLiteral( "status" ) ).toString() == QLatin1String( "running" ) );

    // A Running task cannot drop its authority.
    const auto unbind = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                    QStringLiteral( "unbind_run" ) ) );
    REQUIRE( unbind.success );
    CHECK( toQJsonObject( parseToolOutput( unbind ) ).value( QStringLiteral( "reason" ) ).toString()
           == QLatin1String( "unbind_running_rejected" ) );
    clearHost();
}

// ── reconciliation through the tool ──────────────────────────────────────

TEST_CASE( "mission:advance reconciles references and run authority", "[mission][tools]" )
{
    Fixture fx( QStringLiteral( "mission-tools-rec" ) );
    const auto deadLayer = []( const QString &refId ) -> sicnu::app::MissionRefStatus {
        sicnu::app::MissionRefStatus s;
        if ( refId == QLatin1String( "layer-a" ) )
        {
            s.alive = false;
            s.reason = QStringLiteral( "deleted_layer" );
        }
        return s;
    };
    installHost( fx.project, nullptr, deadLayer );

    sicnu::agent::spatial_tools::MissionAdvanceTool tool;
    const auto result = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                    QStringLiteral( "reconcile" ) ) );
    REQUIRE( result.success );
    const QJsonObject out = toQJsonObject( parseToolOutput( result ) );
    CHECK( out.value( QStringLiteral( "applied" ) ).toBool() );
    const QJsonObject rec = out.value( QStringLiteral( "reconciliation" ) ).toObject();
    CHECK( rec.value( QStringLiteral( "has_issues" ) ).toBool() );
    CHECK( rec.value( QStringLiteral( "stale_tasks" ) ).toArray().size() == 2 );

    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( fx.project, QDomDocument(), state, &err ) );
    CHECK( state.timeline.task( QStringLiteral( "pre-1" ) )->status == MissionTaskStatus::Stale );
    CHECK( state.timeline.task( QStringLiteral( "ana-1" ) )->status == MissionTaskStatus::Stale );

    // Without a liveness resolver the action refuses instead of guessing.
    installHost( fx.project );
    const auto refused = tool.execute( advanceInput( QStringLiteral( "ana-1" ),
                                                     QStringLiteral( "reconcile" ) ) );
    REQUIRE( refused.success );
    CHECK( toQJsonObject( parseToolOutput( refused ) ).value( QStringLiteral( "reason" ) ).toString()
           == QLatin1String( "ref_resolver_unavailable" ) );
    clearHost();
}

// ── fail closed when there is no project ─────────────────────────────────

TEST_CASE( "mission tools refuse when no project is open", "[mission][tools]" )
{
    clearHost();
    sicnu::agent::spatial_tools::MissionContextTool contextTool;
    sicnu::agent::spatial_tools::MissionTimelineTool timelineTool;
    sicnu::agent::spatial_tools::MissionAdvanceTool advance;

    const Json::Value empty( Json::objectValue );
    const std::vector<sicnu::agent::spatial_tools::SpatialTool *> readers = {
        &contextTool, &timelineTool };
    for ( sicnu::agent::spatial_tools::SpatialTool *tool : readers )
    {
        const auto result = tool->execute( empty );
        CHECK_FALSE( result.success );
        CHECK( result.errorCode == "no_active_project" );
    }
    const auto result = advance.execute( advanceInput( QStringLiteral( "t" ),
                                                       QStringLiteral( "retry" ) ) );
    CHECK_FALSE( result.success );
    CHECK( result.errorCode == "no_active_project" );
}

// ── O7/O9: a fresh process reads and advances through the tools ──────────

TEST_CASE( "mission tools operate in a fresh process", "[mission][tools]" )
{
#ifndef Q_OS_LINUX
    SKIP( "fresh-process tool check uses /proc/self/exe (Linux)" );
#endif
    Fixture fx( QStringLiteral( "mission-tools-fresh" ) );

    // Parent: install the host and read the timeline through the tool.
    installHost( fx.project );
    sicnu::agent::spatial_tools::MissionTimelineTool tool;
    Json::Value input( Json::objectValue );
    input[ "max_items" ] = 32;
    const auto result = tool.execute( input );
    REQUIRE( result.success );
    const std::string parentBytes = Json::FastWriter().write( parseToolOutput( result ) );

    int argc = 1;
    static char arg0[] = "mission-runtime-gate";
    static char *argv[] = { arg0, nullptr };
    QCoreApplication app( argc, argv );

    QProcess proc;
    proc.setProgram( QStringLiteral( "/proc/self/exe" ) );
    proc.setArguments( { QStringLiteral( "mission tools fresh-process load helper" ) } );
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert( QStringLiteral( "MISSION_TOOLS_PROJECT" ), fx.project );
    env.insert( QStringLiteral( "QT_QPA_PLATFORM" ), QStringLiteral( "offscreen" ) );
    proc.setProcessEnvironment( env );
    proc.start();
    REQUIRE( proc.waitForFinished( 60000 ) );
    CHECK( proc.exitStatus() == QProcess::NormalExit );
    CHECK( proc.exitCode() == 0 );

    // The child prints the same projection bytes for the same mission.
    const QByteArray childOut = proc.readAllStandardOutput();
    const QByteArray marker = "MISSION_TOOLS_PAYLOAD ";
    const int at = childOut.indexOf( marker );
    REQUIRE( at >= 0 );
    QByteArray childBytes = childOut.mid( at + marker.size(),
                                          childOut.indexOf( '\n', at ) - at - marker.size() );
    while ( !childBytes.isEmpty() && ( childBytes.endsWith( '\n' ) || childBytes.endsWith( '\r' ) ) )
        childBytes.chop( 1 );
    QByteArray parentCopy( parentBytes.data(), static_cast<int>( parentBytes.size() ) );
    while ( !parentCopy.isEmpty()
            && ( parentCopy.endsWith( '\n' ) || parentCopy.endsWith( '\r' ) ) )
        parentCopy.chop( 1 );
    CHECK( childBytes.toStdString() == parentCopy.toStdString() );
    clearHost();
}

TEST_CASE( "mission tools fresh-process load helper", "[mission][tools][helper]" )
{
    const QByteArray project = qgetenv( "MISSION_TOOLS_PROJECT" );
    if ( project.isEmpty() )
        SKIP( "helper case: driven by the parent case through a child process" );

    auto &host = sicnu::agent::spatial_tools::MissionToolHost::instance();
    host.setAuthority( std::make_shared<sicnu::app::MissionStoreAuthority>() );
    host.setProjectPathProvider( [project]() -> QString { return QString::fromUtf8( project ); } );
    host.setProjectDocumentProvider( []() -> QDomDocument { return QDomDocument(); } );

    sicnu::agent::spatial_tools::MissionTimelineTool tool;
    Json::Value input( Json::objectValue );
    input[ "max_items" ] = 32;
    const auto result = tool.execute( input );
    REQUIRE( result.success );
    std::printf( "MISSION_TOOLS_PAYLOAD %s\n", Json::FastWriter().write( result.output ).c_str() );
}
