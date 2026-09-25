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
    long long scannedRows = 0;
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
    result.scannedRows = model.scannedRows();
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

    // …and each event scanned exactly one row. This is the bound the
    // lookupOps counter cannot express on its own: an applyEvents that
    // diffs the whole task table keeps lookupOps at O(events) while making
    // the real work O(tasks x events).
    REQUIRE( smallRun.scannedRows == kEventCount );
    REQUIRE( largeRun.scannedRows == kEventCount );

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

namespace {

/// The O(events)-not-O(tasks x events) contract, parameterized so the
/// always-on suite proves it at 10^4 events while the 10^5-event campaign
/// profile stays opt-in (SICNU_MISSION_SCALE_100K=1 — same pattern as
/// SICNU_LAB_SCALE_1000): at ~5.5 ms/event in a Debug build the full 10^5
/// run costs ~9 minutes, dominated by Qt COW snapshot detaches that scale
/// with the task count, not by any per-event table scan.
void runScaleContract( int taskCount, int eventCount, qint64 ceilingMs )
{
    constexpr int kLayerCount = 300;

    const MissionTimeline base = makeMission( kLayerCount, taskCount );
    // Page size above the task count so every row is paged in and the
    // touched-row count is directly comparable with the event count.
    const RunResult run = runEvents( base, taskCount, eventCount, taskCount + 1 );

    // One reset (the initial load), never again — model reset stays a
    // per-project-load event, not a per-update fallback.
    REQUIRE( run.resets == 1 );
    REQUIRE( run.fullRangeChanges == 0 );

    // O(1) per event on both counters, independent of the task count.
    REQUIRE( run.lookupOps == eventCount );
    REQUIRE( run.scannedRows == eventCount );
    REQUIRE( run.touchedRows == eventCount );

    // Pathological-regression ceiling, NOT a performance claim: the counter
    // assertions above are the algorithmic oracle (O(1) work per event).
    // The wall-clock constant is dominated by Qt COW detaches of the shared
    // task vector on every authority mutation — value-type semantics that
    // scale with the task count even though the WORK per event does not. A
    // reintroduced per-event task scan multiplies that constant by the task
    // count again and lands far beyond any ceiling chosen here.
    REQUIRE( run.elapsedMs < ceilingMs );
    WARN( "mission timeline scale: " << taskCount << " tasks x " << eventCount << " events = "
          << run.elapsedMs << " ms (" << ( double( run.elapsedMs ) / eventCount )
          << " ms/event)" );
}

} // namespace

TEST_CASE( "10k progress events over a 10k-task mission cost O(events), not O(tasks x events)",
           "[mission][scale][benchmark]" )
{
    // Always-on contract: one event per apply (the shape of a live TaskCenter
    // progress stream) costs one lookup and one scanned row — the pre-index
    // implementations did a full task-vector pass per event here.
    runScaleContract( 10000, 10000, 300000 );
}

TEST_CASE( "100k-event campaign profile (opt-in: SICNU_MISSION_SCALE_100K=1)",
           "[mission][scale][benchmark][.integration]" )
{
    // The 10^5-event leg of the contract: same assertions, ten times the
    // events. Opt-in because a Debug build pays ~5.5 ms/event in COW
    // snapshot costs (~9 minutes); the number itself is the deliverable of
    // the profile run and is printed below.
    if ( qEnvironmentVariableIsEmpty( "SICNU_MISSION_SCALE_100K" ) )
    {
        WARN( "skipping the 100k-event profile; set SICNU_MISSION_SCALE_100K=1 to run it" );
        SKIP();
    }
    runScaleContract( 10000, 100000, 900000 );
}

