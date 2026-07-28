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
#include "llm_config_manager.h"
#include "llm_streaming_client.h"

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
    void planApprovalRequested( const QJsonObject &planJson );
    void toolExecutionFinished( const QJsonObject &resultJson );

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

  private:
    void appendUserMessageCard( const QString &text );
    void appendAssistantMessageCard();
    void appendToolCallCard( const QJsonObject &toolCallJson );

    data::DataManager *m_dataManager = nullptr;
    ActiveViewHost *m_viewHost = nullptr;
    processing::AgentWorkflowExecutor m_workflowExecutor;

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
