// tests/test_mission_runtime_store.cpp — single-authority mission runtime
// persistence contract.
//
// The window's project-boundary rule (New Project / failed Open must drop the
// previous project's mission before the next save) relies on this store-level
// contract: a boundary-reset runtime ({}, authorityLoaded == false) saved to
// a project without an authority publishes a FRESH mission there — a newly
// minted id and an empty timeline — never a foreign project's context. That
// is the exact end state of the "New Project → Save As" flow after the
// window reset; before the reset the window's cache leaked the old mission
// into the new project's sidecar + XML through this same path.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_runtime_store.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::app;

namespace {

QString sidecarPath( const QString &projectPath )
{
    return missionSidecarPathForProject( projectPath );
}

} // namespace

TEST_CASE( "A fresh project has no mission authority and loads clean",
           "[mission][runtime_store]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "fresh.qgs" ) );

    MissionRuntimeState state;
    QDomDocument doc;
    QString error;
    REQUIRE( loadMissionRuntime( project, doc, state, &error ) );
    CHECK( error.isEmpty() );
    CHECK_FALSE( state.authorityLoaded );
    CHECK_FALSE( state.authorityCorrupt );
    CHECK( state.context.missionId.isEmpty() );
    CHECK( state.timeline.missionId().isEmpty() );
    CHECK( state.timeline.tasks().isEmpty() );
    CHECK( state.problems.isEmpty() );
    CHECK_FALSE( QFile::exists( sidecarPath( project ) ) );
}

TEST_CASE( "A boundary-reset runtime saves as a fresh mission, never a foreign one",
           "[mission][runtime_store][boundary]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString projectB = dir.filePath( QStringLiteral( "project_b.qgs" ) );

    // Exactly what resetMissionSessionState() leaves behind at a project
    // boundary (New Project / failed Open).
    MissionRuntimeState reset;
    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( projectB, doc, reset, &error ) );

    // The authority exists now and carries a FRESH mission id with the new
    // project as its home — an empty context mints its own id, and the empty
    // timeline stays empty. Nothing was inherited from a previous project.
    CHECK( QFile::exists( sidecarPath( projectB ) ) );
    CHECK( reset.authorityLoaded );
    CHECK_FALSE( reset.context.missionId.isEmpty() );
    CHECK( reset.context.projectRef == projectB );
    CHECK( reset.timeline.tasks().isEmpty() );

    // Reload agrees with what the save published.
    MissionRuntimeState reloaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE( loadMissionRuntime( projectB, freshDoc, reloaded, &loadErr ) );
    CHECK( reloaded.authorityLoaded );
    CHECK( reloaded.context.missionId == reset.context.missionId );
    CHECK( reloaded.context.projectRef == projectB );
    CHECK( reloaded.timeline.tasks().isEmpty() );
}

TEST_CASE( "A saved mission context and timeline survive a save/load roundtrip",
           "[mission][runtime_store]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "roundtrip.qgs" ) );

    MissionRuntimeState state;
    state.context.missionId = QStringLiteral( "mission-abc-123" );
    state.context.projectRef = project;

    MissionTask task;
    task.id = QStringLiteral( "task-1" );
    task.stage = MissionStage::Import;
    task.title = QStringLiteral( "导入数据" );
    task.capabilityId = QStringLiteral( "project.importLayer" );
    REQUIRE( state.timeline.addTask( task ).applied );

    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    CHECK( state.authorityLoaded );

    MissionRuntimeState reloaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE( loadMissionRuntime( project, freshDoc, reloaded, &loadErr ) );
    CHECK( reloaded.authorityLoaded );
    CHECK( reloaded.context.missionId == QStringLiteral( "mission-abc-123" ) );
    REQUIRE( reloaded.timeline.tasks().size() == 1 );
    CHECK( reloaded.timeline.tasks().first().id == QStringLiteral( "task-1" ) );
    CHECK( reloaded.timeline.tasks().first().status == MissionTaskStatus::Pending );
}

TEST_CASE( "A corrupt authority refuses the save until the state is recovered",
           "[mission][runtime_store][poison]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "poisoned.qgs" ) );

    MissionRuntimeState corrupt;
    corrupt.authorityCorrupt = true;
    QDomDocument doc;
    QString error;
    REQUIRE_FALSE( saveMissionRuntime( project, doc, corrupt, &error ) );
    CHECK( error == QStringLiteral( "poisoned_authority" ) );
    // Refusal must not have written anything.
    CHECK_FALSE( QFile::exists( sidecarPath( project ) ) );
}

namespace {

void writeSidecarBytes( const QString &projectPath, const QByteArray &bytes )
{
    QFile side( sidecarPath( projectPath ) );
    REQUIRE( side.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( side.write( bytes ) == bytes.size() );
}

MissionRuntimeState stateWithOneTask( const QString &projectPath )
{
    MissionRuntimeState state;
    state.context.projectRef = projectPath;
    MissionTask task;
    task.id = QStringLiteral( "task-1" );
    task.stage = MissionStage::Import;
    task.title = QStringLiteral( "正射纠正" );
    REQUIRE( state.timeline.addTask( task ).applied );
    return state;
}

} // namespace

TEST_CASE( "a corrupt sidecar with no last-good refuses the load and poisons the session",
           "[mission][runtime_store][corrupt]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "corrupt.qgs" ) );

    MissionRuntimeState state = stateWithOneTask( project );
    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    // Drop the snapshot: only the corrupt artifact remains.
    REQUIRE( QFile::remove( missionRuntimeLastGoodPathForProject( project ) ) );
    writeSidecarBytes( project, QStringLiteral( "{\"kind\": \"mission_cont" ).toUtf8() );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE_FALSE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityCorrupt );
    CHECK_FALSE( loaded.authorityLoaded );
    // Fail closed means NOT an empty legitimate mission.
    CHECK( loaded.timeline.tasks().isEmpty() );
    CHECK_FALSE( loaded.problems.isEmpty() );

