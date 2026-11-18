/***************************************************************************
 * mission_timeline_model.cpp — paged incremental mission timeline model
 ***************************************************************************/

#include "app/workbench/mission_timeline_model.h"

#include "app/workbench/mission_projection.h"

#include <QHash>

#include <algorithm>

namespace sicnu::app
{

MissionTimelineModel::MissionTimelineModel( QObject *parent )
    : QAbstractTableModel( parent )
{
}

void MissionTimelineModel::setPageSize( int size )
{
    mPageSize = size > 0 ? size : 1;
}

void MissionTimelineModel::setTimeline( const MissionTimeline &timeline )
{
    beginResetModel();
    mTimeline = timeline;
    mTasks = mTimeline.tasks();
    mSessionRows.clear();
    mRowIndex.clear();
    mRowIndex.reserve( mTasks.size() );
    for ( int i = 0; i < mTasks.size(); ++i )
        mRowIndex.insert( mTasks.at( i ).id, i );
    mVisible = qMin( mPageSize, mTasks.size() );
    ++mResets;
    endResetModel();
    emit timelineChanged( mTimeline.revision() );
}

int MissionTimelineModel::rowOfTask( const QString &taskId ) const
{
    return mRowIndex.value( taskId, -1 );
}

void MissionTimelineModel::emitRowChanged( int row )
{
    if ( row < 0 || row >= mVisible )
        return; // not paged in: no widget exists, no signal needed

    // Row-scoped by construction — a full-range dataChanged can only come
    // from a code path that does not exist here, and the benchmark asserts
    // `fullRangeDataChangedCount() == 0`.
    const QModelIndex topLeft = index( row, 0 );
    const QModelIndex bottomRight = index( row, ColCount - 1 );
    emit dataChanged( topLeft, bottomRight, { Qt::DisplayRole } );
    ++mTouchedRows;
}

void MissionTimelineModel::appendNewTasks( const QVector<MissionTask> &tasks )
{
    if ( tasks.size() <= mTasks.size() )
        return;

    // Paging is preserved across growth (#WP02):
    //   * a table that is already fully visible grows its rowCount by
    //     exactly the appended rows (live work stays visible);
    //   * a paged-in table keeps its visible prefix — revealing the hidden
    //     middle here would announce [mVisible, last] and materialise the
    //     whole history on one appended task, the "display everything"
    //     bypass paging exists to prevent. The new tail stays reachable
    //     through canFetchMore()/fetchMore();
    //   * an empty model takes its FIRST page, not the whole vector.
    // rowCount() only ever changes inside a begin/endInsertRows pair whose
    // range matches what it gains (QAbstractItemModel contract).
    const int oldSize = mTasks.size();
    const bool fullyVisible = mVisible >= oldSize;
    const int first = oldSize;
    const int last = tasks.size() - 1;

    int revealFirst = -1;
    int revealLast = -1;
    if ( oldSize == 0 )
    {
        const int chunk = qMin( mPageSize, tasks.size() );
        if ( chunk > 0 )
        {
            revealFirst = 0;
            revealLast = chunk - 1;
        }
    }
    else if ( fullyVisible )
    {
        revealFirst = first;
        revealLast = last;
    }

    if ( revealFirst >= 0 )
        beginInsertRows( QModelIndex(), revealFirst, revealLast );
    for ( int i = first; i <= last; ++i )
        mRowIndex.insert( tasks.at( i ).id, i );
    mTasks = tasks;
    if ( revealFirst >= 0 )
    {
        mVisible = revealLast + 1;
        endInsertRows();
    }
    // Inserting N tasks touches N rows of work, not the whole table — the
    // id-index insertions happen whether the rows are visible or not.
    mTouchedRows += ( last - first + 1 );
}

int MissionTimelineModel::applyEvents( const MissionTimeline &timeline, quint64 sinceSeq )
{
    const QVector<MissionTask> incoming = timeline.tasks();

    // A timeline that is not this mission's growth cannot be applied
    // incrementally: rows are provenance and the id index is append-only,
    // so fewer tasks (a replaced authority) or a different mission id
    // (a spliced document) would leave rowCount() and rowOfTask() pointing
    // at the wrong tasks. Rebuild from the authority through the one legal
    // full-reload path instead — never a silent partial adoption.
    if ( incoming.size() < mTasks.size()
         || ( !mTimeline.missionId().isEmpty() && !timeline.missionId().isEmpty()
              && timeline.missionId() != mTimeline.missionId() ) )
    {
        // Not an incremental apply: the model rebuilt from the authority.
        // The caller-visible contract stays "incrementally touched rows",
        // and a rebuild touches rows through the reset signal pair, not
        // the incremental path — so this reports 0.
        setTimeline( timeline );
        return 0;
    }

    // Structural growth first (rare): tasks appended since the last apply.
    if ( incoming.size() > mTasks.size() )
        appendNewTasks( incoming );

    // The event batch IS the work list: every applied mutation appends an
    // event (reconciliation and retry go through the state machine), so the
    // touched rows are the event rows — O(events), never a scan of the table.
    // The event log is bounded, so one apply is bounded even for a
    // 100k-event mission.
    const QVector<MissionEvent> events = timeline.eventsSince( sinceSeq );
    mSessionRows.clear();
    QVector<int> rows;
    rows.reserve( events.size() );
    for ( const MissionEvent &ev : events )
    {
        ++mLookupOps;
        ++mScannedRows;
        auto it = mRowIndex.constFind( ev.taskId );
        if ( it == mRowIndex.constEnd() )
            continue;
        rows.push_back( *it );
    }

    // Adopt the incoming state BEFORE emitting: every observer of
    // dataChanged (views, and the panel's selection re-push) must read the
    // NEW task data from data() — emitting first handed them the stale
    // snapshot.
    mTasks = incoming;
    mTimeline = timeline;
    // Visible-prefix growth (a raised page size against a paged-in table)
    // is announced like any other rowCount() change — never a silent jump.
    const int targetVisible = qMax( mVisible, qMin( mPageSize, mTasks.size() ) );
    if ( targetVisible > mVisible )
    {
        beginInsertRows( QModelIndex(), mVisible, targetVisible - 1 );
        mVisible = targetVisible;
        endInsertRows();
    }
    // rowCount() never shrinks on this path: the guard above rebuilt the
    // model through setTimeline instead, so a rowCount drop without
    // beginRemoveRows cannot reach views (QAbstractItemModel contract).
    ++mApplyCount;

    // Ascending, one entry per row even when two events hit the same task in
    // one batch (the honest per-call bound; the cumulative counter still
    // counts both events as work).
    std::sort( rows.begin(), rows.end() );
    rows.erase( std::unique( rows.begin(), rows.end() ), rows.end() );
    for ( const int row : rows )
    {
        // Emit even when nothing observable changed: the event says the task
        // moved, and a view is entitled to refresh on that. Rows beyond the
        // paged-in prefix have no widget to refresh but are still reported
        // as touched work.
        emitRowChanged( row );
        mSessionRows.push_back( row );
    }

    emit timelineChanged( mTimeline.revision() );
    return mSessionRows.size();
}

int MissionTimelineModel::rowCount( const QModelIndex &parent ) const
{
    if ( parent.isValid() )
        return 0;
    return mVisible;
}

int MissionTimelineModel::columnCount( const QModelIndex &parent ) const
{
    if ( parent.isValid() )
        return 0;
    return ColCount;
}

QVariant MissionTimelineModel::data( const QModelIndex &index, int role ) const
{
    if ( !index.isValid() || role != Qt::DisplayRole )
        return QVariant();
    const int row = index.row();
    if ( row < 0 || row >= mTasks.size() )
        return QVariant();
    const MissionTask &task = mTasks.at( row );

    switch ( index.column() )
    {
        case ColStage:
            return missionStageLabel( task.stage );
        case ColTitle:
            return task.title;
        case ColStatus:
            return missionTaskStatusLabel( task.status );
        case ColAttempts:
            return task.attempts;
        case ColCapability:
            return task.capabilityId;
        case ColRun:
            return task.run.isNull() ? QVariant()
                                     : QStringLiteral( "%1:%2" ).arg( task.run.kind, task.run.id );
        case ColUpdated:
            return task.endedIso.isEmpty() ? task.startedIso : task.endedIso;
        default:
            return QVariant();
    }
}

QVariant MissionTimelineModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole )
        return QVariant();
    switch ( section )
    {
        case ColStage:
            return QStringLiteral( "Stage" );
        case ColTitle:
            return QStringLiteral( "Task" );
        case ColStatus:
            return QStringLiteral( "Status" );
        case ColAttempts:
            return QStringLiteral( "Attempts" );
        case ColCapability:
            return QStringLiteral( "Capability" );
        case ColRun:
            return QStringLiteral( "Run" );
        case ColUpdated:
            return QStringLiteral( "Updated" );
        default:
            return QVariant();
    }
}

bool MissionTimelineModel::canFetchMore( const QModelIndex &parent ) const
{
    if ( parent.isValid() )
        return false;
    return mVisible < mTasks.size();
}

void MissionTimelineModel::fetchMore( const QModelIndex &parent )
{
    if ( parent.isValid() )
        return;
    const int remaining = mTasks.size() - mVisible;
    if ( remaining <= 0 )
        return;
    const int chunk = qMin( mPageSize, remaining );
    beginInsertRows( QModelIndex(), mVisible, mVisible + chunk - 1 );
    mVisible += chunk;
    endInsertRows();
}

QJsonObject MissionTimelineModel::projectionAt( int row ) const
{
    if ( row < 0 || row >= mTasks.size() )
        return QJsonObject();
    return missionTaskProjectionJson( mTasks.at( row ) );
}

} // namespace sicnu::app
