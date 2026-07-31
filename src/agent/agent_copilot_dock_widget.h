// src/agent/agent_copilot_dock_widget.h
#pragma once

#include <QComboBox>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "agent_context_resolver.h"
#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"
#include "llm_config_manager.h"
#include "llm_streaming_client.h"

#include <QMap>
#include <memory>

namespace sicnu::data
{
class DataManager;
}

class ActiveViewHost;

namespace sicnu::agent
{

class AgentCopilotDockWidget : public QDockWidget
{
  Q_OBJECT

  public:
    explicit AgentCopilotDockWidget( QWidget *parent = nullptr );
    ~AgentCopilotDockWidget() override = default;

    void setContext( data::DataManager *dataManager, ActiveViewHost *viewHost );
    void sendPrompt( const QString &promptText );

  signals:
    void viewPlanInCanvasRequested( const QJsonObject &planJson );
    void planApprovalRequested( const QJsonObject &planJson );
    void toolExecutionFinished( const QJsonObject &resultJson );
    /// Emitted once a single tool-call output was committed via
    /// OutputCommitter; the host loads this stable path (the original task
    /// output path was moved there by the commit).
    void toolOutputLayerRequested( const QString &filePath );

  private slots:
    void onSendClicked();
    void onClearClicked();
    void onSettingsClicked();
    void onProviderChanged( int index );

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
    void appendToolCallCard( const QJsonObject &toolCallJson );
    void appendPlanApprovalCard( const QJsonObject &planJson );
    void appendErrorMessage( const QString &errorMsg );
    /// Shared rejection tail: surface the reason in the chat and emit an error
    /// result payload.
    void handleToolCallRejection( const QString &errorMsg );
    /// Extracts the plan payload for the approval card from a parsed envelope.
    QJsonObject planArgumentsFor( const QJsonObject &toolCallJson ) const;
    /// Registers a completion callback for a tool-call task; invokes it
    /// immediately when the task is already terminal.
    void watchToolCallCompletion( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete );
    /// Commits a completed tool-call task's output via OutputCommitter and
    /// records the committed stable path in @a payload.
    void commitToolCallOutput( const sicnu::AlgorithmTaskInfo &info, Json::Value &payload );
    static Json::Value buildToolCallResultPayload( const sicnu::AlgorithmTaskInfo &info );

    data::DataManager *m_dataManager = nullptr;
    ActiveViewHost *m_viewHost = nullptr;
    processing::AgentWorkflowExecutor m_workflowExecutor;

    processing::ToolCallDispatcher m_toolCallDispatcher;
    std::unique_ptr<sicnu::OutputCommitter> m_outputCommitter;
    QMap<long, processing::ToolCallDispatcher::CompletionCallback> m_pendingToolCallCompletions;

    LlmStreamingClient *m_client = nullptr;
    QJsonArray m_messageHistory;
    QList<LlmProviderProfile> m_profiles;

    QComboBox *m_providerCombo = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_chatContainer = nullptr;
    QVBoxLayout *m_chatLayout = nullptr;

    QTextEdit *m_inputEdit = nullptr;
    QPushButton *m_sendBtn = nullptr;

    QLabel *m_currentReasoningLabel = nullptr;
    QLabel *m_currentContentLabel = nullptr;
    QString m_accumulatedReasoning;
    QString m_accumulatedContent;
    bool m_isStreaming = false;
};

} // namespace sicnu::agent
