// Track glm53-mission-workbench-12 — mission task space: stage machine,
// resumable timeline and reference reconciliation.
//
// Every case here must fail before the module exists and pass after it: the
// transition table, the "Running needs a run authority" rule and the
// delete/rename reconciliation are the behaviours under test, not
// implementation details.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_stage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>

using namespace sicnu::app;

namespace
{

MissionTask makeTask( const QString &id,
                      MissionStage stage,
                      const QStringList &inputs = {},
                      const QStringList &outputs = {} )
{
    MissionTask task;
    task.id = id;
    task.stage = stage;
    task.title = QStringLiteral( "task " ) + id;
    task.capabilityId = QStringLiteral( "rs:spectral_index" );
    task.inputRefIds = inputs;
    task.outputRefIds = outputs;
    return task;
}

} // namespace

TEST_CASE( "mission stage and status keys round-trip", "[mission][stage]" )
{
    for ( MissionStage stage : missionStages() )
    {
        const QString key = QLatin1String( missionStageKey( stage ) );
        const auto decoded = missionStageFromKey( key );
        REQUIRE( decoded.has_value() );
        REQUIRE( *decoded == stage );
        REQUIRE_FALSE( missionStageLabel( stage ).isEmpty() );
    }
    REQUIRE_FALSE( missionStageFromKey( QStringLiteral( "nope" ) ).has_value() );

    for ( MissionTaskStatus status : { MissionTaskStatus::Pending, MissionTaskStatus::Running,
                                       MissionTaskStatus::Succeeded, MissionTaskStatus::Failed,
                                       MissionTaskStatus::Canceled, MissionTaskStatus::Stale } )
    {
        const auto decoded = missionTaskStatusFromKey( QLatin1String( missionTaskStatusKey( status ) ) );
        REQUIRE( decoded.has_value() );
        REQUIRE( *decoded == status );
    }
    REQUIRE_FALSE( missionTaskStatusFromKey( QStringLiteral( "wat" ) ).has_value() );
}

TEST_CASE( "illegal transitions are rejected without mutating or bumping revision",
           "[mission][stage]" )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-1" ) );
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Import ) ).applied );

    const quint64 revisionBefore = timeline.revision();
    const quint64 seqBefore = timeline.lastEventSeq();

    // Pending -> Succeeded is not reachable without running first.
    const MissionOutcome bad = timeline.transition( QStringLiteral( "t1" ),
                                                    MissionTaskStatus::Succeeded,
                                                    QStringLiteral( "2026-09-20T00:00:00Z" ) );
    REQUIRE_FALSE( bad.applied );
    REQUIRE( bad.reason.startsWith( QLatin1String( kMissionErrIllegalTransition ) ) );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Pending );
    REQUIRE( timeline.revision() == revisionBefore );
    REQUIRE( timeline.lastEventSeq() == seqBefore );

    // Unknown task ids fail closed as well.
    const MissionOutcome unknown = timeline.transition( QStringLiteral( "ghost" ),
                                                        MissionTaskStatus::Running,
                                                        QStringLiteral( "2026-09-20T00:00:00Z" ) );
    REQUIRE_FALSE( unknown.applied );
    REQUIRE( unknown.reason == QLatin1String( kMissionErrUnknownTask ) );

    // Duplicate task ids are rejected.
    REQUIRE_FALSE( timeline.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Import ) ).applied );
    REQUIRE_FALSE( timeline.addTask( makeTask( QStringLiteral( "  " ), MissionStage::Import ) ).applied );
}

