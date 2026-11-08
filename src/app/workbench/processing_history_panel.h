/***************************************************************************
 * processing_history_panel.h — Workbench 7.0 unified history surface (§C)
 *
 * One queryable view over TaskCenter + WorkflowRunCoordinator (the only
 * execution seams). Actions route through those same seams:
 *   cancel → TaskCenter::cancelTask
 *   rerun  → TaskCenter::enqueueTask (same algorithmId + parameter snapshot)
 *   resume → WorkflowRunCoordinator::resumeRun (interrupted runs only)
 *   open / compare / inspect provenance → signals the shell routes through
 *   its existing Data/Display and inspector seams.
 *
 * TaskCenter/WorkflowRunCoordinator keep their own retention policies; this
 * panel never stores a second copy — every refresh re-queries them and the
 * model cap bounds the projection.
 ***************************************************************************/
#pragma once

#include <QPointer>
#include <qgsdockwidget.h>

#include <QTableView>

#include "processing_history_model.h"

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;

namespace sicnu
{
class TaskCenter;
namespace workflow
{
class WorkflowRunCoordinator;
}
} // namespace sicnu

namespace sicnu::app
{

class ProcessingHistoryPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit ProcessingHistoryPanel( QWidget *parent = nullptr );

    ProcessingHistoryModel *model() const { return m_model; }

    /// Re-query both sources and project them into the model (bounded).
    /// Called by the debounced refresh timer and from tests.
    void refreshNow();

    /// One multi-artifact pick option: the destination path, a label that
    /// shows type/existence, and whether opening it is currently possible
    /// (a vanished artifact renders disabled, never silently re-aimed).
    struct OutputArtifactChoice
    {
        QString path;
        QString label;
        bool exists = false;
        bool supported = true; ///< false → shown for context, never enabled
    };
    /// Pure projection of an entry's artifacts into pick options — the
    /// menu and the tests share this single truth.
    static QVector<OutputArtifactChoice> outputChoicesFor( const HistoryEntry &entry );

  public slots:
    /// Coalesced refresh scheduling (≤250 ms) on TaskCenter/Coordinator churn.
    void scheduleRefresh();

  signals:
    void resultOpenRequested( const QString &path );
    void compareRequested( const QString &pathA, const QString &pathB );
    /// Shell resolves the path → asset and drives the SelectionContext so the
    /// provenance inspector section shows the production chain (§B↔§C link).
    void inspectRequested( const QString &path );
    void resumeRunRequested( const QString &runId );
    /// Workbench 10.0 unified selection source (shell → SelectionContext
    /// selectedWorkflowRunIds). Carries the run ids of the selected
    /// workflow-run rows only; a pure task selection emits an empty list.
    void workflowRunSelectionChanged( const QStringList &runIds );

  private slots:
    void onContextMenu( const QPoint &pos );
    void onFilterChanged();
    void cancelSelected();
    void rerunSelected();
    void openSelectedOutput();
    void compareSelectedOutputs();
    void inspectSelected();
    void resumeSelected();

  private:
    HistoryEntry *selectedEntry();
    /// The ONE row every row-scoped action (cancel/rerun/open/inspect/
    /// resume) targets and updateActionStates() gates on: the current row
    /// when it is part of the selection, else the first selected row. One
    /// rule for enable and act — the enable check may never look at a
    /// different task than the action hits (D2, R29-T04).
    QModelIndex actionTargetRow() const;
    /// Re-selects the captured identity keys after a model reset and
    /// restores the scroll position; identities whose entries vanished are
    /// dropped honestly.
    void restoreSelection( const QStringList &identityKeys, const QString &anchorKey,
                           int verticalScrollValue );
    /// Menu-based artifact pick for multi-output rows (single-output rows
    /// return their one path without interaction). Empty = dismissed.
    QString pickOutputArtifact( const HistoryEntry &entry );
    void updateActionStates();

    ProcessingHistoryModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_stateFilter = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_rerunBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_compareBtn = nullptr;
    QPushButton *m_inspectBtn = nullptr;
    QPushButton *m_resumeBtn = nullptr;
    QPointer<sicnu::TaskCenter> m_taskCenter;
    QPointer<sicnu::workflow::WorkflowRunCoordinator> m_coordinator;
    bool m_refreshScheduled = false;
};

} // namespace sicnu::app
