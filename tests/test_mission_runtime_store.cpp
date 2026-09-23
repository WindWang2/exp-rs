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
