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

    /// Row index of a task id, or -1 when unknown / not yet paged in.
    int rowOfTask( const QString &taskId ) const;

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

    MissionTimeline mTimeline;
    QVector<MissionTask> mTasks;   ///< in insertion order; index == row
    QHash<QString, int> mRowIndex; ///< task id -> row, maintained incrementally
    int mVisible = 0;              ///< paged-in prefix of mTasks
    int mPageSize = 200;
    int mResets = 0;
    int mFullRangeChanges = 0;
    long long mTouchedRows = 0;
    long long mLookupOps = 0;
    int mApplyCount = 0;
};

} // namespace sicnu::app
