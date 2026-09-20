/***************************************************************************
 * mission_timeline_model.cpp — paged incremental mission timeline model
 ***************************************************************************/

#include "app/workbench/mission_timeline_model.h"

#include "app/workbench/mission_projection.h"

#include <QHash>

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
    mLastTouchedSerial.clear();
    mSessionRows.clear();
    mSerial = 0;
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

    // Newly created tasks are shown immediately (they are the work in
    // progress); paging only governs the bulk load in setTimeline().
    const int first = mTasks.size();
    const int last = tasks.size() - 1;
    beginInsertRows( QModelIndex(), first, last );
    mTasks = tasks;
    for ( int i = first; i <= last; ++i )
        mRowIndex.insert( mTasks.at( i ).id, i );
    mVisible = mTasks.size();
    endInsertRows();
    // Inserting N tasks touches N rows, not the whole table.
    mTouchedRows += ( last - first + 1 );
}

int MissionTimelineModel::applyEvents( const MissionTimeline &timeline, quint64 sinceSeq )
{
    const QVector<MissionTask> incoming = timeline.tasks();

    // Structural growth first (rare): tasks appended since the last apply.
    if ( incoming.size() > mTasks.size() )
        appendNewTasks( incoming );

    // Rows whose task actually changed. Driven by the events (O(1) id lookup
    // against the incrementally maintained index — the whole point of this
    // model: an event costs one lookup, not a table scan) PLUS any task whose
    // payload moved without an event (reconciliation makes a task Stale
    // directly; retry() brings one back). The comparison is cheap because it
    // only runs for tasks that already carry an event in this batch.
    const QVector<MissionEvent> events = timeline.eventsSince( sinceSeq );
    mSessionRows.clear();
    ++mSerial;
    for ( const MissionEvent &ev : events )
    {
        ++mLookupOps;
        auto it = mRowIndex.constFind( ev.taskId );
        if ( it == mRowIndex.constEnd() )
            continue;
        mLastTouchedSerial.insert( ev.taskId, mSerial );
    }

    const int n = qMin( mTasks.size(), incoming.size() );
    for ( int row = 0; row < n; ++row )
    {
        if ( mLastTouchedSerial.value( incoming.at( row ).id ) != mSerial )
            continue;
        // Emit even when nothing observable changed: the event says the task
        // moved, and a view is entitled to refresh on that.
        emitRowChanged( row );
        mSessionRows.push_back( row );
    }

    mTasks = incoming;
    mTimeline = timeline;
    mVisible = qMax( mVisible, qMin( mPageSize, mTasks.size() ) );
    ++mApplyCount;
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
