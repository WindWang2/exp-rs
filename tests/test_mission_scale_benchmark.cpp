// Track glm53-mission-workbench-12 — UI scale benchmark for the mission
// timeline: hundreds of layers, thousands of history events, and a hard
// assertion that an incremental update costs O(events), not O(tasks).
//
// The discriminator is deliberately *not* wall-clock (that would be flaky on a
// shared host): the model counts its own row lookups, so a regression that
// reintroduces a per-event full scan shows up as `lookupOps` scaling with the
// mission size instead of with the event count.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_timeline_model.h"

#include <QElapsedTimer>
#include <QString>
#include <QVector>

using namespace sicnu::app;

namespace
{

struct RunResult
{
    long long touchedRows = 0;
    long long lookupOps = 0;
    int resets = 0;
    int fullRangeChanges = 0;
    qint64 elapsedMs = 0;
};

/// A mission over @p layerCount layers with @p taskCount tasks.
MissionTimeline makeMission( int layerCount, int taskCount )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "bench" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/bench.qgz" ) );

    for ( int i = 0; i < taskCount; ++i )
    {
        MissionTask task;
        task.id = QStringLiteral( "task-%1" ).arg( i );
        task.stage = static_cast<MissionStage>( i % 5 );
        task.title = QStringLiteral( "task %1" ).arg( i );
        task.capabilityId = QStringLiteral( "rs:spectral_index" );
        task.inputRefIds.push_back( QStringLiteral( "layer-%1" ).arg( i % layerCount ) );
        task.run.kind = QStringLiteral( "task_center" );
        task.run.id = QStringLiteral( "tc-%1" ).arg( i );
        (void) timeline.addTask( task );
    }
    return timeline;
}

/// Drives @p eventCount progress events (idempotent status replays, exactly
/// what a long-running TaskCenter task emits) through the incremental path.
RunResult runEvents( const MissionTimeline &base, int taskCount, int eventCount, int pageSize )
{
    MissionTimeline timeline = base;
    MissionTimelineModel model;
    model.setPageSize( pageSize );
    model.setTimeline( timeline );

    QVector<QString> ids;
    ids.reserve( taskCount );
    for ( int i = 0; i < taskCount; ++i )
        ids.push_back( QStringLiteral( "task-%1" ).arg( i ) );

    QElapsedTimer timer;
    timer.start();

    quint64 cursor = timeline.lastEventSeq();
    for ( int i = 0; i < eventCount; ++i )
    {
        const QString &id = ids.at( i % ids.size() );
        const MissionTask *task = timeline.task( id );
        // Same-status replay: legal, audit-only, and the realistic shape of a
        // progress stream.
        (void) timeline.transition( id, task->status, QStringLiteral( "2026-09-20T00:00:00Z" ) );
        const int perCall = model.applyEvents( timeline, cursor );
        cursor = timeline.lastEventSeq();

        // The per-call bound is the thing a user feels. One event must touch
        // at most one row, however large the mission is.
        REQUIRE( perCall <= 1 );
        REQUIRE( model.lastTouchedRows().size() <= 1 );
    }

    RunResult result;
    result.touchedRows = model.touchedRows();
    result.lookupOps = model.lookupOps();
    result.resets = model.resetCount();
    result.fullRangeChanges = model.fullRangeDataChangedCount();
    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace

TEST_CASE( "incremental updates cost O(events), not O(tasks)", "[mission][scale][benchmark]" )
{
    constexpr int kLayerCount = 300;
    constexpr int kEventCount = 2000;

    const MissionTimeline small = makeMission( kLayerCount, 300 );
    const MissionTimeline large = makeMission( kLayerCount, 3000 );

    // Page size above the task count so every row is materialised and the
    // touched-row count is directly comparable between the two fixtures.
    const RunResult smallRun = runEvents( small, 300, kEventCount, 4096 );
    const RunResult largeRun = runEvents( large, 3000, kEventCount, 4096 );

    // One reset for the initial load, never again.
    REQUIRE( smallRun.resets == 1 );
    REQUIRE( largeRun.resets == 1 );

    // No full-range dataChanged anywhere: every update is row-scoped.
    REQUIRE( smallRun.fullRangeChanges == 0 );
    REQUIRE( largeRun.fullRangeChanges == 0 );

    // The actual O(1) claim: 2000 events cost 2000 lookups on a 300-task
    // mission AND on a 3000-task mission. A per-event scan would show ~10x.
    REQUIRE( smallRun.lookupOps == kEventCount );
    REQUIRE( largeRun.lookupOps == kEventCount );
    REQUIRE( smallRun.lookupOps == largeRun.lookupOps );

    // And every event repainted exactly one row.
    REQUIRE( smallRun.touchedRows == kEventCount );
    REQUIRE( largeRun.touchedRows == kEventCount );

    // Generous wall-clock ceiling: catches pathological regressions (a full
    // rebuild per event would blow past this) without being flaky.
    REQUIRE( largeRun.elapsedMs < 60000 );

    WARN( "mission timeline scale: 300 tasks x 2000 events = " << smallRun.elapsedMs
                                                               << " ms, 3000 tasks x 2000 events = "
                                                               << largeRun.elapsedMs << " ms" );
}

TEST_CASE( "history is paged instead of materialised", "[mission][scale][benchmark]" )
{
    const MissionTimeline timeline = makeMission( 300, 1200 );

    MissionTimelineModel model;
    model.setPageSize( 200 );
    model.setTimeline( timeline );

    REQUIRE( model.rowCount() == 200 );
    REQUIRE( model.canFetchMore( QModelIndex() ) );

    model.fetchMore( QModelIndex() );
    REQUIRE( model.rowCount() == 400 );
    REQUIRE( model.canFetchMore( QModelIndex() ) );

    // Paging never touches rows: it inserts, it does not repaint.
    REQUIRE( model.fullRangeDataChangedCount() == 0 );
    REQUIRE( model.touchedRows() == 0 );
}