TEST_CASE( "a task may only enter Running with a run authority recorded", "[mission][stage]" )
{
    MissionTimeline timeline;
    // t1 has no run authority: it may exist, but it may not claim to run.
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Analyze ) ).applied );

    const MissionOutcome noRun = timeline.transition( QStringLiteral( "t1" ),
                                                      MissionTaskStatus::Running,
                                                      QStringLiteral( "2026-09-20T00:00:00Z" ) );
    REQUIRE_FALSE( noRun.applied );
    REQUIRE( noRun.reason == QLatin1String( kMissionErrStaleRun ) );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Pending );

    // t2 carries the TaskCenter id, so the same transition becomes legal.
    MissionTask bound = makeTask( QStringLiteral( "t2" ), MissionStage::Analyze );
    bound.run.kind = QStringLiteral( "task_center" );
    bound.run.id = QStringLiteral( "tc-42" );
    REQUIRE( timeline.addTask( bound ).applied );

    REQUIRE( timeline.transition( QStringLiteral( "t2" ), MissionTaskStatus::Running,
                                  QStringLiteral( "2026-09-20T00:00:00Z" ) ).applied );
    REQUIRE( timeline.task( QStringLiteral( "t2" ) )->status == MissionTaskStatus::Running );
    REQUIRE( timeline.task( QStringLiteral( "t2" ) )->attempts == 1 );
    REQUIRE( timeline.task( QStringLiteral( "t2" ) )->startedIso
             == QStringLiteral( "2026-09-20T00:00:00Z" ) );
}

TEST_CASE( "mission walks import -> publish and reports the current stage", "[mission][stage]" )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-1" ) );
    for ( MissionStage stage : missionStages() )
    {
        MissionTask task = makeTask( QStringLiteral( "t-" ) + QLatin1String( missionStageKey( stage ) ),
                                     stage );
        task.run.kind = QStringLiteral( "task_center" );
        task.run.id = QStringLiteral( "tc-" ) + QLatin1String( missionStageKey( stage ) );
        REQUIRE( timeline.addTask( task ).applied );
    }

    REQUIRE( timeline.currentStage() == MissionStage::Import );

    const auto advance = [&timeline]( const QString &id, MissionTaskStatus to ) {
        return timeline.transition( id, to, QStringLiteral( "2026-09-20T00:00:00Z" ) );
    };

    // One pass over the stages: the current stage is the earliest one that
    // still has an unsettled task, so it must track the walk exactly.
    for ( MissionStage stage : missionStages() )
    {
        const QString id = QStringLiteral( "t-" ) + QLatin1String( missionStageKey( stage ) );
        REQUIRE( timeline.currentStage() == stage );
        REQUIRE( advance( id, MissionTaskStatus::Running ).applied );
        REQUIRE( advance( id, MissionTaskStatus::Succeeded ).applied );
    }
    // Everything settled: the furthest stage that produced a result remains.
    REQUIRE( timeline.currentStage() == MissionStage::Publish );

    // A settled task is terminal: re-running it is rejected, not silently
    // replayed (the state machine never rewinds on its own).
    REQUIRE_FALSE( advance( QStringLiteral( "t-import" ), MissionTaskStatus::Running ).applied );
    REQUIRE( timeline.task( QStringLiteral( "t-import" ) )->status
             == MissionTaskStatus::Succeeded );
}

TEST_CASE( "failed and canceled tasks are retryable and keep their lineage", "[mission][stage]" )
{
    MissionTimeline timeline;
    MissionTask task = makeTask( QStringLiteral( "t1" ), MissionStage::Analyze );
    task.run.kind = QStringLiteral( "task_center" );
    task.run.id = QStringLiteral( "tc-1" );
    REQUIRE( timeline.addTask( task ).applied );

    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t0" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Failed,
                                  QStringLiteral( "t1" ) ).applied );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Failed );

    // Retry rebinds a fresh TaskCenter id and preserves the lineage.
    MissionRunRef secondRun;
    secondRun.kind = QStringLiteral( "task_center" );
    secondRun.id = QStringLiteral( "tc-2" );
    REQUIRE( timeline.retry( QStringLiteral( "t1" ), QStringLiteral( "t2" ), secondRun ).applied );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Pending );
    // A requeue is not an attempt: only Pending -> Running counts.
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->attempts == 1 );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->retryOf == QStringLiteral( "t1" ) );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->run.id == QStringLiteral( "tc-2" ) );

    // Cancel -> resume is the same primitive.
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t3" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Canceled,
                                  QStringLiteral( "t4" ) ).applied );
    REQUIRE( timeline.retry( QStringLiteral( "t1" ), QStringLiteral( "t5" ) ).applied );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->attempts == 2 );

    // A succeeded task is not silently retryable.
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t6" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Succeeded,
                                  QStringLiteral( "t7" ) ).applied );
    const MissionOutcome denied = timeline.retry( QStringLiteral( "t1" ), QStringLiteral( "t8" ) );
    REQUIRE_FALSE( denied.applied );
    REQUIRE( denied.reason.startsWith( QLatin1String( kMissionErrNotRetryable ) ) );
}

