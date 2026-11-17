// src/agent/agent_copilot_dock_widget.h
#pragma once

#include <QCheckBox>
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

#include "processing/framework/task_center.h"
#include "llm_config_manager.h"
#include "runtime/agent_conversation_runtime.h"
#include "runtime/agent_tool_host.h"
#include "runtime/conversation_checkpoint_store.h"
#include "raster_display_service.h"
#include "view_control_service.h"

#include <QMap>
#include <QPointer>


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
    /// The default argument resolves in the caller's module, keeping the
    /// desktop task view and agent on the same process-lifetime authority.
    explicit AgentCopilotDockWidget( QWidget *parent = nullptr,
                                    sicnu::TaskCenter &taskCenter = sicnu::TaskCenter::instance(),
                                    const QString &installationPrefix = {},
                                    const QString &checkpointDirectory = {} );
    ~AgentCopilotDockWidget() override;

    void setContext( data::DataManager *dataManager, QgsMapCanvas *canvas );
    void sendPrompt( const QString &promptText );
    bool isRunActive() const { return m_isStreaming; }
    /// Reconcile owned work before New/Open/Exit replaces the GUI project.
    bool stopForProjectChange( int timeoutMs = 30000 );

    /// Human-readable summary of the current/last agent run inspector state.
    /// Exposed for tests; returns an empty string when no run has started.
    QString runInspectorSummary() const;
    QJsonArray messageHistory() const;

    /// Retention bound for the LLM conversation history (LCY-5). Pure and
    /// static so the bound is unit-testable without a live LLM: keeps the
    /// leading system prompt plus the most recent kMaxHistoryMessages entries,
    /// never starting the retained tail on an orphaned role:"tool" reply.
    static QJsonArray pruneHistory( const QJsonArray &history );
    static constexpr int kMaxHistoryMessages = 60;

    ViewControlService *viewControlService() { return &m_viewControlService; }
    const ViewControlService *viewControlService() const { return &m_viewControlService; }
    RasterDisplayService *rasterDisplayService() { return &m_rasterDisplayService; }
    const RasterDisplayService *rasterDisplayService() const { return &m_rasterDisplayService; }

  signals:
    void viewPlanInCanvasRequested( const QJsonObject &planJson );
    /// The shell displays an already committed asset through its active view.
    void showResultOnMapRequested( const QString &assetId );

  private slots:
    void onSendClicked();
    void onClearClicked();
    void onSettingsClicked();
    void onProviderChanged( int index );
    void onRestoreClicked();
    /// Cancels all TaskCenter tasks submitted during the current run.
    void cancelCurrentRunTasks();

    void onReasoningTokenReceived( const QString &text );
    void onContentTokenReceived( const QString &text );
    /// Consumes LlmStreamingClient::malformedToolCall (#1395 / agent-harness-r4
    /// review #10): a streamed tool call the transport refused. Never fails the
    /// run — the refusal is a fact about the stream, and must be visible.
    void onMalformedToolCall( const QJsonObject &detail );
    void onToolCallParsed( const QJsonObject &toolCallJson );

    void onLlmFinished();
    void onErrorOccurred( const QString &errorMsg );

  private:
    void appendChatCard( QWidget *card );
    void appendUserMessageCard( const QString &text );
    void appendAssistantMessageCard();
    /// Creates a tool-call card and returns it so the completion path can
    /// update it with stage/result summary (P1-U3).
    QPointer<QWidget> appendToolCallCard( const QJsonObject &toolCallJson );
    void updateToolCallCard( const QString &toolCallId, const QString &statusText, const QString &detailText );
    void appendPlanApprovalCard( const QJsonObject &planJson, const QJsonObject &toolCallJson,
                                 const QString &token );
    void appendErrorMessage( const QString &errorMsg );
    /// Non-fatal notice card for a transport-refused tool call. Kept out of the
    /// streaming content label so later answer tokens cannot overwrite it.
    void appendRefusalNotice( const QString &text );
    /// Handles a `canvas:` action (draw_roi). Draws a QgsRubberBand on the
    /// active canvas and stores the ROI geometry (in canvas CRS) for later tool
    /// calls to consume. Returns a result payload (status + WKT). The agent→
    /// canvas write-back seam (ADR 0021 sibling); never routed through Task
    /// Center. Defined in the .cpp so the header stays free of QGIS canvas deps.
    Json::Value handleCanvasAction( const std::string &action, const Json::Value &arguments );

    void setupRunInspector();
    void updateRunInspector();
    void setRunStage( const QString &stage );
    void createConversationRuntime();
    void connectToolHost();
    void setRunControlsEnabled( bool enabled );
    void finishRunIfIdle();
    void finishRunUi();
    bool isPiBackend() const;
    void refreshConversationIdentity( bool forceNew = false );
    QString stableSystemPrompt() const;
    QString promptWithWorkspaceContext( const QString &text ) const;
    QString installationPrefix() const;
    runtime::ConversationCheckpointStore checkpointStore() const;
    bool prepareCheckpointBinding( const QString &toolRevision );
    void submitPendingPrompt();
    void renderRestoredHistory( const QJsonObject &checkpoint );
    void checkpointSaveFailed( bool publishedButUnverified = false );
    void noticeModelSettingsChanged();
    void recordOwnedExecutions( const QString &runId );
    bool inspectInterruptedExecutions( const runtime::ConversationCheckpointLoadResult &saved );
    QString translatedToolState( const QString &state ) const;
    /// (Re)creates the intro card shown before the first message of a
    /// conversation; tracked in m_welcomeLabel so sendPrompt can retire the
    /// live card instead of guessing by object name.
    void makeWelcomeCard();

    // Must bind before the executor/dispatcher members are constructed.
    sicnu::TaskCenter &m_taskCenter;
    // #2074: the project can close or replace these objects at any time, so
    // the dock observes them through guards instead of raw pointers. The
    // tool host stays the authority for what a run may act on.
    QPointer<data::DataManager> m_dataManager;
    QPointer<QgsMapCanvas> m_canvas;
    ViewControlService m_viewControlService;
    RasterDisplayService m_rasterDisplayService;
    runtime::AgentToolHost *m_toolHost = nullptr;
    QString m_installationPrefix;
    QString m_checkpointDirectory;
    runtime::AgentConversationRuntime *m_runtime = nullptr;
    QString m_pendingPrompt;
    QString m_toolRevision;
    QString m_unsavedProjectId;
    QString m_projectId;
    QString m_conversationId;
    QString m_piProfileName;
    bool m_piModelSettingsChanged = false;
    QByteArray m_recordedExecutionRefs;
    bool m_executionRefsWriteFailed = false;
    runtime::ConversationCheckpointBinding m_checkpointBinding;
    QString m_checkpointId;
    QString m_checkpointRunId;
    QString m_interruptionToAcknowledge;
    QJsonObject m_restoreCandidate;
    quint64 m_runtimeGeneration = 0;
    bool m_piSessionActive = false;
    bool m_piResetRequired = false;
    bool m_restoringConversation = false;
    bool m_checkpointPending = false;
    bool m_checkpointAttempted = false;
    bool m_checkpointSaveFailed = false;
    bool m_clearAfterRun = false;
    bool m_conversationSettled = true;
    bool m_hostIdle = true;
    bool m_cancelRequested = false;
    bool m_hideRunDelivery = false;
    bool m_projectContextInvalid = false;
    bool m_projectIdentityRefreshPending = false;
    QList<LlmProviderProfile> m_profiles;

    QPointer<QComboBox> m_providerCombo;
    QPointer<QComboBox> m_backendCombo;
    QPointer<QPushButton> m_settingsBtn;
    QPointer<QPushButton> m_clearBtn;
    QPointer<QPushButton> m_restoreBtn;

    QPointer<QScrollArea> m_scrollArea;
    bool m_followLatest = true;
    QPointer<QWidget> m_chatContainer;
    QPointer<QVBoxLayout> m_chatLayout;

    QPointer<QTextEdit> m_inputEdit;
    QPointer<QPushButton> m_sendBtn;
    QPointer<QCheckBox> m_includeWorkspaceContext;
    QPointer<QLabel> m_welcomeLabel;

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
    /// Streamed tool calls the transport refused (truncated/unparseable
    /// arguments, missing name, oversized) — surfaced per run so a silently
    /// dropped call is visible in the inspector (#1395).
    int m_refusedToolCallCount = 0;
    int m_runRepairAttempts = 0;
    QDateTime m_runStartTime;

    /// Tool-call id → card widget for in-place result updates (P1-U3).
    QMap<QString, QPointer<QWidget>> m_toolCallCards;
    QMap<QString, QPointer<QPushButton>> m_approvalButtons;

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
