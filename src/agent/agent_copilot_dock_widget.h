// src/agent/agent_copilot_dock_widget.h
#pragma once

#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"
#include "llm_config_manager.h"
#include "llm_streaming_client.h"
#include "view_control_service.h"
#include "raster_display_service.h"
#include "view_control_service.h"

#include <QMap>
#include <QPointer>

#include <atomic>
#include <memory>

namespace sicnu::data
{
class DataManager;
}

class QgsMapCanvas;
class QgsRubberBand;

#include "sicnu_agent_export.h"

namespace sicnu::agent
{

class SICNU_AGENT_EXPORT AgentCopilotDockWidget : public QDockWidget
{
  Q_OBJECT

  public:
    explicit AgentCopilotDockWidget( QWidget *parent = nullptr );
    ~AgentCopilotDockWidget() override;

    void setContext( data::DataManager *dataManager, QgsMapCanvas *canvas );
    void sendPrompt( const QString &promptText );

    /// Human-readable summary of the current/last agent run inspector state.
    /// Exposed for tests; returns an empty string when no run has started.
    QString runInspectorSummary() const;

    ViewControlService *viewControlService() { return &m_viewControlService; }
    const ViewControlService *viewControlService() const { return &m_viewControlService; }
    RasterDisplayService *rasterDisplayService() { return &m_rasterDisplayService; }
    const RasterDisplayService *rasterDisplayService() const { return &m_rasterDisplayService; }

  signals:
    void viewPlanInCanvasRequested( const QJsonObject &planJson );

  private slots:
    void onSendClicked();
    void onClearClicked();
    void onSettingsClicked();
    void onProviderChanged( int index );
    /// Cancels all TaskCenter tasks submitted during the current run.
    void cancelCurrentRunTasks();

    void onReasoningTokenReceived( const QString &text );
    void onContentTokenReceived( const QString &text );
    void onToolCallParsed( const QJsonObject &toolCallJson );
    void onLlmFinished();
    void onErrorOccurred( const QString &errorMsg );
    /// Terminal TaskCenter updates for watched tool-call tasks. Runs on the GUI
    /// thread (queued connection — taskUpdated is emitted by a worker thread).
    void onTaskCenterTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    void appendUserMessageCard( const QString &text );
    void appendAssistantMessageCard();
    /// Creates a tool-call card and returns it so the completion path can
    /// update it with stage/result summary (P1-U3).
    QPointer<QWidget> appendToolCallCard( const QJsonObject &toolCallJson );
    void updateToolCallCard( const QString &toolCallId, const QString &statusText, const QString &detailText );
    void appendPlanApprovalCard( const QJsonObject &planJson );
    void appendErrorMessage( const QString &errorMsg );
    /// Shared rejection tail: surface the reason in the chat and emit an error
    /// result payload.
    void handleToolCallRejection( const QString &errorMsg );
    /// Registers a completion callback for a tool-call task; invokes it
    /// immediately when the task is already terminal.
    void watchToolCallCompletion( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete );
    /// Sends the tool-call result back to the LLM as a function message and
    /// asks for the final answer.  Failures are surfaced explicitly so the
    /// model cannot hallucinate success.
    void sendToolResultFollowUp( const QJsonObject &toolCallJson, const Json::Value &resultPayload );
    /// Handles a `canvas:` action (draw_roi). Draws a QgsRubberBand on the
    /// active canvas and stores the ROI geometry (in canvas CRS) for later tool
    /// calls to consume. Returns a result payload (status + WKT). The agent→
    /// canvas write-back seam (ADR 0021 sibling); never routed through Task
    /// Center. Defined in the .cpp so the header stays free of QGIS canvas deps.
    Json::Value handleCanvasAction( const std::string &action, const Json::Value &arguments );

    void setupRunInspector();
    void updateRunInspector();
    void setRunStage( const QString &stage );

    data::DataManager *m_dataManager = nullptr;
    QgsMapCanvas *m_canvas = nullptr;
    ViewControlService m_viewControlService;
    RasterDisplayService m_rasterDisplayService;
    processing::AgentWorkflowExecutor m_workflowExecutor;

    processing::ToolCallDispatcher m_toolCallDispatcher;
    QMap<long, processing::ToolCallDispatcher::CompletionCallback> m_pendingToolCallCompletions;

    /// Shared guard for all async completion callbacks that touch `this`.
    /// Setting the atomic to false and clearing the pending map makes every
    /// outstanding callback no-op after the dock is cleared or destroyed.
    std::shared_ptr<std::atomic<bool>> m_completionGuard;
    /// Monotonic run epoch: bumped on every new run / clear so callbacks from
    /// a previous run (or a reopened dock) cannot pollute the new run's inspector.
    quint64 m_runEpoch = 0;

    LlmStreamingClient *m_client = nullptr;
    QJsonArray m_messageHistory;
    QList<LlmProviderProfile> m_profiles;

    QPointer<QComboBox> m_providerCombo;
    QPointer<QPushButton> m_settingsBtn;
    QPointer<QPushButton> m_clearBtn;

    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_chatContainer;
    QPointer<QVBoxLayout> m_chatLayout;

    QPointer<QTextEdit> m_inputEdit;
    QPointer<QPushButton> m_sendBtn;

    QPointer<QLabel> m_currentReasoningLabel;
    QPointer<QLabel> m_currentContentLabel;
    QString m_accumulatedReasoning;
    QString m_accumulatedContent;
    bool m_isStreaming = false;

    /// Per-run bookkeeping for stop→cancel routing and the run inspector.
    QString m_currentRunId;
    QSet<long> m_submittedTaskIds;
    QSet<long> m_submittedPipelineIds;
    QString m_currentRunStage;
    QString m_lastError;
    int m_runRepairAttempts = 0;
    QDateTime m_runStartTime;

    /// Tool-call id → card widget for in-place result updates (P1-U3).
    QMap<QString, QPointer<QWidget>> m_toolCallCards;

    /// Lightweight run inspector widgets (P1-U3 / P1-E4).
    struct RunInspector {
      QPointer<QFrame> container;
      QPointer<QLabel> titleLabel;
      QPointer<QLabel> stageLabel;
      QPointer<QLabel> taskLabel;
      QPointer<QLabel> callsLabel;
      QPointer<QLabel> errorsLabel;
      QPointer<QLabel> durationLabel;
      bool expanded = true;
    } m_runInspector;
};

} // namespace sicnu::agent