TEST_CASE( "stale tasks must be re-bound before they can run again", "[mission][stage]" )
{
    MissionTimeline timeline;
    MissionTask task = makeTask( QStringLiteral( "t1" ), MissionStage::Verify, { QStringLiteral( "layer-1" ) } );
    task.run.kind = QStringLiteral( "task_center" );
    task.run.id = QStringLiteral( "tc-1" );
    REQUIRE( timeline.addTask( task ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t0" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Succeeded,
                                  QStringLiteral( "t1" ) ).applied );

    // The layer disappears -> Stale.
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Stale,
                                  QStringLiteral( "t2" ) ).applied );

    // Stale -> Running is illegal: the references have to be re-bound first.
    const MissionOutcome illegal = timeline.transition( QStringLiteral( "t1" ),
                                                        MissionTaskStatus::Running,
                                                        QStringLiteral( "t3" ) );
    REQUIRE_FALSE( illegal.applied );
    REQUIRE( illegal.reason.startsWith( QLatin1String( kMissionErrIllegalTransition ) ) );

    // Stale -> Pending (re-bind) -> Running is the supported recovery path.
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Pending,
                                  QStringLiteral( "t4" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t5" ) ).applied );
}

TEST_CASE( "deleting a layer marks dependent tasks stale with O(distinct refs) work",
           "[mission][stage]" )
{
    MissionTimeline timeline;
    const QString layerId = QStringLiteral( "layer-1" );
    const QString otherId = QStringLiteral( "layer-2" );
    // Ten tasks all referencing the same two layers.
    for ( int i = 0; i < 10; ++i )
    {
        REQUIRE( timeline.addTask( makeTask( QStringLiteral( "t%1" ).arg( i ),
                                             MissionStage::Analyze,
                                             { layerId, otherId },
                                             {} ) ).applied );
    }

    int resolverCalls = 0;
    MissionRefResolver resolver = [&]( const QString &id ) {
        ++resolverCalls;
        if ( id == layerId )
            return MissionRefStatus{ false, QStringLiteral( "deleted_layer" ) };
        return MissionRefStatus{ true, QString() };
    };

    const MissionReconciliation rec = reconcileMission( timeline, resolver );
    REQUIRE( rec.hasIssues() );
    REQUIRE( rec.danglingRefIds == QStringList{ layerId } );
    REQUIRE( rec.staleTaskIds.size() == 10 );
    // Distinct refs only: 2 calls, not 20.
    REQUIRE( resolverCalls == 2 );

    const int moved = applyReconciliation( timeline, rec, QStringLiteral( "t9" ) );
    REQUIRE( moved == 10 );
    for ( const MissionTask &t : timeline.tasks() )
        REQUIRE( t.status == MissionTaskStatus::Stale );
}

