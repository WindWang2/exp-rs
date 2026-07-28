/***************************************************************************
 * workflow_session_controller.h  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#pragma once

#include "workflow/workflow_registry.h"
#include "workflow/workflow_runtime.h"
#include "processing/framework/task_center.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace sicnu::data {
class DataManager;
struct TemporaryReapResult;
}

namespace sicnu::workflow::gui {
class PipelineCanvasWidget;
}

class TaskPanelHost;

/**
 * Owns process-local workflow registry/runtime and drives TaskPanelHost and PipelineCanvasWidget.
 * Run submits through Task Center; UI updates arrive from the owning task.
 */
class WorkflowSessionController : public QObject
{
    Q_OBJECT
  public:
    explicit WorkflowSessionController( QObject *parent = nullptr );

    void registerBuiltins();
    void bindPanel( TaskPanelHost *panel );
    void bindCanvas( sicnu::workflow::gui::PipelineCanvasWidget *canvas );
    void setDataManager( sicnu::data::DataManager *dataManager );
    sicnu::data::DataManager *dataManager() const { return m_dataManager; }

    sicnu::data::TemporaryReapResult reapTaskTemporaries();

    /**
     * Open a workflow definition, fill the panel from the first operator step
     * schema, and return the new session id (empty on failure).
     */
    QString openTool( const QString &definitionId );

    void setLayerChoices( const QStringList &ids, const QStringList &names );

  public slots:
    void onRunClicked();
    void runFullWorkflow();
    void runUpToNode( const QString &targetStepId );
    void stopWorkflow();

  signals:
    void requestLoadRaster( const QString &path );
    void statusMessage( const QString &msg );
    /** Workspace-hosted labs (e.g. lab.obia → open OBIA window). */
    void requestOpenWorkspace( const QString &workspaceKind );
    void stepStatusChanged( const QString &stepId, const QString &statusStr );
    void showLogsRequested( const QString &stepId );

  private slots:
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    void ensureRunConnected();
    void applyJobResultToSession( const std::string &sessionId,
                                  const std::string &stepId,
                                  const Json::Value &result );

    // Member order: registry must outlive runtime (runtime holds a reference).
    sicnu::workflow::WorkflowRegistry m_registry;
    sicnu::workflow::WorkflowRuntime m_runtime;

    QString m_activeSession;
    QString m_activeStepId;
    QString m_activeTitle;
    long m_pendingTaskId = -1;
    long m_activePipelineId = -1;
    bool m_pendingLoadToMap = false;
    TaskPanelHost *m_panel = nullptr;
    sicnu::workflow::gui::PipelineCanvasWidget *m_canvas = nullptr;
    sicnu::data::DataManager *m_dataManager = nullptr;

    QStringList m_layerIds;
    QStringList m_layerNames;
    bool m_builtinsRegistered = false;
    bool m_runConnected = false;
    bool m_runInFlight = false;
};

