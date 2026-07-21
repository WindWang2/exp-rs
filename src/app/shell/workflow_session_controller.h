/***************************************************************************
 * workflow_session_controller.h  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#pragma once

#include "workflow/workflow_registry.h"
#include "workflow/workflow_runtime.h"

#include <QObject>
#include <QString>
#include <QStringList>

class TaskPanelHost;

/**
 * Owns process-local workflow registry/runtime and drives TaskPanelHost.
 * Run submits to JobEngine; UI updates arrive via JobEngineQtBridge signals.
 */
class WorkflowSessionController : public QObject
{
    Q_OBJECT
  public:
    explicit WorkflowSessionController( QObject *parent = nullptr );

    void registerBuiltins();
    void bindPanel( TaskPanelHost *panel );

    /**
     * Open a workflow definition, fill the panel from the first operator step
     * schema, and return the new session id (empty on failure).
     */
    QString openTool( const QString &definitionId );

    void setLayerChoices( const QStringList &ids, const QStringList &names );

  public slots:
    void onRunClicked();

  signals:
    void requestLoadRaster( const QString &path );
    void statusMessage( const QString &msg );

  private slots:
    void onJobFinished( const QString &jobId );

  private:
    void ensureRunConnected();
    void ensureJobBridgeConnected();
    void applyJobResultToSession( const std::string &sessionId,
                                  const std::string &stepId,
                                  const Json::Value &result );

    // Member order: registry must outlive runtime (runtime holds a reference).
    sicnu::workflow::WorkflowRegistry m_registry;
    sicnu::workflow::WorkflowRuntime m_runtime;

    QString m_activeSession;
    QString m_activeStepId;
    QString m_activeTitle;
    QString m_pendingJobId;
    bool m_pendingLoadToMap = false;
    TaskPanelHost *m_panel = nullptr;
    QStringList m_layerIds;
    QStringList m_layerNames;
    bool m_builtinsRegistered = false;
    bool m_runConnected = false;
    bool m_jobBridgeConnected = false;
    bool m_runInFlight = false;
};