TEST_CASE( "renaming a layer rewrites references without changing status or attempts",
           "[mission][stage]" )
{
    MissionTimeline timeline;
    MissionTask task = makeTask( QStringLiteral( "t1" ), MissionStage::Import,
                                 { QStringLiteral( "layer-old" ) },
                                 { QStringLiteral( "artifact-1" ) } );
    task.run.kind = QStringLiteral( "task_center" );
    task.run.id = QStringLiteral( "tc-1" );
    REQUIRE( timeline.addTask( task ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t0" ) ).applied );
    const int attemptsBefore = timeline.task( QStringLiteral( "t1" ) )->attempts;

    QVector<MissionRename> renames;
    renames.push_back( { QStringLiteral( "layer-old" ), QStringLiteral( "layer-new" ) } );
    const int rewritten = applyRenames( timeline, renames, QStringLiteral( "t1" ) );
    REQUIRE( rewritten == 1 );

    const MissionTask *after = timeline.task( QStringLiteral( "t1" ) );
    REQUIRE( after->inputRefIds == QStringList{ QStringLiteral( "layer-new" ) } );
    REQUIRE( after->status == MissionTaskStatus::Running );
    REQUIRE( after->attempts == attemptsBefore );
    REQUIRE( after->run.id == QStringLiteral( "tc-1" ) );

    // A rename is not a dangling reference. The resolver stands in for the
    // live project registry, which knows BOTH the renamed input and the
    // output artifact this task produced.
    MissionRefResolver resolver = []( const QString &id ) {
        const bool alive = id == QStringLiteral( "layer-new" )
                           || id == QStringLiteral( "artifact-1" );
        return MissionRefStatus{ alive, QString() };
    };
    const MissionReconciliation rec = reconcileMission( timeline, resolver );
    REQUIRE_FALSE( rec.hasIssues() );
    REQUIRE( rec.staleTaskIds.isEmpty() );
}

TEST_CASE( "mission timeline json round-trips and fails closed", "[mission][stage]" )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-1" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/demo.qgz" ) );
    MissionTask task = makeTask( QStringLiteral( "t1" ), MissionStage::Preprocess,
                                 { QStringLiteral( "layer-1" ) }, { QStringLiteral( "artifact-1" ) } );
    task.run.kind = QStringLiteral( "workflow_run" );
    task.run.id = QStringLiteral( "run-7" );
    REQUIRE( timeline.addTask( task ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "t0" ) ).applied );
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Succeeded,
                                  QStringLiteral( "t1" ) ).applied );

    const QJsonObject doc = timeline.toJson();
    REQUIRE( doc.value( QStringLiteral( "kind" ) ).toString()
             == QLatin1String( kMissionTimelineKind ) );

    MissionTimeline restored;
    QString error;
    REQUIRE( restored.fromJson( doc, &error ) );
    REQUIRE( error.isEmpty() );
    REQUIRE( restored == timeline );
    REQUIRE( restored.toJson() == doc );

    // Wrong kind / version / payloads are rejected, never partially applied.
    MissionTimeline bogus;
    QJsonObject badKind = doc;
    badKind.insert( QStringLiteral( "kind" ), QStringLiteral( "something_else" ) );
    REQUIRE_FALSE( bogus.fromJson( badKind, &error ) );
    REQUIRE( error == QStringLiteral( "unexpected_kind" ) );

    QJsonObject badVersion = doc;
    badVersion.insert( QStringLiteral( "schema_version" ), QStringLiteral( "9.9" ) );
    REQUIRE_FALSE( bogus.fromJson( badVersion, &error ) );

    QJsonObject badStatus = doc;
    QJsonArray tasks = badStatus.value( QStringLiteral( "tasks" ) ).toArray();
    // QJsonArray::operator[] hands back a reference wrapper whose toObject()
    // is a *copy*: the patch only lands if the object is written back.
    QJsonObject first = tasks.at( 0 ).toObject();
    first.insert( QStringLiteral( "status" ), QStringLiteral( "wat" ) );
    tasks.replace( 0, first );
    badStatus.insert( QStringLiteral( "tasks" ), tasks );
    REQUIRE_FALSE( bogus.fromJson( badStatus, &error ) );
}

TEST_CASE( "the timeline keeps ids, never live pointers, across host destruction",
           "[mission][stage]" )
{
    // A stand-in for a QgsMapLayer-like host: the timeline must survive its
    // destruction because nothing holds a pointer to it.
    QPointer<QObject> host = new QObject;
    const QString hostId = QStringLiteral( "layer-owned" );

    MissionTimeline timeline;
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Analyze,
                                         { hostId } ) ).applied );

    delete host;
    REQUIRE( host.isNull() );

    MissionRefResolver resolver = []( const QString & ) {
        return MissionRefStatus{ false, QStringLiteral( "deleted_layer" ) };
    };
    const MissionReconciliation rec = reconcileMission( timeline, resolver );
    REQUIRE( rec.danglingRefIds == QStringList{ hostId } );
    REQUIRE( applyReconciliation( timeline, rec, QStringLiteral( "t1" ) ) == 1 );
    REQUIRE( timeline.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Stale );
}
