/***************************************************************************
 * workflow_session_controller.h  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#pragma once

#include "workflow/workflow_runtime.h"
#include "processing/framework/task_center.h"

#include <QObject>
#include <QPointer>
#include <vector>
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

    /**
     * Workbench 10.0: open a bare rs: operator as a ONE-STEP workflow in the
     * TaskPanelHost (WP-F catalog entry point). Registers the synthesized
     * definition in the runtime, then rides the normal session path — the
     * run seam stays TaskCenter, never a second executor.
     */
    QString openBareOperator( const QString &operatorId );

    void setLayerChoices( const QStringList &ids, const QStringList &names );

    bool isRunInFlight() const { return m_runInFlight; }
    QString activeSessionId() const { return m_activeSession; }

    /// R28-T04 (R2 F-5, test seam): completed step ids of a session (empty
    /// for an unknown session) — pins data attribution of late results.
    std::vector<QString> completedStepIds( const QString &sessionId ) const;    QString activeStepId() const { return m_activeStepId; }
    long activePipelineId() const { return m_activePipelineId; }
    long pendingTaskId() const { return m_pendingTaskId; }
    void cancelActiveRun();

  public slots:
    void onRunClicked();
    void runFullWorkflow();
    void runUpToNode( const QString &targetStepId );
    void stopWorkflow();
    /// Workbench 10.0: canvas node selection drives the step parameter form
    /// (TaskPanelHost stays the single schema-form surface).
    void selectStep( const QString &stepId );

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
    /**
     * swarm-review APP-R-2: push the canvas definition into the runtime so a
     * canvas Run executes what the canvas shows. Sessions snapshot their
     * definition at open() (immutable afterwards), so a canvas edit only
     * reaches the engine by (re)opening the session on the exported
     * definition. Returns false with @p reason set when the canvas provides
     * nothing runnable and no session is open.
     */
    bool syncCanvasDefinitionForRun( QString *reason );
    void applyJobResultToSession( const std::string &sessionId,
                                  const std::string &stepId,
                                  const Json::Value &result );

    /// WP04 (R28-T04): a parameter edit drops the stale result summary
    /// and demotes the node's success state (the form no longer shows the
    /// parameters that produced it).
    void invalidateStaleResult();

public:
    /// WP05 (R28-T04): merges a session's panel-entered params onto the
    /// definition a pipeline run executes — one parameter store (the
    /// canvas document and the session used to diverge silently: Run Full
    /// executed the loaded JSON's params, not the form's). Static for
    /// headless testing.
    static void overlayPanelParams( sicnu::workflow::WorkflowDefinition &def,
                                    const Json::Value &paramsByStep );

private:

    sicnu::workflow::WorkflowRuntime m_runtime;

    QString m_activeSession;
    QString m_activeStepId;
    QString m_activeTitle;
    long m_pendingTaskId = -1;
    /// R28-T04 (WP05): session/step captured AT SUBMISSION — a late
    /// terminal event must commit into the session the run belongs to,
    /// never the session the user has since switched to.
    QString m_pendingSessionId;
    QString m_pendingStepId;
    /// R28-T04 (WP04): the step whose result summary is on screen; the
    /// next parameter edit invalidates it (staleness contract).
    QString m_lastResultStepId;
    long m_activePipelineId = -1;
    bool m_pendingLoadToMap = false;
    /// QPointer (WP05): a panel/canvas destroyed mid-run must read null
    /// instead of dangling.
    QPointer<TaskPanelHost> m_panel;
    QPointer<sicnu::workflow::gui::PipelineCanvasWidget> m_canvas;
    /// Tracked connections so a REBIND replaces the wiring instead of
    /// duplicating it (double run-up-to, double status updates).
    QMetaObject::Connection m_runClickedConn;
    QMetaObject::Connection m_stopClickedConn;
    QMetaObject::Connection m_formValuesConn;
    QMetaObject::Connection m_runUpToConn;
    QMetaObject::Connection m_statusConn;
    QMetaObject::Connection m_nodeSelConn;
    sicnu::data::DataManager *m_dataManager = nullptr;

    QStringList m_layerIds;
    QStringList m_layerNames;
    bool m_builtinsRegistered = false;
    bool m_runConnected = false;
    bool m_runInFlight = false;
    /// swarm-review APP-R-2: identity of the last canvas→runtime sync, so an
    /// unchanged canvas does not churn sessions (and lose panel-entered params).
    QString m_syncedSessionId;
    QString m_syncedCanvasDefinitionId;
    std::string m_syncedCanvasSignature;
    /// swarm-review APP-S-2: a cancel was requested and the terminal callback
    /// for the tracked ids has not arrived yet — the ids stay armed so the
    /// callback can still commit the result (Canceled or Completed-in-race).
    bool m_cancelRequested = false;
};

