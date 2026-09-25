// tests/test_mission_runtime_scale.cpp
//
// Mission Runtime 13.0 — scale gate (O10).
//
// Fixture (derived from the repo's realistic mission size, not a synthetic
// maximum): 400 layers, 4000 tasks, 12000+ events. All assertions are
// STRUCTURAL (counters, distinct-ref resolution, bounded payloads) plus one
// generous wall-clock ceiling — no fragile millisecond budgets, so the gate
// stays low-flake on a shared machine.

#include "app/workbench/mission_context.h"
#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_runtime_store.h"
#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_timeline_model.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::app;

namespace
{

constexpr int kLayers = 400;
constexpr int kTasks = 4000;
constexpr int kMinEvents = 10000;

MissionTask makeTask( const QString &id, MissionStage stage, MissionTaskStatus status,
                      const QStringList &inputs, const QStringList &outputs )
{
    MissionTask t;
    t.id = id;
    t.stage = stage;
    t.title = QStringLiteral( "Task %1" ).arg( id );
    t.status = status;
    t.capabilityId = QStringLiteral( "rs:spectral_index" );
    t.inputRefIds = inputs;
    t.outputRefIds = outputs;
    return t;
}

/// 400 layers × 4000 tasks × 12000+ events, built through the real state
/// machine so the event log is genuine.
MissionTimeline buildLargeTimeline()
{
    MissionTimeline tl;
    tl.setMissionId( QStringLiteral( "mission-scale" ) );
    const QVector<MissionStage> stages = missionStages();

    for ( int i = 0; i < kTasks; ++i )
    {
        const MissionStage stage = stages.at( i % stages.size() );
        const QString layerA = QStringLiteral( "layer-%1" ).arg( i % kLayers );
        const QString layerB = QStringLiteral( "layer-%1" ).arg( ( i * 7 + 3 ) % kLayers );
        MissionTask t = makeTask( QStringLiteral( "task-%1" ).arg( i ), stage,
                                  MissionTaskStatus::Pending, { layerA, layerB },
                                  { QStringLiteral( "artifact-%1" ).arg( i ) } );
        tl.addTask( t );

        // Half the tasks execute (bind → Running → terminal); a sixth of
        // those fail and are retried once, which also exercises the retry
        // lineage at scale. kTasks add events + ~6.7k transition events.
        if ( i % 2 == 0 )
        {
            MissionRunRef run;
            run.kind = QStringLiteral( "task_center" );
            run.id = QString::number( 10000 + i );
            tl.bindRunReference( t.id, run );
            tl.transition( t.id, MissionTaskStatus::Running,
                           QStringLiteral( "2026-09-21T00:00:00Z" ) );
            const bool failed = ( i % 6 == 0 );
            tl.transition( t.id, failed ? MissionTaskStatus::Failed
                                        : MissionTaskStatus::Succeeded,
                           QStringLiteral( "2026-09-21T00:01:00Z" ) );
            if ( failed )
            {
                tl.retry( t.id, QStringLiteral( "2026-09-21T00:02:00Z" ) );
                tl.transition( t.id, MissionTaskStatus::Running,
                               QStringLiteral( "2026-09-21T00:03:00Z" ) );
                tl.transition( t.id, MissionTaskStatus::Succeeded,
                               QStringLiteral( "2026-09-21T00:04:00Z" ) );
            }
        }
    }
    return tl;
}

} // namespace