    // The poisoned session can never publish over the artifact.
    MissionRuntimeState poisoned = loaded;
    QString saveErr;
    REQUIRE_FALSE( saveMissionRuntime( project, freshDoc, poisoned, &saveErr ) );
    CHECK( saveErr == QStringLiteral( "poisoned_authority" ) );
}

TEST_CASE( "a corrupt sidecar recovers from the last-good snapshot",
           "[mission][runtime_store][recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "recover.qgs" ) );

    MissionRuntimeState state = stateWithOneTask( project );
    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    const QString missionId = state.context.missionId;
    writeSidecarBytes( project, "not json at all" );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityLoaded );
    CHECK( loaded.recoveredFromLastGood );
    CHECK( loaded.context.missionId == missionId );
    REQUIRE( loaded.timeline.tasks().size() == 1 );
    CHECK( loaded.timeline.tasks().first().id == QStringLiteral( "task-1" ) );
    CHECK( loaded.notices.contains( QStringLiteral( "authority_recovered_from_last_good" ) ) );
}

TEST_CASE( "an absent sidecar with a last-good still recovers the authority",
           "[mission][runtime_store][recovery]" )
{
    // #1169: the removal paths (failed-sidecar-write cleanup, .qgz-only
    // projects) leave a good snapshot beside an ABSENT sidecar; keying
    // recovery on sidecar presence made the next save publish an empty
    // mission.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "absent.qgs" ) );

    MissionRuntimeState state = stateWithOneTask( project );
    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    REQUIRE( QFile::remove( sidecarPath( project ) ) );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityLoaded );
    CHECK( loaded.recoveredFromLastGood );
    CHECK( loaded.timeline.tasks().size() == 1 );

    // And the recovered authority republishes a real sidecar again.
    MissionRuntimeState republished = loaded;
    REQUIRE( saveMissionRuntime( project, freshDoc, republished, &error ) );
    CHECK( QFile::exists( sidecarPath( project ) ) );
}

TEST_CASE( "an embedded timeline with an unknown future schema refuses the load",
           "[mission][runtime_store][future]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "future.qgs" ) );

    // A well-formed context whose embedded timeline claims schema 9.9 — a
    // document written by a FUTURE version. Decoding it as 1.0 would silently
    // downgrade or drop the task space, so the load must refuse.
    MissionContext ctx;
    ctx.projectRef = project;
    MissionTimeline timeline;
    MissionTask task;
    task.id = QStringLiteral( "task-9" );
    REQUIRE( timeline.addTask( task ).applied );
    embedMissionTimeline( ctx, timeline );

    QJsonObject doc = missionContextToJson( ctx );
    QJsonObject metadata = doc.value( QStringLiteral( "metadata" ) ).toObject();
    QJsonObject embedded = metadata.value( QLatin1String( kMissionTimelineMetadataKey ) )
                               .toObject();
    embedded.insert( QStringLiteral( "schema_version" ), QStringLiteral( "9.9" ) );
    metadata.insert( QLatin1String( kMissionTimelineMetadataKey ), embedded );
    doc.insert( QStringLiteral( "metadata" ), metadata );

    QJsonDocument futureDoc( doc );
    writeSidecarBytes( project, futureDoc.toJson( QJsonDocument::Compact ) );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE_FALSE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityCorrupt );
    CHECK( loadErr == QStringLiteral( "unsupported_schema_version" ) );
    CHECK_FALSE( loaded.problems.isEmpty() );

    MissionRuntimeState poisoned = loaded;
    QString saveErr;
    REQUIRE_FALSE( saveMissionRuntime( project, freshDoc, poisoned, &saveErr ) );
    CHECK( saveErr == QStringLiteral( "poisoned_authority" ) );
}

TEST_CASE( "a truncated sidecar is never silently an empty mission",
           "[mission][runtime_store][corrupt]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "truncated.qgs" ) );

    MissionContext ctx;
    ctx.projectRef = project;
    MissionTimeline timeline;
    MissionTask task;
    task.id = QStringLiteral( "task-1" );
    REQUIRE( timeline.addTask( task ).applied );
    embedMissionTimeline( ctx, timeline );
    const QByteArray bytes =
        QJsonDocument( missionContextToJson( ctx ) ).toJson( QJsonDocument::Compact );
    REQUIRE( bytes.size() > 16 );
    // A crash mid-write residue: half a document, no last-good anywhere.
    writeSidecarBytes( project, bytes.left( bytes.size() / 2 ) );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE_FALSE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityCorrupt );
}

TEST_CASE( "the authority round-trips through a Unicode project path",
           "[mission][runtime_store][unicode]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project =
        dir.filePath( QStringLiteral( "项目-热度图-Ω.qgz" ) );

    MissionRuntimeState state = stateWithOneTask( project );
    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    CHECK( QFile::exists( sidecarPath( project ) ) );

    MissionRuntimeState loaded;
    QDomDocument freshDoc;
    QString loadErr;
    REQUIRE( loadMissionRuntime( project, freshDoc, loaded, &loadErr ) );
    CHECK( loaded.authorityLoaded );
    CHECK( loaded.context.missionId == state.context.missionId );
    REQUIRE( loaded.timeline.tasks().size() == 1 );
    CHECK( loaded.timeline.tasks().first().title == QStringLiteral( "正射纠正" ) );
    CHECK( loaded.timeline.missionId() == state.timeline.missionId() );
}
