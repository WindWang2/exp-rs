/***************************************************************************
 * processing_history_model.h — Workbench 7.0 unified processing history (§C)
 *
 * One queryable projection over every processing surface: TaskCenter tasks
 * (source-tagged gui / agent / mcp / cli / workflow) and WorkflowRunCoordinator
 * runs (including Interrupted crash recoveries). The model is a bounded,
 * value-typed view — it owns NO execution state, never re-derives results,
 * and drops the oldest rows past its cap while reporting exactly how many
 * were dropped (truthful truncation, no silent loss).
 *
 * Scaling contract (goal §H): raw entries are capped (most recent wins),
 * filtering/search run over the capped set, data() is O(1) per cell, and no
 * widget exists per row (QTableView renders this model).
 ***************************************************************************/
#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariantMap>

#include "processing/framework/task_center.h"
#include "workflow/workflow_run.h"

namespace sicnu::app
{

// ── Truthful state projection (shared by the panel and its tests) ──────────
inline QString historyTaskStateText( sicnu::TaskStatus status )
{
    switch ( status )
    {
        case sicnu::TaskStatus::Queued:
            return QObject::tr( "排队中" );
        case sicnu::TaskStatus::Running:
            return QObject::tr( "运行中" );
        case sicnu::TaskStatus::Paused:
            return QObject::tr( "已暂停" );
        case sicnu::TaskStatus::Completed:
            return QObject::tr( "已完成" );
        case sicnu::TaskStatus::Failed:
            return QObject::tr( "失败" );
        case sicnu::TaskStatus::Canceled:
            return QObject::tr( "已取消" );
        case sicnu::TaskStatus::WaitingResource:
            return QObject::tr( "等待资源" );
        case sicnu::TaskStatus::Dispatching:
            return QObject::tr( "调度中" );
    }
    return QObject::tr( "未知" );
}

inline bool historyTaskTerminal( sicnu::TaskStatus status )
{
    switch ( status )
    {
        case sicnu::TaskStatus::Completed:
        case sicnu::TaskStatus::Failed:
        case sicnu::TaskStatus::Canceled:
            return true;
        default:
            return false;
    }
}

inline QString historyRunStateText( sicnu::workflow::WorkflowRunState state )
{
    switch ( state )
    {
        case sicnu::workflow::WorkflowRunState::Created:
            return QObject::tr( "已创建" );
        case sicnu::workflow::WorkflowRunState::Planning:
            return QObject::tr( "规划中" );
        case sicnu::workflow::WorkflowRunState::Ready:
            return QObject::tr( "就绪" );
        case sicnu::workflow::WorkflowRunState::Running:
            return QObject::tr( "运行中" );
        case sicnu::workflow::WorkflowRunState::WaitingResource:
            return QObject::tr( "等待资源" );
        case sicnu::workflow::WorkflowRunState::Interrupted:
            return QObject::tr( "已中断（可恢复）" );
        case sicnu::workflow::WorkflowRunState::Cancelling:
            return QObject::tr( "取消中" );
        case sicnu::workflow::WorkflowRunState::Canceled:
            return QObject::tr( "已取消" );
        case sicnu::workflow::WorkflowRunState::Failed:
            return QObject::tr( "失败" );
        case sicnu::workflow::WorkflowRunState::Completed:
            return QObject::tr( "已完成" );
    }
    return QObject::tr( "未知" );
}

inline bool historyRunResumable( sicnu::workflow::WorkflowRunState state )
{
    return state == sicnu::workflow::WorkflowRunState::Interrupted;
}

struct HistoryEntry
{
    enum class Kind
    {
        Task,
        WorkflowRun
    };
    Kind kind = Kind::Task;

    long taskId = -1;      ///< Task rows (TaskCenter identity)
    QString runId;         ///< Workflow-run rows (coordinator identity)
    QString title;         ///< algorithm display name / workflow id
    QString source;        ///< gui | agent | mcp | cli | workflow | …
    QString stateText;     ///< truthful, already-localized state string
    bool running = false;  ///< drives cancel/resume affordances
    bool resumable = false;///< interrupted workflow run
    bool failed = false;
    double progress = -1.0;///< 0..100, negative = not applicable
    QDateTime started;
    QDateTime ended;
    QStringList outputPaths;
    QVariantMap params;    ///< rerun snapshot (task rows)
    QString algorithmId;   ///< rerun seam (task rows)
};

class ProcessingHistoryModel : public QAbstractTableModel
{
    Q_OBJECT
  public:
    enum Column
    {
        Title = 0,
        Source,
        State,
        Progress,
        Started,
        Duration,
        Output,
        ColumnCount
    };

    /// Hard cap on retained rows (most recent win). Bounded memory, bounded
    /// paint cost — the UI can never grow without bound no matter what the
    /// execution plane did (goal §H).
    static constexpr int kMaxRows = 5000;

    explicit ProcessingHistoryModel( QObject *parent = nullptr );

    /// Full bounded replace + refilter. Call after re-querying the
    /// authoritative sources; the model stays a pure projection.
    void setEntries( const QVector<HistoryEntry> &entries );

    /// How many newest-beyond-cap rows setEntries() has dropped so far.
    long long droppedCount() const { return m_droppedCount; }

    // ── Filtering (incremental, over the capped set) ────────────────────
    void setStateFilter( const QString &stateSubstring );
    void setSearchText( const QString &textSubstring );
    QString stateFilter() const { return m_stateFilter; }
    QString searchText() const { return m_searchText; }

    /// Row projection accessor for actions (nullptr when out of range).
    const HistoryEntry *entryAtRow( int row ) const;

    // QAbstractTableModel
    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;

  signals:
    /// Emitted when entries were dropped by the cap — the panel surfaces the
    /// count so truncation is visible, never silent.
    void droppedCountChanged( long long dropped );

  private:
    void refilter();

    QVector<HistoryEntry> m_entries; ///< capped, newest first
    QVector<int> m_visible;          ///< indices into m_entries passing filters
    QString m_stateFilter;
    QString m_searchText;
    long long m_droppedCount = 0;
};

} // namespace sicnu::app