TEST_CASE( "a large mission stays incremental and bounded", "[mission][scale]" )
{
    QElapsedTimer timer;
    timer.start();

    const MissionTimeline timeline = buildLargeTimeline();
    REQUIRE( timeline.tasks().size() == kTasks );
    // #1170: the event log is bounded — the builder appends kMinEvents, the
    // timeline retains the most recent kEventLogBound window with the
    // truncation facts surfaced instead of an unbounded log.
    REQUIRE( timeline.events().size() == MissionTimeline::kEventLogBound );
    REQUIRE( timeline.eventsTruncated() );
    REQUIRE( timeline.firstRetainedEventSeq() > 1 );
    REQUIRE( timeline.lastEventSeq() >= static_cast<quint64>( kMinEvents ) );
    // eventsSince is served from the retained window; a cursor before it
    // gets exactly the window.
    REQUIRE( timeline.eventsSince( 1 ).size() == MissionTimeline::kEventLogBound );

    // ── incremental UI updates: rows touched == rows the events carry ────
    MissionTimelineModel model;
    model.setTimeline( timeline );
    CHECK( model.resetCount() == 1 );
    const long long baselineLookups = model.lookupOps();

    // One applyEvents over the WHOLE log (the catch-up path a client takes
    // after reconnecting): row-scoped updates only — never a full reset and
    // never a full-range dataChanged.
    const int catchupTouched = model.applyEvents( timeline, 0 );
    CHECK( catchupTouched <= kTasks ); // rows, not events × tasks
    // #1170: the log is bounded, so the catch-up window covers the most
    // recent tasks only — the touched set is exactly the tasks the retained
    // events name (the window is dense in seq, hence ~bound/2 tasks).
    CHECK( catchupTouched >= MissionTimeline::kEventLogBound / 4 );
    CHECK( model.resetCount() == 1 );
    CHECK( model.fullRangeDataChangedCount() == 0 );
    CHECK( model.incrementalApplyCount() == 1 );
    // Lookups grow with the number of EVENTS, not with the number of tasks:
    // a per-event linear scan would be O(events × tasks) here.
    CHECK( model.lookupOps() - baselineLookups <= timeline.events().size() * 2 );

    // An incremental batch (the live path after each mutation) touches only
    // the rows the batch's events carry. task-1/3/5 are Pending in the
    // fixture, so a legal batch cancels them.
    MissionTimeline live = timeline;
    for ( const QString &id : { QStringLiteral( "task-1" ), QStringLiteral( "task-3" ),
                                QStringLiteral( "task-5" ) } )
    {
        const MissionOutcome outcome =
            live.transition( id, MissionTaskStatus::Canceled,
                             QStringLiteral( "2026-09-21T01:00:00Z" ) );
        REQUIRE( outcome.applied );
    }
    const int batchTouched = model.applyEvents( live, timeline.lastEventSeq() );
    CHECK( batchTouched == 3 );
    CHECK( model.incrementalApplyCount() == 2 );
    CHECK( model.resetCount() == 1 );
    CHECK( model.fullRangeDataChangedCount() == 0 );

    // ── reconciliation resolves each reference once ──────────────────────
    int resolverCalls = 0;
    const MissionRefResolver countingResolver = [&resolverCalls]( const QString & ) {
        ++resolverCalls;
        return MissionRefStatus{};
    };
    const MissionReconciliation rec = reconcileMission( timeline, countingResolver );
    // 400 layer ids + 4000 artifact ids = 4400 distinct references — NOT
    // 8000 task→ref pairs (each task references two layers).
    CHECK( resolverCalls == kLayers + kTasks );
    CHECK( rec.refs.size() == resolverCalls );

    // ── bounded agent payload ────────────────────────────────────────────
    // The projection is capped by max_items and the event cursor: a 4000-task
    // mission must never produce an unbounded payload. (since_seq = 0 carries
    // the whole log by design — the caller chooses the cursor.)
    const QJsonObject full =
        missionTimelineProjectionJson( timeline, 32, 0 );
    CHECK( full.value( QStringLiteral( "task_count" ) ).toInt() == kTasks );
    CHECK( full.value( QStringLiteral( "task_truncated" ) ).toBool() );
    CHECK( full.value( QStringLiteral( "tasks" ) ).toArray().size() == 32 );

    const QByteArray bounded =
        missionCanonicalJson( missionTimelineProjectionJson(
            timeline, 32, timeline.lastEventSeq() - 10 ) );
    CHECK( bounded.size() < 200 * 1024 ); // comfortably inside the 512 KiB tool budget
    const QJsonObject tail = QJsonDocument::fromJson( bounded ).object();
    CHECK( tail.value( QStringLiteral( "events" ) ).toArray().size() == 10 );
    CHECK( tail.value( QStringLiteral( "tasks" ) ).toArray().size() == 32 );
}

TEST_CASE( "a large mission round-trips through the single authority", "[mission][scale]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "mission-scale.qgz" ) );

    MissionRuntimeState state;
    state.context.missionId = QStringLiteral( "mission-scale" );
    state.timeline = buildLargeTimeline();

    QElapsedTimer timer;
    timer.start();
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    MissionRuntimeState reopened;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), reopened, &err ) );
    CHECK( reopened.authorityLoaded );
    CHECK( reopened.timeline.tasks().size() == kTasks );
    CHECK( reopened.timeline.events().size() == MissionTimeline::kEventLogBound );
    CHECK( reopened.timeline.eventsTruncated() );
    CHECK( reopened.timeline.firstRetainedEventSeq() > 1 );
    CHECK( reopened.timeline == state.timeline );
    // Structural ceiling only (a shared machine may be slow); the equality
    // assertions above are the real gate.
    CHECK( timer.elapsed() < 120000 );

    // A fresh model over the reopened timeline renders without a reset.
    MissionTimelineModel model;
    model.setTimeline( reopened.timeline );
    CHECK( model.resetCount() == 1 );
    CHECK( model.rowCount() == qMin( model.pageSize(), kTasks ) );
}
