/***************************************************************************
 * mission_timeline_model.h — paged, incrementally updated mission timeline
 *
 * Why this exists: the pre-existing processing history model caps rows at
 * 5000 (`processing_history_model.h:143`) and has no incremental path, so a
 * long mission rebuilt the whole view on every progress event. This model is
 * the Track's answer to Oracle O4:
 *
 *   - `setTimeline()` is the only full (reset) path and happens once per
 *     project load;
 *   - `applyEvents()` consumes `eventsSince(seq)` and emits `dataChanged`
 *     for the affected rows only — one row per event, independent of how
 *     many tasks the mission holds;
 *   - `canFetchMore()/fetchMore()` page the history so a 2000-event mission
 *     never materialises 2000 widgets.
 *
 * The instrumentation counters (`resetCount`, `fullRangeDataChangedCount`,
 * `touchedRows`) exist so the benchmark can *prove* the O(1)-per-event claim
 * instead of trusting a wall-clock number.
 *
 * Qt Core only (QAbstractTableModel lives in Qt Core); no QGIS, no Widgets.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_stage.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace sicnu::app
{

/// True when every event after @p sinceSeq is still retained by @p timeline,
/// i.e. applyEvents( timeline, sinceSeq ) would see every mutation since the
/// cursor. The event log is bounded: a cursor that predates the retained
/// window (long agent burst against a stale GUI) can only be served by a
/// full setTimeline() — applyEvents would adopt the fresh task data while
/// repainting just the tail, leaving the rest of the table stale.
inline bool missionTimelineCursorRetained( const MissionTimeline &timeline, quint64 sinceSeq )
{
    return !timeline.eventsTruncated() || sinceSeq + 1 >= timeline.firstRetainedEventSeq();
}

class MissionTimelineModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColStage = 0,
        ColTitle,
        ColStatus,
        ColAttempts,
        ColCapability,
        ColRun,
        ColUpdated,
        ColCount
    };

    explicit MissionTimelineModel( QObject *parent = nullptr );

    // --- data ---
    /// Full (re)load. This is the only path that resets the model.
    void setTimeline( const MissionTimeline &timeline );
    const MissionTimeline &timeline() const { return mTimeline; }

    /// Incremental update: applies events with seq > @p sinceSeq from
    /// @p timeline and emits row-scoped `dataChanged`. Never resets.
    /// Returns the number of rows touched.
    int applyEvents( const MissionTimeline &timeline, quint64 sinceSeq );

    /// Row index of a task id, or -1 when unknown.
    int rowOfTask( const QString &taskId ) const;

    /// Rows reported as changed by the previous applyEvents() call, ascending.
    /// The benchmark uses size() as the per-call work bound.
    const QVector<int> &lastTouchedRows() const { return mSessionRows; }

    /// Rows this model ITERATED over inside applyEvents(). This is the
    /// anti-regression twin of lookupOps(): the touched-row set is derived
    /// from the event batch directly (O(events)), so a change that REPLACES
    /// that derivation with a whole-table diff shows up as scannedRows
    /// growing with the mission size instead of with the event count. A
    /// scan ADDED after the event loop is caught by the wall-clock ceiling
    /// in the scale benchmark, not by this counter.
    long long scannedRows() const { return mScannedRows; }

    /// Projection of a single row — the exact bytes the MCP `mission:timeline`
    /// tool returns for the same task (surface parity by construction).
    QJsonObject projectionAt( int row ) const;

    // --- QAbstractTableModel ---
    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;
    bool canFetchMore( const QModelIndex &parent ) const override;
    void fetchMore( const QModelIndex &parent ) override;

    // --- paging / instrumentation ---
    int pageSize() const { return mPageSize; }
    void setPageSize( int size );

    int resetCount() const { return mResets; }
    /// Structural tripwire: the model has no code path that emits a
    /// table-wide dataChanged (emitRowChanged is the only emission point and
    /// is row-scoped), so this stays 0 unless such a path is introduced —
    /// in which case the new path must increment it here and the benchmark's
    /// == 0 assertions turn the introduction into a failure.
    int fullRangeDataChangedCount() const { return mFullRangeChanges; }
    long long touchedRows() const { return mTouchedRows; }
    int incrementalApplyCount() const { return mApplyCount; }

    /// Row lookups performed by `applyEvents`. This is the benchmark's hard
    /// evidence: a per-event full scan would make this grow with the number of
    /// tasks instead of with the number of events.
    long long lookupOps() const { return mLookupOps; }

signals:
    void timelineChanged( quint64 revision );

private:
    void emitRowChanged( int row );
    void appendNewTasks( const QVector<MissionTask> &tasks );

    /// Rows touched by the current applyEvents() call, ascending. Derived
    /// directly from the event batch: every mutation that changes a task
    /// appends an event (reconciliation, retry and rename all go through the
    /// state machine), so the events are the complete work list — the model
    /// never scans rows whose task did not move. Rows are never removed
    /// (positions are stable and the history is provenance), so no index has
    /// to move.
    QVector<int> mSessionRows;

    MissionTimeline mTimeline;
    QVector<MissionTask> mTasks;   ///< in insertion order; index == row
    QHash<QString, int> mRowIndex; ///< task id -> row, maintained incrementally
    int mVisible = 0;              ///< paged-in prefix of mTasks
    int mPageSize = 200;
    int mResets = 0;
    int mFullRangeChanges = 0;
    long long mTouchedRows = 0;
    long long mLookupOps = 0;
    long long mScannedRows = 0;
    int mApplyCount = 0;
};

} // namespace sicnu::app
