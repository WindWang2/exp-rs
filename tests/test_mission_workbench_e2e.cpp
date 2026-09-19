// Track glm53-mission-workbench-12 — end-to-end mission workbench loop:
//   GUI builds the mission -> the agent reads the *same* projection -> work is
//   executed through the existing run authority -> the GUI observes it
//   incrementally -> the project is saved, reopened and the history resumed.
//
// The runner below is a deterministic stand-in for TaskCenter; it deliberately
// speaks the same seam as the production bridge
// (src/app/workbench/mission_timeline_bridge.h): it only ever records a run
// authority id and asks the timeline for a transition. No second scheduler is
// introduced anywhere in this Track.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_timeline_model.h"
#include "app/workbench/mission_timeline_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>

using namespace sicnu::app;

namespace
{

/// Stand-in for the run authority the desktop shell binds to
/// (`MissionTaskCenterBridge`). Same call shape, deterministic outcomes.
struct FakeTaskCenter
{
    MissionTimeline *timeline = nullptr;
    int nextRunId = 0;

    MissionOutcome submit( const QString &taskId, const QString &iso )
    {
        MissionRunRef run;
        run.kind = QStringLiteral( "task_center" );
        run.id = QStringLiteral( "tc-%1" ).arg( ++nextRunId );
        // Bind the authority first: the timeline refuses to enter Running
        // without one, which is what keeps GUI/MCP/Pi in agreement.
        const MissionOutcome bound = timeline->bindRunReference( taskId, run, iso );
        if ( !bound.applied )
            return bound;
        return timeline->transition( taskId, MissionTaskStatus::Running, iso );
    }

    MissionOutcome finish( const QString &taskId, bool ok, const QString &iso )
    {
        return timeline->transition( taskId,
                                     ok ? MissionTaskStatus::Succeeded : MissionTaskStatus::Failed,
                                     iso );
    }

    MissionOutcome cancel( const QString &taskId, const QString &iso )
    {
        return timeline->transition( taskId, MissionTaskStatus::Canceled, iso );
    }

    MissionOutcome retry( const QString &taskId, const QString &iso )
    {
        MissionRunRef run;
        run.kind = QStringLiteral( "task_center" );
        run.id = QStringLiteral( "tc-%1" ).arg( ++nextRunId );
        return timeline->retry( taskId, iso, run );
    }
};

MissionTimeline fiveStageMission()
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-e2e" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/e2e.qgz" ) );

    const QStringList titles = { QStringLiteral( "Import GF-2 scene" ),
                                 QStringLiteral( "Orthorectify + calibrate" ),
                                 QStringLiteral( "NDVI + classification" ),
                                 QStringLiteral( "Accuracy assessment" ),
                                 QStringLiteral( "Publish map package" ) };

    int index = 0;
    for ( MissionStage stage : missionStages() )
    {
        MissionTask task;
        task.id = QStringLiteral( "task-%1" ).arg( index );
        task.stage = stage;
        task.title = titles.at( index );
        task.capabilityId = QStringLiteral( "rs:spectral_index" );
        task.inputRefIds.push_back( QStringLiteral( "layer-%1" ).arg( index ) );
        task.outputRefIds.push_back( QStringLiteral( "artifact-%1" ).arg( index ) );
        (void) timeline.addTask( task );
        ++index;
    }
    return timeline;
}

QString tempProjectPath()
{
    const QString dir = QDir::temp().filePath( QStringLiteral( "mission-e2e-" )
                                               + QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    QDir().mkpath( dir );
    return dir + QStringLiteral( "/demo.qgz" );
}

} // namespace

