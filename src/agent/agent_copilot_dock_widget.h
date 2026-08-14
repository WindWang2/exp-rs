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

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"
#include "llm_config_manager.h"
#include "llm_streaming_client.h"
#include "view_control_service.h"
#include "raster_display_service.h"
#include "view_control_service.h"

#include <QMap>

namespace sicnu::data
{
class DataManager;
}

class QgsMapCanvas;
class QgsRubberBand;

namespace sicnu::agent
{

class AgentCopilotDockWidget : public QDockWidget
{
  Q_OBJECT

  public:
    explicit AgentCopilotDockWidget( QWidget *parent = nullptr );
    ~AgentCopilotDockWidget() override = default;

    void setContext( data::DataManager *dataManager, QgsMapCanvas *canvas );
    void sendPrompt( const QString &promptText );

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
    /// Registers a completion callback for a tool-call task; invokes it
    /// immediately when the task is already terminal.
    void watchToolCallCompletion( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete );
    /// Handles a `canvas:` action (draw_roi). Draws a QgsRubberBand on the
    /// active canvas and stores the ROI geometry (in canvas CRS) for later tool
    /// calls to consume. Returns a result payload (status + WKT). The agent→
    /// canvas write-back seam (ADR 0021 sibling); never routed through Task
    /// Center. Defined in the .cpp so the header stays free of QGIS canvas deps.
    Json::Value handleCanvasAction( const std::string &action, const Json::Value &arguments );

    data::DataManager *m_dataManager = nullptr;
    QgsMapCanvas *m_canvas = nullptr;
    ViewControlService m_viewControlService;
    RasterDisplayService m_rasterDisplayService;
    processing::AgentWorkflowExecutor m_workflowExecutor;

    processing::ToolCallDispatcher m_toolCallDispatcher;
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