TEST_CASE( "append-heavy missions keep row identity and paging", "[mission][scale][selection]" )
{
    // Rows are provenance: appending tasks must never move, rename or lose
    // an existing row's identity — a view selection bound to a row keeps
    // addressing the same task, and every id stays resolvable through the
    // incremental path (no reset).
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "bench" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/bench.qgz" ) );

    constexpr int kInitial = 2000;
    for ( int i = 0; i < kInitial; ++i )
    {
        MissionTask task;
        task.id = QStringLiteral( "task-%1" ).arg( i );
        task.title = QStringLiteral( "task %1" ).arg( i );
        REQUIRE( timeline.addTask( task ).applied );
    }

    MissionTimelineModel model;
    model.setPageSize( 200 );
    model.setTimeline( timeline );
    REQUIRE( model.resetCount() == 1 );
    REQUIRE( model.rowCount() == 200 ); // paged-in prefix only
    REQUIRE( model.canFetchMore( QModelIndex() ) );

    // Selection identity before growth: row 5 is task-5 by id.
    REQUIRE( model.projectionAt( 5 ).value( QStringLiteral( "id" ) ).toString()
             == QStringLiteral( "task-5" ) );

    quint64 cursor = timeline.lastEventSeq();
    constexpr int kAppended = 3000;
    for ( int i = 0; i < kAppended; ++i )
    {
        MissionTask task;
        task.id = QStringLiteral( "task-%1" ).arg( kInitial + i );
        task.title = QStringLiteral( "task %1" ).arg( kInitial + i );
        REQUIRE( timeline.addTask( task ).applied );
        model.applyEvents( timeline, cursor );
        cursor = timeline.lastEventSeq();
    }

    // No reset happened; the pre-existing rows kept their positions and ids.
    REQUIRE( model.resetCount() == 1 );
    REQUIRE( model.rowOfTask( QStringLiteral( "task-5" ) ) == 5 );
    REQUIRE( model.rowOfTask( QStringLiteral( "task-1999" ) ) == 1999 );
    REQUIRE( model.projectionAt( 5 ).value( QStringLiteral( "id" ) ).toString()
             == QStringLiteral( "task-5" ) );
    // New tasks are addressable by id. By design (documented in the model
    // header) appended in-flight tasks become visible immediately — paging
    // governs the bulk load, not live appends — so the row count grew to the
    // full task count without a second reset.
    REQUIRE( model.rowOfTask( QStringLiteral( "task-4999" ) ) == kInitial + kAppended - 1 );
    REQUIRE( model.rowCount() == kInitial + kAppended );
    REQUIRE_FALSE( model.canFetchMore( QModelIndex() ) );
    // Each append counts twice in the work ledger — one inserted row plus
    // the row emission of its task_added event — and no repaint existed
    // anywhere (dataChanged stays row-scoped, inserts are not repaints).
    REQUIRE( model.touchedRows() == 2 * kAppended );
    REQUIRE( model.fullRangeDataChangedCount() == 0 );
}

TEST_CASE( "a 10k-task reload resets once and pages, and ids survive the reset",
           "[mission][scale][reset]" )
{
    const MissionTimeline timeline = makeMission( 300, 10000 );

    MissionTimelineModel model;
    model.setPageSize( 200 );
    model.setTimeline( timeline );
    REQUIRE( model.resetCount() == 1 );
    REQUIRE( model.rowCount() == 200 );
    REQUIRE( model.canFetchMore( QModelIndex() ) );

    // Identity is bound to the stable task id, not to a stale row cache:
    // after a full reload the same id resolves to the same task.
    model.setTimeline( timeline );
    REQUIRE( model.resetCount() == 2 );
    REQUIRE( model.rowOfTask( QStringLiteral( "task-4242" ) ) == 4242 );
    REQUIRE( model.projectionAt( 4242 ).value( QStringLiteral( "id" ) ).toString()
             == QStringLiteral( "task-4242" ) );
    REQUIRE( model.canFetchMore( QModelIndex() ) );
}

TEST_CASE( "a cursor outside the retained event window is detected",
           "[mission][scale][truncation]" )
{
    // The bounded log is what keeps a 100k-event mission cheap; the cost is
    // that applyEvents() cannot serve a cursor older than the retained
    // window. missionTimelineCursorRetained is the decision the shell must
    // consult before choosing the incremental path: stale cursor -> full
    // reload, fresh cursor -> incremental.
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "bench" ) );
    MissionTask task;
    task.id = QStringLiteral( "t1" );
    REQUIRE( timeline.addTask( task ).applied );

    // Below the bound nothing is truncated: any cursor is servable.
    REQUIRE_FALSE( timeline.eventsTruncated() );
    REQUIRE( missionTimelineCursorRetained( timeline, 0 ) );

    // Drive the log past kEventLogBound so the window truncates.
    for ( int i = 0; i < 5000; ++i )
        (void) timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Pending,
                                    QStringLiteral( "2026-09-25T00:00:00Z" ), "replay" );
    REQUIRE( timeline.eventsTruncated() );
    REQUIRE( timeline.firstRetainedEventSeq() > 1 );

    // The event at seq F-1 (F = first retained) is gone, so a cursor of F-2
    // cannot be served: its post-cursor set [F-1..last] needs a lost event.
    // Derived from the actual window so the oracle survives a change of
    // kEventLogBound.
    const quint64 staleCursor = timeline.firstRetainedEventSeq() - 2;
    REQUIRE_FALSE( missionTimelineCursorRetained( timeline, staleCursor ) );
    // F-1 itself is servable again: [F..last] is exactly the retained set.
    REQUIRE( missionTimelineCursorRetained( timeline, timeline.firstRetainedEventSeq() - 1 ) );
    // And the fresh cursor saw everything.
    REQUIRE( missionTimelineCursorRetained( timeline, timeline.lastEventSeq() ) );
}
