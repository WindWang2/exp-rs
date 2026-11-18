/***************************************************************************
 * tests/test_r33_t05_mission_paging.cpp — R33-T05 WP02/WP03 model gates.
 *
 * The paged timeline model's structural paths must obey the
 * QAbstractItemModel contract: rowCount() only changes inside a
 * begin/end pair whose announced range matches the change, hidden rows
 * never become visible without notification, and the last row stays
 * reachable through canFetchMore()/fetchMore().
 *
 * RowContractWatcher pins that contract signal by signal (insert ranges
 * vs rowCount deltas, no removals, no dataChanged outside the live row
 * range).
 *
 * QAbstractItemModelTester stays attached in Fatal mode on every Qt version.
 * Its automatic fetchMore probes are disabled: Qt 6.8 connects runAllTests
 * before its rowsInserted/modelReset bookkeeping, so a probe can insert
 * while changeInFlight still describes the completed outer operation.
 * See probes/r33_t05_modeltester/ for a standalone reproduction and source
 * references. Tests drive fetchMore explicitly, with both the Qt tester
 * and RowContractWatcher checking every emitted signal; no contract check
 * or paging assertion is disabled.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_timeline_model.h"

#include <QAbstractItemModel>
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QMetaObject>
#include <QModelIndex>
#include <QObject>
#include <QPersistentModelIndex>
#include <QString>
#include <QVariant>
#include <QVector>

using namespace sicnu::app;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t05_mission_paging";
char *appArgv[] = { appArgv0, nullptr };

QCoreApplication &ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
    return *QCoreApplication::instance();
}

/// Signal-by-signal row-contract recorder: every insert pair's announced
/// range must match the rowCount() delta exactly, no removal may ever fire
/// (rows are provenance), and dataChanged must stay inside the live rows
/// and outside an open insert window.
class RowContractWatcher : public QObject
{
    Q_OBJECT

public:
    explicit RowContractWatcher( QAbstractItemModel *model ) : m_model( model )
    {
        connect( model, &QAbstractItemModel::rowsAboutToBeInserted, this,
                 &RowContractWatcher::onRowsAboutToBeInserted );
        connect( model, &QAbstractItemModel::rowsInserted, this,
                 &RowContractWatcher::onRowsInserted );
        connect( model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                 [this] { fail( QStringLiteral( "unexpected rowsAboutToBeRemoved" ) ); } );
        connect( model, &QAbstractItemModel::rowsRemoved, this,
                 [this] { fail( QStringLiteral( "unexpected rowsRemoved" ) ); } );
        connect( model, &QAbstractItemModel::modelAboutToBeReset, this,
                 [this] { m_inReset = true; } );
        connect( model, &QAbstractItemModel::modelReset, this, [this] { m_inReset = false; } );
        connect( model, &QAbstractItemModel::dataChanged, this,
                 &RowContractWatcher::onDataChanged );
    }

    bool ok() const { return m_failure.isEmpty(); }
    const QString &failure() const { return m_failure; }
    int insertPairs() const { return m_insertPairs; }

private:
    void fail( const QString &what )
    {
        if ( m_failure.isEmpty() )
            m_failure = what;
    }

    void onRowsAboutToBeInserted( const QModelIndex &parent, int first, int last )
    {
        if ( parent.isValid() )
            return fail( QStringLiteral( "insert under a valid parent" ) );
        if ( m_openInsert )
            return fail( QStringLiteral( "nested beginInsertRows" ) );
        if ( m_inReset )
            return fail( QStringLiteral( "beginInsertRows during a reset" ) );
        if ( first < 0 || last < first )
            return fail( QStringLiteral( "invalid insert range" ) );
        m_openInsert = true;
        m_pendingFirst = first;
        m_pendingLast = last;
        m_rowsBeforeInsert = m_model->rowCount();
        if ( first > m_rowsBeforeInsert )
            fail( QStringLiteral( "insert starts past rowCount" ) );
    }

    void onRowsInserted( const QModelIndex &parent, int first, int last )
    {
        if ( parent.isValid() )
            return fail( QStringLiteral( "rowsInserted under a valid parent" ) );
        if ( !m_openInsert )
            return fail( QStringLiteral( "rowsInserted without beginInsertRows" ) );
        if ( first != m_pendingFirst || last != m_pendingLast )
            fail( QStringLiteral( "rowsInserted range differs from announcement" ) );
        const int expected = m_rowsBeforeInsert + ( last - first + 1 );
        const int actual = m_model->rowCount();
        if ( actual != expected )
            fail( QStringLiteral( "rowCount delta %1 does not match the announced %2 rows" )
                      .arg( actual - m_rowsBeforeInsert )
                      .arg( last - first + 1 ) );
        m_openInsert = false;
        ++m_insertPairs;
    }

    void onDataChanged( const QModelIndex &topLeft, const QModelIndex &bottomRight )
    {
        if ( m_openInsert )
            fail( QStringLiteral( "dataChanged inside an open insert window" ) );
        if ( !topLeft.isValid() || !bottomRight.isValid()
             || topLeft.parent().isValid() || bottomRight.parent().isValid() )
            fail( QStringLiteral( "dataChanged with invalid or non-top-level indexes" ) );
        else if ( topLeft.row() < 0 || bottomRight.row() >= m_model->rowCount()
                  || bottomRight.row() < topLeft.row() )
            fail( QStringLiteral( "dataChanged range outside the live rows" ) );
    }

    QAbstractItemModel *m_model = nullptr;
    bool m_openInsert = false;
    bool m_inReset = false;
    int m_pendingFirst = -1;
    int m_pendingLast = -1;
    int m_rowsBeforeInsert = 0;
    int m_insertPairs = 0;
    QString m_failure;
};

/// Keep all official model checks; only tests, not the tester, drive paging.
struct ModelTesterGuard
{
    QAbstractItemModelTester tester;
    explicit ModelTesterGuard( QAbstractItemModel *model )
        : tester( model, QAbstractItemModelTester::FailureReportingMode::Fatal )
    {
        // All callers attach to an empty model, before setTimeline/applyEvents.
        tester.setUseFetchMore( false );
    }
};

MissionTask makeTask( const QString &id )
{
    MissionTask task;
    task.id = id;
    task.stage = MissionStage::Import;
    task.title = QStringLiteral( "task " ) + id;
    task.capabilityId = QStringLiteral( "rs:spectral_index" );
    return task;
}

MissionTimeline makeMission( const QString &missionId, int taskCount )
{
    MissionTimeline timeline;
    timeline.setMissionId( missionId );
    timeline.setProjectRef( QStringLiteral( "/tmp/paging.qgz" ) );
    for ( int i = 0; i < taskCount; ++i )
        REQUIRE( timeline.addTask( makeTask( QStringLiteral( "task-%1" ).arg( i ) ) ).applied );
    return timeline;
}

/// Fetch pages until the model is fully visible; returns the iterations.
int fetchAll( MissionTimelineModel &model )
{
    int rounds = 0;
    while ( model.canFetchMore( QModelIndex() ) )
    {
        const int before = model.rowCount();
        const QPersistentModelIndex first( model.index( 0, MissionTimelineModel::ColTitle ) );
        const QVariant firstTitle = first.data();
        model.fetchMore( QModelIndex() );
        REQUIRE( model.rowCount() == qMin( before + model.pageSize(), model.timeline().tasks().size() ) );
        REQUIRE( first.isValid() );
        REQUIRE( first.data() == firstTitle );
        REQUIRE_FALSE( model.index( model.rowCount(), 0 ).isValid() );
        ++rounds;
        REQUIRE( rounds < 100000 );
    }
    return rounds;
}

} // namespace

TEST_CASE( "a paged-in table keeps its visible prefix across live appends",
           "[mission][r33t05][paging]" )
{
    ensureApp();
    MissionTimeline timeline = makeMission( QStringLiteral( "m-paged" ), 1000 );
    MissionTimelineModel model;
    ModelTesterGuard tester( &model );
    RowContractWatcher watcher( &model );
    model.setPageSize( 100 );
    model.setTimeline( timeline );
    REQUIRE( model.rowCount() == 100 );
    REQUIRE( model.canFetchMore( QModelIndex() ) );

    const quint64 cursor = timeline.lastEventSeq();
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "task-live" ) ) ).applied );
    const int touched = model.applyEvents( timeline, cursor );

    // The hidden middle (100..999) did not become visible — with or
    // without notification — and the new tail is reachable by paging.
    REQUIRE( model.rowCount() == 100 );
    REQUIRE( model.resetCount() == 1 ); // growth never resets the table
    // The appended task's event still counts as work (its row is tracked),
    // even though no widget exists for it until the page is fetched.
    REQUIRE( touched == 1 );
    REQUIRE( watcher.insertPairs() == 0 ); // no rowCount change -> no insert pair
    REQUIRE( watcher.ok() );
    REQUIRE( model.canFetchMore( QModelIndex() ) );
    fetchAll( model );
    REQUIRE( model.rowCount() == 1001 );
    REQUIRE( model.rowOfTask( QStringLiteral( "task-live" ) ) == 1000 );
    // The last row is reachable and reports the appended task.
    REQUIRE( model.projectionAt( model.rowCount() - 1 )
                 .value( QStringLiteral( "id" ) )
                 .toString() == QStringLiteral( "task-live" ) );
    // 10 paging inserts (100 rows each) + nothing else moved the count.
    REQUIRE( watcher.insertPairs() == 10 );
    REQUIRE( watcher.ok() );
}

TEST_CASE( "a fully visible table reveals appended rows inside the announced range",
           "[mission][r33t05][paging]" )
{
    ensureApp();
    MissionTimeline timeline = makeMission( QStringLiteral( "m-live" ), 5 );
    MissionTimelineModel model;
    ModelTesterGuard tester( &model );
    RowContractWatcher watcher( &model );
    model.setPageSize( 200 );
    model.setTimeline( timeline );
    REQUIRE( model.rowCount() == 5 );
    REQUIRE_FALSE( model.canFetchMore( QModelIndex() ) );

    // Live work stays visible: 3 appended rows, rowCount grows by exactly 3.
    const quint64 cursor = timeline.lastEventSeq();
    for ( int i = 0; i < 3; ++i )
    {
        REQUIRE( timeline.addTask( makeTask( QStringLiteral( "new-%1" ).arg( i ) ) ).applied );
        model.applyEvents( timeline, cursor );
    }
    REQUIRE( model.rowCount() == 8 );
    REQUIRE_FALSE( model.canFetchMore( QModelIndex() ) );
    REQUIRE( model.rowOfTask( QStringLiteral( "new-2" ) ) == 7 );
    // One insert pair per append, each announcing exactly one row.
    REQUIRE( watcher.insertPairs() == 3 );
    REQUIRE( watcher.ok() );

    // A persistent index held across the appends still addresses the same
    // task: rows are provenance and never move.
    const QPersistentModelIndex persistent( model.index( 2, MissionTimelineModel::ColTitle ) );
    REQUIRE( persistent.data( Qt::DisplayRole ).toString() == QStringLiteral( "task task-2" ) );
    const quint64 cursor2 = timeline.lastEventSeq();
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "new-3" ) ) ).applied );
    model.applyEvents( timeline, cursor2 );
    REQUIRE( persistent.isValid() );
    REQUIRE( persistent.data( Qt::DisplayRole ).toString() == QStringLiteral( "task task-2" ) );
    REQUIRE( watcher.ok() );
}

TEST_CASE( "an empty model takes its first page, not the whole vector",
           "[mission][r33t05][paging]" )
{
    ensureApp();
    MissionTimeline timeline = makeMission( QStringLiteral( "m-first" ), 1000 );
    MissionTimelineModel model;
    ModelTesterGuard tester( &model );
    RowContractWatcher watcher( &model );
    model.setPageSize( 100 );
    // No setTimeline: the first data arrives through the incremental path.
    model.applyEvents( timeline, 0 );
    REQUIRE( model.rowCount() == 100 ); // the first page, paging preserved
    REQUIRE( watcher.insertPairs() == 1 ); // exactly one announced insert
    REQUIRE( watcher.ok() );
    REQUIRE( model.canFetchMore( QModelIndex() ) );
    fetchAll( model );
    REQUIRE( model.rowCount() == 1000 );
    REQUIRE( watcher.ok() );
}

TEST_CASE( "a raised page size against a paged table is announced, never silent",
           "[mission][r33t05][paging]" )
{
    ensureApp();
    MissionTimeline timeline = makeMission( QStringLiteral( "m-raise" ), 500 );
    MissionTimelineModel model;
    ModelTesterGuard tester( &model );
    RowContractWatcher watcher( &model );
    model.setPageSize( 50 );
    model.setTimeline( timeline );
    REQUIRE( model.rowCount() == 50 );

    model.setPageSize( 120 );
    REQUIRE( model.rowCount() == 50 ); // the raise alone changes nothing
    const quint64 cursor = timeline.lastEventSeq();
    // A same-status replay is the cheapest legal event: applyEvents then
    // adopts the raised page with a proper insert announcement.
    const MissionTask *task = timeline.task( QStringLiteral( "task-0" ) );
    REQUIRE( task != nullptr );
    REQUIRE( timeline.transition( task->id, task->status, QStringLiteral( "2026-11-18T00:00:00Z" ) )
                 .applied );
    model.applyEvents( timeline, cursor );
    REQUIRE( model.rowCount() == 120 );
    REQUIRE( watcher.insertPairs() == 1 ); // the announced 50..119 raise
    REQUIRE( watcher.ok() );
    REQUIRE( model.canFetchMore( QModelIndex() ) );
}

TEST_CASE( "a shrinking or foreign timeline rebuilds instead of partial adoption",
           "[mission][r33t05][paging][rebuild]" )
{
    ensureApp();
    MissionTimeline a = makeMission( QStringLiteral( "mission-a" ), 10 );
    MissionTimelineModel model;
    ModelTesterGuard tester( &model );
    RowContractWatcher watcher( &model );
    model.setPageSize( 200 );
    model.setTimeline( a );
    REQUIRE( model.rowCount() == 10 );

    SECTION( "fewer tasks rebuild from the authority" )
    {
        MissionTimeline b = makeMission( QStringLiteral( "mission-a" ), 6 );
        model.applyEvents( b, a.lastEventSeq() );
        REQUIRE( model.resetCount() == 2 );
        REQUIRE( model.rowCount() == 6 );
        // The id index was rebuilt too: no stale rows from the old table.
        REQUIRE( model.rowOfTask( QStringLiteral( "task-9" ) ) == -1 );
        REQUIRE( model.rowOfTask( QStringLiteral( "task-5" ) ) == 5 );
        // The rebuild rode the reset signal pair, never a removal.
        REQUIRE( watcher.insertPairs() == 0 );
        REQUIRE( watcher.ok() );
    }

    SECTION( "a different mission id rebuilds even at equal size" )
    {
        MissionTimeline b = makeMission( QStringLiteral( "mission-b" ), 10 );
        model.applyEvents( b, a.lastEventSeq() );
        REQUIRE( model.resetCount() == 2 );
        REQUIRE( model.rowCount() == 10 );
        REQUIRE( model.timeline().missionId() == QStringLiteral( "mission-b" ) );
        REQUIRE( model.rowOfTask( QStringLiteral( "task-0" ) ) == 0 );
        REQUIRE( watcher.ok() );
    }
}

#include "test_r33_t05_mission_paging.moc"