TEST_CASE( "a mission runs from import to publish and survives a project reopen",
           "[mission][e2e]" )
{
    MissionTimeline timeline = fiveStageMission();
    FakeTaskCenter runner;
    runner.timeline = &timeline;

    MissionTimelineModel model;
    model.setTimeline( timeline );
    REQUIRE( model.rowCount() == 5 );

    // --- 1. the agent reads exactly what the GUI shows -------------------
    const QJsonObject agentView = missionTimelineProjectionJson( timeline, 32, 0 );
    REQUIRE( agentView.value( QStringLiteral( "mission_id" ) ).toString()
             == QStringLiteral( "m-e2e" ) );
    REQUIRE( agentView.value( QStringLiteral( "current_stage" ) ).toString()
             == QStringLiteral( "import" ) );
    REQUIRE( agentView.value( QStringLiteral( "task_count" ) ).toInt() == 5 );

    for ( int row = 0; row < model.rowCount(); ++row )
    {
        const QByteArray gui = missionCanonicalJson( model.projectionAt( row ) );
        const QJsonArray tasks = agentView.value( QStringLiteral( "tasks" ) ).toArray();
        const QByteArray agent = missionCanonicalJson( tasks.at( row ).toObject() );
        REQUIRE( gui == agent );
    }

    // --- 2. execute through the run authority, observe incrementally -----
    quint64 cursor = timeline.lastEventSeq();
    const auto observe = [&]() {
        model.applyEvents( timeline, cursor );
        cursor = timeline.lastEventSeq();
    };

    REQUIRE( runner.submit( QStringLiteral( "task-0" ), QStringLiteral( "t0" ) ).applied );
    observe();
    REQUIRE( runner.finish( QStringLiteral( "task-0" ), true, QStringLiteral( "t1" ) ).applied );
    observe();

    REQUIRE( runner.submit( QStringLiteral( "task-1" ), QStringLiteral( "t2" ) ).applied );
    observe();
    REQUIRE( runner.finish( QStringLiteral( "task-1" ), false, QStringLiteral( "t3" ) ).applied );
    observe();

    // Incremental observation: one reset for the load, none afterwards.
    REQUIRE( model.resetCount() == 1 );
    REQUIRE( model.fullRangeDataChangedCount() == 0 );
    REQUIRE( model.incrementalApplyCount() == 4 );
    REQUIRE( model.touchedRows() == 4 );

    // GUI and agent agree after the failures as well.
    const QJsonObject afterFailure = missionTimelineProjectionJson( timeline, 32, 0 );
    REQUIRE( afterFailure.value( QStringLiteral( "current_stage" ) ).toString()
             == QStringLiteral( "preprocess" ) );

    // --- 3. persist and reopen -------------------------------------------
    const QString projectPath = tempProjectPath();
    QString error;
    REQUIRE( saveMissionTimelineToSidecar( projectPath, timeline, &error ) );
    REQUIRE( error.isEmpty() );

    MissionTimeline reopened;
    bool loaded = false;
    REQUIRE( loadMissionTimelineFromSidecar( projectPath, reopened, &loaded, &error ) );
    REQUIRE( loaded );
    REQUIRE( reopened == timeline );
    REQUIRE( reopened.tasks().size() == 5 );
    REQUIRE( reopened.events().size() == timeline.events().size() );

    MissionTimelineModel reopenedModel;
    reopenedModel.setTimeline( reopened );
    REQUIRE( reopenedModel.rowCount() == 5 );

    // --- 4. resume the failed task after reopening -----------------------
    FakeTaskCenter resumed;
    resumed.timeline = &reopened;
    resumed.nextRunId = 100;

    REQUIRE( reopened.task( QStringLiteral( "task-1" ) )->status == MissionTaskStatus::Failed );
    REQUIRE( resumed.retry( QStringLiteral( "task-1" ), QStringLiteral( "t4" ) ).applied );
    REQUIRE( reopened.task( QStringLiteral( "task-1" ) )->status == MissionTaskStatus::Pending );
    REQUIRE( reopened.task( QStringLiteral( "task-1" ) )->retryOf == QStringLiteral( "task-1" ) );

    REQUIRE( resumed.submit( QStringLiteral( "task-1" ), QStringLiteral( "t5" ) ).applied );
    REQUIRE( resumed.finish( QStringLiteral( "task-1" ), true, QStringLiteral( "t6" ) ).applied );
    REQUIRE( reopened.task( QStringLiteral( "task-1" ) )->status == MissionTaskStatus::Succeeded );
    REQUIRE( reopened.task( QStringLiteral( "task-1" ) )->attempts == 2 );
    // History grew: the retry is auditable, not a silent overwrite.
    REQUIRE( reopened.events().size() > timeline.events().size() );

    reopenedModel.applyEvents( reopened, 0 );
    REQUIRE( reopenedModel.resetCount() == 1 );

    // --- 5. deleting a layer after reopen marks work stale, on all surfaces
    MissionRefResolver resolver = []( const QString &id ) {
        return MissionRefStatus{ id != QStringLiteral( "layer-3" ),
                                 id == QStringLiteral( "layer-3" )
                                     ? QStringLiteral( "deleted_layer" )
                                     : QString() };
    };
    const MissionReconciliation rec = reconcileMission( reopened, resolver );
    REQUIRE( rec.danglingRefIds == QStringList{ QStringLiteral( "layer-3" ) } );
    REQUIRE( rec.staleTaskIds == QStringList{ QStringLiteral( "task-3" ) } );
    REQUIRE( applyReconciliation( reopened, rec, QStringLiteral( "t7" ) ) == 1 );

    reopenedModel.applyEvents( reopened, 0 );
    const QJsonObject staleView = missionTimelineProjectionJson( reopened, 32, 0 );
    QJsonArray staleTasks = staleView.value( QStringLiteral( "tasks" ) ).toArray();
    REQUIRE( staleTasks.at( 3 ).toObject().value( QStringLiteral( "status" ) ).toString()
             == QStringLiteral( "stale" ) );
    // The GUI row and the agent row still agree.
    REQUIRE( missionCanonicalJson( reopenedModel.projectionAt( 3 ) )
             == missionCanonicalJson( staleTasks.at( 3 ).toObject() ) );

    // --- 6. rename recovery ------------------------------------------------
    QVector<MissionRename> renames;
    renames.push_back( { QStringLiteral( "layer-2" ), QStringLiteral( "layer-2-renamed" ) } );
    REQUIRE( applyRenames( reopened, renames, QStringLiteral( "t8" ) ) >= 1 );
    REQUIRE( reopened.task( QStringLiteral( "task-2" ) )->inputRefIds
             == QStringList{ QStringLiteral( "layer-2-renamed" ) } );
    MissionRefResolver afterRename = []( const QString &id ) {
        return MissionRefStatus{ id != QStringLiteral( "layer-3" ), QString() };
    };
    REQUIRE_FALSE( reconcileMission( reopened, afterRename ).staleTaskIds.contains(
                       QStringLiteral( "task-2" ) ) );

    QDir( QFileInfo( projectPath ).absolutePath() ).removeRecursively();
}

TEST_CASE( "a missing sidecar is a fresh project, not an error", "[mission][e2e]" )
{
    const QString projectPath = tempProjectPath();
    MissionTimeline out;
    bool loaded = true;
    QString error;
    REQUIRE( loadMissionTimelineFromSidecar( projectPath, out, &loaded, &error ) );
    REQUIRE( loaded == false );
    REQUIRE( error.isEmpty() );
    QDir( QFileInfo( projectPath ).absolutePath() ).removeRecursively();
}
