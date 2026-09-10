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
    void buildRowActions();
    HistoryEntry *selectedEntry();
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
