// src/agent/agent_copilot_dock_widget.cpp
#include "agent_copilot_dock_widget.h"
#include "interaction_tool_registry.h"
#include "llm_settings_dialog.h"
#include "output_verifier.h"
#include "workspace_snapshot.h"

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"
#include "agent/tool_catalog/agent_tool_catalog.h"

#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgsrubberband.h>

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QUuid>

namespace sicnu::agent
{

AgentCopilotDockWidget::AgentCopilotDockWidget( QWidget *parent )
  : QDockWidget( tr( "AI Copilot 智能助手" ), parent )
  , m_completionGuard( std::make_shared<std::atomic<bool>>( true ) )
{
  setObjectName( QStringLiteral( "AgentCopilotDockWidget" ) );
  setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

  auto *mainWidget = new QWidget( this );
  auto *mainLayout = new QVBoxLayout( mainWidget );
  mainLayout->setContentsMargins( 4, 4, 4, 4 );
  mainLayout->setSpacing( 4 );

  // 1. Header Toolbar
  auto *headerLayout = new QHBoxLayout();
  m_providerCombo = new QComboBox( mainWidget );
  m_providerCombo->setToolTip( tr( "选择 AI 模型服务配置" ) );
  m_settingsBtn = new QPushButton( tr( "设置" ), mainWidget );
  m_settingsBtn->setToolTip( tr( "打开 AI Copilot 模型与连接设置" ) );
  m_clearBtn = new QPushButton( tr( "清空对话" ), mainWidget );
  m_clearBtn->setToolTip( tr( "清空对话历史" ) );

  headerLayout->addWidget( new QLabel( tr( "模型:" ), mainWidget ) );
  headerLayout->addWidget( m_providerCombo, 1 );
  headerLayout->addWidget( m_settingsBtn );
  headerLayout->addWidget( m_clearBtn );

  mainLayout->addLayout( headerLayout );

  // 2. Run Inspector (collapsible, lightweight observability panel)
  setupRunInspector();
  if ( m_runInspector.container )
    mainLayout->addWidget( m_runInspector.container.data() );

  // 3. Chat Scroll Area
  m_scrollArea = new QScrollArea( mainWidget );
  m_scrollArea->setWidgetResizable( true );
  m_chatContainer = new QWidget( m_scrollArea );
  m_chatLayout = new QVBoxLayout( m_chatContainer );
  m_chatLayout->setAlignment( Qt::AlignTop );
  m_chatLayout->setSpacing( 8 );
  m_chatContainer->setLayout( m_chatLayout );
  m_scrollArea->setWidget( m_chatContainer );

  mainLayout->addWidget( m_scrollArea, 1 );

  // 3. Bottom Input Bar
  auto *inputLayout = new QHBoxLayout();
  m_inputEdit = new QTextEdit( mainWidget );
  m_inputEdit->setPlaceholderText( tr( "输入遥感指令 (例: 对当前 Landsat 图像计算 NDVI)..." ) );
  m_inputEdit->setFixedHeight( 60 );

  m_sendBtn = new QPushButton( tr( "发送" ), mainWidget );
  m_sendBtn->setProperty( "primary", true );
  m_sendBtn->setToolTip( tr( "发送遥感指令 (Ctrl+Enter)" ) );
  m_sendBtn->setFixedHeight( 60 );

  inputLayout->addWidget( m_inputEdit, 1 );
  inputLayout->addWidget( m_sendBtn );

  mainLayout->addLayout( inputLayout );
  setWidget( mainWidget );

  // Setup Client & Signals
  m_client = new LlmStreamingClient( this );
  m_profiles = LlmConfigManager::loadProfiles();

  for ( const auto &p : m_profiles )
  {
    m_providerCombo->addItem( p.name, p.id );
  }

  LlmProviderProfile active = LlmConfigManager::activeProfile();
  for ( int i = 0; i < m_profiles.size(); ++i )
  {
    if ( m_profiles[i].id == active.id )
    {
      m_providerCombo->setCurrentIndex( i );
      break;
    }
  }

  connect( m_providerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &AgentCopilotDockWidget::onProviderChanged );
  connect( m_settingsBtn, &QPushButton::clicked, this, &AgentCopilotDockWidget::onSettingsClicked );
  connect( m_clearBtn, &QPushButton::clicked, this, &AgentCopilotDockWidget::onClearClicked );
  connect( m_sendBtn, &QPushButton::clicked, this, &AgentCopilotDockWidget::onSendClicked );

  connect( &LlmConfigManager::instance(), &LlmConfigManager::profilesChanged, this, [this]() {
    m_profiles = LlmConfigManager::loadProfiles();
    m_providerCombo->blockSignals( true );
    m_providerCombo->clear();
    for ( const auto &p : m_profiles )
    {
      m_providerCombo->addItem( p.name, p.id );
    }
    LlmProviderProfile active = LlmConfigManager::activeProfile();
    for ( int i = 0; i < m_profiles.size(); ++i )
    {
      if ( m_profiles[i].id == active.id )
      {
        m_providerCombo->setCurrentIndex( i );
        break;
      }
    }
    m_providerCombo->blockSignals( false );
  } );

  connect( &LlmConfigManager::instance(), &LlmConfigManager::activeProfileChanged, this, [this]( const LlmProviderProfile &active ) {
    for ( int i = 0; i < m_profiles.size(); ++i )
    {
      if ( m_profiles[i].id == active.id )
      {
        m_providerCombo->blockSignals( true );
        m_providerCombo->setCurrentIndex( i );
        m_providerCombo->blockSignals( false );
        break;
      }
    }
  } );

  connect( m_client, &LlmStreamingClient::reasoningTokenReceived,
           this, &AgentCopilotDockWidget::onReasoningTokenReceived );
  connect( m_client, &LlmStreamingClient::contentTokenReceived,
           this, &AgentCopilotDockWidget::onContentTokenReceived );
  connect( m_client, &LlmStreamingClient::toolCallParsed,
           this, &AgentCopilotDockWidget::onToolCallParsed );
  connect( m_client, &LlmStreamingClient::finished,
           this, &AgentCopilotDockWidget::onLlmFinished );
  connect( m_client, &LlmStreamingClient::errorOccurred,
           this, &AgentCopilotDockWidget::onErrorOccurred );

  // Tool-call completion watcher. Connected once in the constructor so no
  // completion can race watcher registration; the JobEngine worker thread
  // emits taskUpdated, so Qt delivers this queued onto the GUI thread.
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &AgentCopilotDockWidget::onTaskCenterTaskUpdated );
}

void AgentCopilotDockWidget::setContext( data::DataManager *dataManager, QgsMapCanvas *canvas )
{
  m_dataManager = dataManager;
  m_canvas = canvas;
  m_workflowExecutor.setDataManager( dataManager );
  m_toolCallDispatcher.setSourceTag( QStringLiteral( "agent" ) );
  m_toolCallDispatcher.setDataManager( dataManager );
  m_toolCallDispatcher.setOutputVerificationHandler(
    []( const QString &committedPath, const QString &kindHint ) -> Json::Value {
      const sicnu::agent::OutputVerification verification =
        sicnu::agent::OutputVerifier().verify( committedPath, kindHint );

      Json::Value result( Json::objectValue );
      result["ok"] = verification.ok;
      result["kind"] = verification.kind.toStdString();
      result["summary"] = verification.summary;
      Json::Value issues( Json::arrayValue );
      for ( const QString &issue : verification.issues )
        issues.append( issue.toStdString() );
      result["issues"] = issues;
      Json::Value warnings( Json::arrayValue );
      for ( const QString &warning : verification.warnings )
        warnings.append( warning.toStdString() );
      result["warnings"] = warnings;
      return result;
    } );

  m_viewControlService.setDataManager( dataManager );
  m_viewControlService.setMapCanvas( canvas );
  m_rasterDisplayService.setDataManager( dataManager );
  m_rasterDisplayService.setMapCanvas( canvas );
  InteractionToolRegistry::instance().registerBuiltinTools( &m_viewControlService, &m_rasterDisplayService, dataManager );

  m_toolCallDispatcher.setInteractionActionHandler(
    []( const std::string &name, const Json::Value &args ) {
      return InteractionToolRegistry::instance().execute( name, args );
    } );
  m_toolCallDispatcher.setCanvasActionHandler(
    [this]( const std::string &action, const Json::Value &args ) {
      return handleCanvasAction( action, args );
    } );
}

void AgentCopilotDockWidget::setupRunInspector()
{
  auto *container = new QFrame( this );
  container->setObjectName( QStringLiteral( "runInspectorContainer" ) );
  container->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( container );
  layout->setContentsMargins( 6, 6, 6, 6 );
  layout->setSpacing( 4 );

  auto *titleLayout = new QHBoxLayout();
  m_runInspector.titleLabel = new QLabel( tr( "运行监测器" ), container );
  titleLayout->addWidget( m_runInspector.titleLabel.data(), 1 );

  auto *toggleBtn = new QPushButton( tr( "折叠" ), container );
  toggleBtn->setFlat( true );
  titleLayout->addWidget( toggleBtn );
  layout->addLayout( titleLayout );

  auto makeLabel = [container]( const QString &text ) {
    auto *label = new QLabel( text, container );
    label->setWordWrap( true );
    return label;
  };

  m_runInspector.stageLabel = makeLabel( tr( "阶段: -" ) );
  m_runInspector.taskLabel = makeLabel( tr( "任务: -" ) );
  m_runInspector.callsLabel = makeLabel( tr( "调用: 0" ) );
  m_runInspector.errorsLabel = makeLabel( tr( "错误: 0" ) );
  m_runInspector.durationLabel = makeLabel( tr( "耗时: 0s" ) );

  layout->addWidget( m_runInspector.stageLabel.data() );
  layout->addWidget( m_runInspector.taskLabel.data() );
  layout->addWidget( m_runInspector.callsLabel.data() );
  layout->addWidget( m_runInspector.errorsLabel.data() );
  layout->addWidget( m_runInspector.durationLabel.data() );

  m_runInspector.container = container;

  connect( toggleBtn, &QPushButton::clicked, this, [this, toggleBtn]() {
    m_runInspector.expanded = !m_runInspector.expanded;
    if ( m_runInspector.stageLabel )
      m_runInspector.stageLabel->setVisible( m_runInspector.expanded );
    if ( m_runInspector.taskLabel )
      m_runInspector.taskLabel->setVisible( m_runInspector.expanded );
    if ( m_runInspector.callsLabel )
      m_runInspector.callsLabel->setVisible( m_runInspector.expanded );
    if ( m_runInspector.errorsLabel )
      m_runInspector.errorsLabel->setVisible( m_runInspector.expanded );
    if ( m_runInspector.durationLabel )
      m_runInspector.durationLabel->setVisible( m_runInspector.expanded );
    toggleBtn->setText( m_runInspector.expanded ? tr( "折叠" ) : tr( "展开" ) );
  } );
}

void AgentCopilotDockWidget::updateRunInspector()
{
  if ( !m_runInspector.container )
    return;

  if ( m_runInspector.titleLabel )
  {
    m_runInspector.titleLabel->setText(
      m_currentRunId.isEmpty()
        ? tr( "运行监测器" )
        : QString( tr( "运行监测器 — %1" ) ).arg( m_currentRunId.left( 8 ) ) );
  }
  if ( m_runInspector.stageLabel )
    m_runInspector.stageLabel->setText( QString( tr( "阶段: %1" ) ).arg( m_currentRunStage.isEmpty() ? QStringLiteral( "-" ) : m_currentRunStage ) );

  QString taskText = tr( "任务: -" );
  if ( !m_submittedTaskIds.isEmpty() )
  {
    const long latestTaskId = *std::max_element( m_submittedTaskIds.cbegin(), m_submittedTaskIds.cend() );
    const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( latestTaskId );
    taskText = QString( tr( "任务: %1 (ID %2)" ) ).arg( info.algorithmId.isEmpty() ? QStringLiteral( "-" ) : info.algorithmId ).arg( latestTaskId );
  }
  if ( m_runInspector.taskLabel )
    m_runInspector.taskLabel->setText( taskText );

  if ( m_runInspector.callsLabel )
    m_runInspector.callsLabel->setText( QString( tr( "调用: %1" ) ).arg( m_toolCallCards.size() ) );

  int errorCount = m_lastError.isEmpty() ? 0 : 1;
  if ( m_runInspector.errorsLabel )
  {
    m_runInspector.errorsLabel->setText(
      QString( tr( "错误: %1%2" ) )
        .arg( errorCount )
        .arg( errorCount ? QStringLiteral( " — %1" ).arg( m_lastError ) : QString() ) );
  }

  if ( m_runInspector.durationLabel && m_runStartTime.isValid() )
  {
    const qint64 elapsedSecs = m_runStartTime.secsTo( QDateTime::currentDateTimeUtc() );
    m_runInspector.durationLabel->setText( QString( tr( "耗时: %1s" ) ).arg( elapsedSecs ) );
  }
}

void AgentCopilotDockWidget::setRunStage( const QString &stage )
{
  m_currentRunStage = stage;
  updateRunInspector();
}

void AgentCopilotDockWidget::cancelCurrentRunTasks()
{
  for ( long taskId : std::as_const( m_submittedTaskIds ) )
  {
    sicnu::TaskCenter::instance().cancelTask( taskId );
  }
  for ( long pipelineId : std::as_const( m_submittedPipelineIds ) )
  {
    sicnu::TaskCenter::instance().cancelPipeline( pipelineId );
  }
}

Json::Value AgentCopilotDockWidget::handleCanvasAction( const std::string &action,
                                                        const Json::Value &arguments )
{
  if ( action == "draw_roi" )
  {
    return m_viewControlService.setRoi( arguments );
  }
  else if ( action == "clear_roi" )
  {
    return m_viewControlService.clearRoi( arguments );
  }

  Json::Value result( Json::objectValue );
  result["action"] = action;
  result["status"] = "error";
  result["errorMessage"] = "Unknown canvas action: " + action;
  return result;
}

void AgentCopilotDockWidget::onProviderChanged( int index )
{
  if ( index >= 0 && index < m_profiles.size() )
  {
    LlmConfigManager::setActiveProfile( m_profiles[index] );
    m_client->setProfile( m_profiles[index] );
  }
}

void AgentCopilotDockWidget::onSettingsClicked()
{
  LlmSettingsDialog dlg( this );
  if ( dlg.exec() == QDialog::Accepted )
  {
    LlmProviderProfile selected = dlg.selectedProfile();
    LlmConfigManager::setActiveProfile( selected );

    m_profiles = LlmConfigManager::loadProfiles();
    m_providerCombo->blockSignals( true );
    m_providerCombo->clear();
    int activeIdx = 0;
    for ( int i = 0; i < m_profiles.size(); ++i )
    {
      m_providerCombo->addItem( m_profiles[i].name, m_profiles[i].id );
      if ( m_profiles[i].id == selected.id )
        activeIdx = i;
    }
    m_providerCombo->setCurrentIndex( activeIdx );
    m_providerCombo->blockSignals( false );

    m_client->setProfile( selected );
  }
}

void AgentCopilotDockWidget::onClearClicked()
{
  if ( m_isStreaming )
  {
    m_client->cancel();
    onLlmFinished();
  }

  // Cancel any outstanding async completion callbacks that still hold a
  // reference to this dock, then clear the pending map so the taskUpdated
  // slot cannot resurrect and invoke a stale callback (P0-U4).
  if ( m_completionGuard )
    *m_completionGuard = false;
  m_pendingToolCallCompletions.clear();
  m_completionGuard = std::make_shared<std::atomic<bool>>( true );
  ++m_runEpoch;

  // Reset per-run state so the inspector and stop button start clean.
  m_currentRunId.clear();
  m_submittedTaskIds.clear();
  m_submittedPipelineIds.clear();
  m_currentRunStage.clear();
  m_lastError.clear();
  m_runRepairAttempts = 0;
  m_runStartTime = QDateTime();
  m_toolCallCards.clear();
  updateRunInspector();

  m_currentReasoningLabel = nullptr;
  m_currentContentLabel = nullptr;
  m_messageHistory = QJsonArray();
  QLayoutItem *child;
  while ( ( child = m_chatLayout->takeAt( 0 ) ) != nullptr )
  {
    if ( child->widget() )
      child->widget()->deleteLater();
    delete child;
  }
}

void AgentCopilotDockWidget::onSendClicked()
{
  if ( m_isStreaming )
  {
    // Stop streaming first, then cancel any TaskCenter work that was submitted
    // for this run so the user-visible stop button actually stops processing.
    m_client->cancel();
    cancelCurrentRunTasks();
    // #704.2: a tool completion landing after Stop used to re-enter
    // sendToolResultFollowUp, set m_isStreaming and re-contact the LLM —
    // overriding the user's stop. Bumping the epoch makes every in-flight
    // completion callback of this run a no-op (same cross-run guard
    // sendPrompt uses), and clearing the pending map drops them promptly.
    ++m_runEpoch;
    m_pendingToolCallCompletions.clear();
    onLlmFinished();
    setRunStage( tr( "Canceled" ) );
    return;
  }

  QString prompt = m_inputEdit->toPlainText().trimmed();
  if ( prompt.isEmpty() )
    return;

  m_inputEdit->clear();
  sendPrompt( prompt );
}

void AgentCopilotDockWidget::sendPrompt( const QString &promptText )
{
  // Start a new run. Invalidate any completions from the previous run so a
  // late callback cannot pollute the new inspector/cards (cross-run epoch).
  if ( m_completionGuard )
    *m_completionGuard = false;
  m_pendingToolCallCompletions.clear();
  m_completionGuard = std::make_shared<std::atomic<bool>>( true );
  ++m_runEpoch;
  cancelCurrentRunTasks();

  m_currentRunId = QUuid::createUuid().toString( QUuid::WithoutBraces );
  m_submittedTaskIds.clear();
  m_submittedPipelineIds.clear();
  m_lastError.clear();
  m_runRepairAttempts = 0;
  m_runStartTime = QDateTime::currentDateTimeUtc();
  m_toolCallCards.clear();
  setRunStage( tr( "Understanding" ) );

  appendUserMessageCard( promptText );

  // Build Workspace System Context
  QString systemPrompt = WorkspaceSnapshot::capture(
                           m_dataManager, m_canvas,
                           m_canvas && m_canvas->currentLayer()
                             ? m_canvas->currentLayer()->name()
                             : QString(),
                           m_rasterDisplayService.displayRevision() )
                           .toSystemPromptHeader();

  m_messageHistory = QJsonArray();

  QJsonObject sysMsg;
  sysMsg[QStringLiteral( "role" )] = QStringLiteral( "system" );
  sysMsg[QStringLiteral( "content" )] = systemPrompt;
  m_messageHistory.append( sysMsg );

  QJsonObject userMsg;
  userMsg[QStringLiteral( "role" )] = QStringLiteral( "user" );
  userMsg[QStringLiteral( "content" )] = promptText;
  m_messageHistory.append( userMsg );

  appendAssistantMessageCard();

  m_isStreaming = true;
  m_sendBtn->setText( QStringLiteral( "停止 ⏹" ) );

  // the unified agent tool catalog (algorithms, canvas, data) and hand the transport
  // the exact schemas to put on the wire (ADR 0049). Conversion reuses the shared Json↔QVariant helper.
  const Json::Value cppTools = tool_catalog::AgentToolCatalog::instance().exportOpenAiToolDefinitions();
  const QJsonArray tools = QJsonArray::fromVariantList( processing::jsonValueToVariant( cppTools ).toList() );

  m_client->sendChatCompletion( m_messageHistory, tools );
}

void AgentCopilotDockWidget::appendUserMessageCard( const QString &text )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "userBubble" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );
  auto *label = new QLabel( QString( "<b>你:</b> %1" ).arg( text.toHtmlEscaped() ), card );
  label->setWordWrap( true );
  layout->addWidget( label );

  m_chatLayout->addWidget( card );
}

void AgentCopilotDockWidget::appendAssistantMessageCard()
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "agentBubble" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );

  m_currentReasoningLabel = new QLabel( card );
  m_currentReasoningLabel->setObjectName( QStringLiteral( "agentReasoning" ) );
  m_currentReasoningLabel->setWordWrap( true );
  m_currentReasoningLabel->setVisible( false );
  layout->addWidget( m_currentReasoningLabel );

  m_currentContentLabel = new QLabel( card );
  m_currentContentLabel->setWordWrap( true );
  layout->addWidget( m_currentContentLabel );

  m_accumulatedReasoning.clear();
  m_accumulatedContent.clear();

  m_chatLayout->addWidget( card );
}

void AgentCopilotDockWidget::onReasoningTokenReceived( const QString &text )
{
  m_accumulatedReasoning += text;
  if ( m_currentReasoningLabel )
  {
    m_currentReasoningLabel->setVisible( true );
    m_currentReasoningLabel->setText( QString( "<b>思考过程:</b><br/>%1" ).arg( m_accumulatedReasoning.toHtmlEscaped() ) );
  }
}

void AgentCopilotDockWidget::onContentTokenReceived( const QString &text )
{
  m_accumulatedContent += text;
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( m_accumulatedContent.toHtmlEscaped() );
  }
}

void AgentCopilotDockWidget::onToolCallParsed( const QJsonObject &toolCallJson )
{
  // The dispatcher owns parsing, classification, id normalization, and
  // submission (ADR 0021). Plan requests keep the existing approval flow.
  const Json::Value cppEnvelope = processing::jsonValueFromQJson( toolCallJson );
  const auto classification = m_toolCallDispatcher.classify( cppEnvelope );
  if ( classification == processing::ToolCallClassification::PlanRequest )
  {
    // The dispatcher owns envelope-shape knowledge (ADR 0048): extract the
    // arguments exactly as classify()/submit() see them, then hand the plan
    // to the approval card as a QJsonObject.
    const Json::Value planArgs = m_toolCallDispatcher.argumentsFor( cppEnvelope );
    appendPlanApprovalCard( QJsonObject::fromVariantMap( processing::jsonObjectToVariantMap( planArgs ) ),
                            toolCallJson );
    setRunStage( tr( "Planning" ) );
    return;
  }

  const QString toolCallId = toolCallJson[QStringLiteral( "id" )].toString();
  m_toolCallCards.insert( toolCallId, appendToolCallCard( toolCallJson ) );
  updateRunInspector();

  if ( classification == processing::ToolCallClassification::Invalid )
  {
    // rejectionReason reports the same reason submit() would give, without
    // the side-effect of a doomed submission.
    const QString reason = m_toolCallDispatcher.rejectionReason( cppEnvelope );
    handleToolCallRejection( reason, toolCallJson );
    m_lastError = reason;
    updateToolCallCard( toolCallId, tr( "rejected" ), reason );
    setRunStage( tr( "Failed" ) );
    updateRunInspector();
    return;
  }

  // Single tool call: submit asynchronously; the completion payload arrives
  // via the watcher (never blocks the GUI thread). Outputs are committed and
  // layers auto-displayed by the dispatcher.
  // Interaction actions complete synchronously inside submit(): route the
  // result through a real completion callback so the follow-up request with
  // the role:"tool" message is actually sent (#621 - the empty callback
  // dropped the payload and the conversation dead-ended after any
  // view:/roi:/canvas:/layer:/raster:/data: call).
  // Shared state: the callback may also be stored by the dispatcher for the
  // async path, so it must not dangle after this scope returns.
  auto interactionState = std::make_shared<std::pair<Json::Value, bool>>( Json::Value(), false );
  auto captureInteraction = [interactionState]( const Json::Value &payload ) {
    interactionState->first = payload;
    interactionState->second = true;
  };

  QString error;
  long taskId = -1;
  const bool ok = m_toolCallDispatcher.submit( cppEnvelope, captureInteraction, &error, &taskId );

  if ( !ok )
  {
    handleToolCallRejection( error, toolCallJson );
    m_lastError = error;
    updateToolCallCard( toolCallId, tr( "rejected" ), error );
    setRunStage( tr( "Failed" ) );
    return;
  }
  // For interaction actions the dispatcher completed synchronously (sentinel
  // task id 9000001). No watcher needed — answer the model with the captured
  // payload (OpenAI contract: every tool_calls needs a role:"tool" reply).
  if ( taskId == 9000001 )
  {
    updateToolCallCard( toolCallId, tr( "completed" ), tr( "Interaction action completed synchronously" ) );
    if ( interactionState->second )
      sendToolResultFollowUp( toolCallJson, interactionState->first );
    else
    {
      Json::Value empty( Json::objectValue );
      empty["status"] = "success";
      empty["result"] = "interaction action completed";
      sendToolResultFollowUp( toolCallJson, empty );
    }
    return;
  }
  if ( taskId > 0 )
  {
    m_submittedTaskIds.insert( taskId );
    setRunStage( tr( "Running" ) );

    auto guard = m_completionGuard;
    const quint64 epoch = m_runEpoch;
    watchToolCallCompletion( taskId, [guard, epoch, this, toolCallJson, toolCallId, taskId]( const Json::Value &resultPayload ) mutable {
      if ( !guard || !*guard )
        return;
      if ( epoch != m_runEpoch )
        return;

      const bool ok = resultPayload.isObject()
                      && resultPayload.isMember( "status" )
                      && resultPayload["status"].asString() == "success";
      const bool verified = resultPayload.isMember( "verified" ) ? resultPayload["verified"].asBool() : ok;

      QString statusText = ok ? tr( "成功" ) : tr( "失败" );
      QString detailText;
      if ( ok && verified )
      {
        QStringList parts;
        if ( resultPayload.isMember( "output" ) )
          parts.append( tr( "output: %1" ).arg( QString::fromStdString( resultPayload["output"].asString() ) ) );
        if ( resultPayload.isMember( "assetId" ) )
          parts.append( tr( "assetId: %1" ).arg( QString::fromStdString( resultPayload["assetId"].asString() ) ) );
        parts.append( tr( "verified: %1" ).arg( verified ? tr( "yes" ) : tr( "no" ) ) );
        detailText = parts.join( QStringLiteral( " | " ) );
        setRunStage( tr( "Completed" ) );
      }
      else
      {
        const QString error = resultPayload.isMember( "errorMessage" )
                              ? QString::fromStdString( resultPayload["errorMessage"].asString() )
                              : QStringLiteral( "unknown error" );
        detailText = error;
        m_lastError = error;
        setRunStage( tr( "Failed" ) );
      }
      updateToolCallCard( toolCallId, statusText, detailText );
      updateRunInspector();

      // Auto-zoom the active canvas to any new output asset so the user sees
      // the result of the tool call immediately (P1-M1).
      if ( ok && resultPayload.isMember( "assetId" ) && resultPayload["assetId"].isString() )
      {
        Json::Value zoomParams( Json::objectValue );
        zoomParams["asset_id"] = resultPayload["assetId"];
        m_viewControlService.zoomToAsset( zoomParams );
      }

      sendToolResultFollowUp( toolCallJson, resultPayload );
    } );
  }
}

void AgentCopilotDockWidget::handleToolCallRejection( const QString &errorMsg, const QJsonObject &toolCallJson )
{
  appendErrorMessage( errorMsg.isEmpty() ? QStringLiteral( "工具调用失败。" ) : errorMsg );

  // #621/#642: a rejected tool call must still receive a role:"tool" reply,
  // otherwise the model never learns the call failed and the turn dead-ends.
  // Only possible when the envelope is well-formed enough to be a real
  // tool_calls entry on the wire; a garbage envelope has no call to answer.
  if ( !toolCallJson.isEmpty()
       && !toolCallJson[QStringLiteral( "id" )].toString().isEmpty()
       && toolCallJson.contains( QStringLiteral( "function" ) ) )
  {
    Json::Value failure( Json::objectValue );
    failure["status"] = "error";
    failure["errorMessage"] = errorMsg.toStdString();
    sendToolResultFollowUp( toolCallJson, failure );
  }
}

void AgentCopilotDockWidget::sendToolResultFollowUp( const QJsonObject &toolCallJson,
                                                     const Json::Value &resultPayload )
{
  if ( !m_client )
    return;

  // Surface failures explicitly; do not let the model assume success.
  QString content;
  const bool ok = resultPayload.isObject()
                  && resultPayload.isMember( "status" )
                  && resultPayload["status"].asString() == "success";
  const bool verified = resultPayload.isMember( "verified" ) ? resultPayload["verified"].asBool() : ok;

  if ( !ok )
  {
    const QString error = resultPayload.isMember( "errorMessage" )
                          ? QString::fromStdString( resultPayload["errorMessage"].asString() )
                          : QStringLiteral( "unknown error" );
    content = QStringLiteral( "工具执行失败: %1" ).arg( error );
  }
  else if ( !verified )
  {
    QStringList issues;
    if ( resultPayload.isMember( "verificationIssues" ) && resultPayload["verificationIssues"].isArray() )
    {
      for ( const auto &issue : resultPayload["verificationIssues"] )
        issues.append( QString::fromStdString( issue.asString() ) );
    }
    content = QStringLiteral( "工具执行完成，但输出验证未通过: %1" ).arg( issues.join( QStringLiteral( "; " ) ) );
  }
  else
  {
    content = QString::fromStdString( resultPayload.toStyledString() );
  }

  // Budget the tool-result payload: a giant committed payload (full lineage,
  // large feature dumps) re-sent on every follow-up can blow the context
  // window (#642). Tail-truncate with an explicit marker, mirroring the Pi
  // bridge's budget.
  constexpr int kMaxToolResultChars = 50000;
  if ( content.size() > kMaxToolResultChars )
    content = content.left( kMaxToolResultChars )
              + QStringLiteral( "\n…[truncated %1 characters]" ).arg( content.size() - kMaxToolResultChars );

  QJsonArray toolCalls;
  toolCalls.append( toolCallJson );

  QJsonObject assistantMsg;
  assistantMsg[QStringLiteral( "role" )] = QStringLiteral( "assistant" );
  assistantMsg[QStringLiteral( "content" )] = QString();
  assistantMsg[QStringLiteral( "tool_calls" )] = toolCalls;

  QJsonObject toolMsg;
  toolMsg[QStringLiteral( "role" )] = QStringLiteral( "tool" );
  toolMsg[QStringLiteral( "tool_call_id" )] = toolCallJson[QStringLiteral( "id" )].toString();
  toolMsg[QStringLiteral( "content" )] = content;

  // #621/#642: the completed tool exchange must persist in the conversation
  // history. Building the follow-up from the original [system, user] array
  // erased every prior exchange, so a second tool round ran with the first
  // round's result amnesia'd out.
  m_messageHistory.append( assistantMsg );
  m_messageHistory.append( toolMsg );

  // Ask the model to produce the final answer based on the execution evidence.
  QJsonObject finalUserMsg;
  finalUserMsg[QStringLiteral( "role" )] = QStringLiteral( "user" );
  finalUserMsg[QStringLiteral( "content" )] = QStringLiteral(
    "请根据上述工具执行结果生成最终回答。如果执行失败或验证未通过，必须明确说明失败原因，"
    "不得报喜。" );
  m_messageHistory.append( finalUserMsg );

  const QJsonArray followUpMessages = m_messageHistory;

  m_isStreaming = true;
  if ( m_sendBtn )
    m_sendBtn->setText( QStringLiteral( "停止 ⏹" ) );

  const Json::Value cppTools = tool_catalog::AgentToolCatalog::instance().exportOpenAiToolDefinitions();
  const QJsonArray tools = QJsonArray::fromVariantList( processing::jsonValueToVariant( cppTools ).toList() );

  m_client->sendChatCompletion( followUpMessages, tools );
}

void AgentCopilotDockWidget::watchToolCallCompletion( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete )
{
  // The task may already be terminal (completed synchronously): deliver the
  // payload immediately instead of waiting for a signal that already fired.
  const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( isTerminalStatus( info.status ) )
  {
    onComplete( m_toolCallDispatcher.buildCommittedResultPayload( info ) );
    return;
  }
  m_pendingToolCallCompletions.insert( taskId, std::move( onComplete ) );
}

void AgentCopilotDockWidget::onTaskCenterTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  if ( !isTerminalStatus( info.status ) )
    return;

  auto it = m_pendingToolCallCompletions.find( info.taskId );
  if ( it == m_pendingToolCallCompletions.end() )
    return;

  const auto callback = it.value();
  m_pendingToolCallCompletions.erase( it );

  // Committing and payload shape live in ToolCallDispatcher (injected
  // DataManager); layer loading after commit is handled by QgisDisplayManager
  // auto-display on DataManager::assetAdded.
  callback( m_toolCallDispatcher.buildCommittedResultPayload( info ) );
}

void AgentCopilotDockWidget::appendErrorMessage( const QString &errorMsg )
{
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( QString( "<font color='red'>错误: %1</font>" ).arg( errorMsg.toHtmlEscaped() ) );
  }
}

QPointer<QWidget> AgentCopilotDockWidget::appendToolCallCard( const QJsonObject &toolCallJson )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "toolCallCard" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );
  QJsonObject funcObj = toolCallJson[QStringLiteral( "function" )].toObject();
  QString algName = funcObj[QStringLiteral( "name" )].toString();

  auto *title = new QLabel( QString( tr( "准备执行工具: <b>%1</b>" ) ).arg( algName.toHtmlEscaped() ), card );
  title->setObjectName( QStringLiteral( "ToolCallCardTitle" ) );
  title->setWordWrap( true );
  layout->addWidget( title );

  auto *details = new QLabel( tr( "状态: 已提交" ), card );
  details->setObjectName( QStringLiteral( "ToolCallCardDetails" ) );
  details->setWordWrap( true );
  layout->addWidget( details );

  m_chatLayout->addWidget( card );
  return card;
}

void AgentCopilotDockWidget::updateToolCallCard( const QString &toolCallId,
                                                 const QString &statusText,
                                                 const QString &detailText )
{
  QPointer<QWidget> card = m_toolCallCards.value( toolCallId );
  if ( !card )
    return;

  auto *title = card->findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
  if ( title && !statusText.isEmpty() )
  {
    title->setText( QString( "%1 <span style=\"color:%2;\">[%3]</span>" )
                      .arg( title->text().toHtmlEscaped(), statusText.contains( tr( "失败" ) ) ? QStringLiteral( "#f87171" ) : QStringLiteral( "#4ade80" ), statusText.toHtmlEscaped() ) );
  }

  auto *details = card->findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
  if ( details )
    details->setText( detailText );
}

void AgentCopilotDockWidget::appendPlanApprovalCard( const QJsonObject &planJson, const QJsonObject &toolCallJson )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "planApprovalCard" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );

  int stepCount = 0;
  if ( planJson.contains( QStringLiteral( "steps" ) ) && planJson[QStringLiteral( "steps" )].isArray() )
    stepCount = planJson[QStringLiteral( "steps" )].toArray().size();

  auto *title = new QLabel( QString( tr( "AI Copilot 提出了 <b>%1 个步骤</b> 的遥感处理工作流计划" ) ).arg( stepCount ), card );
  layout->addWidget( title );

  auto *btnLayout = new QHBoxLayout();
  auto *previewBtn = new QPushButton( tr( "在画布中预览" ), card );
  auto *runBtn = new QPushButton( tr( "确认并执行" ), card );
  runBtn->setProperty( "primary", true );

  btnLayout->addWidget( previewBtn );
  btnLayout->addWidget( runBtn );
  layout->addLayout( btnLayout );

  m_chatLayout->addWidget( card );

  connect( previewBtn, &QPushButton::clicked, this, [this, planJson]() {
    emit viewPlanInCanvasRequested( planJson );
  } );

  connect( runBtn, &QPushButton::clicked, this, [this, planJson, runBtn, toolCallJson]() {
    runBtn->setEnabled( false );
    runBtn->setText( QStringLiteral( "执行中…" ) );
    QPointer<QPushButton> safeRunBtn = runBtn;
    auto guard = m_completionGuard;
    const quint64 epoch = m_runEpoch;
    setRunStage( tr( "Running" ) );
    // Execute the approved plan asynchronously. AgentWorkflowExecutor owns
    // pipeline watching and marshals the completion callback onto this
    // widget's thread — no detached std::thread (ADR 0047).
    const long pipelineId = m_workflowExecutor.executeAgentPlanAsync( processing::jsonValueFromQJson( planJson ),
                                                                      [guard, epoch, this, safeRunBtn, toolCallJson]( const Json::Value &resultPayload ) {
      // Completion payload shape is owned by the workflow executor; read it
      // in Json-land instead of round-tripping through QJson (ADR 0048).
      if ( !guard || !*guard )
        return;
      if ( epoch != m_runEpoch )
        return;
      const Json::Value resultObj = resultPayload.isObject() ? resultPayload : Json::Value( Json::objectValue );
      if ( resultObj["status"].asString() != "success" )
      {
        const QString error = QString::fromStdString( resultObj["errorMessage"].asString() );
        appendErrorMessage( error );
        m_lastError = error;
        setRunStage( tr( "Failed" ) );
        if ( safeRunBtn )
        {
          safeRunBtn->setEnabled( true );
          safeRunBtn->setText( QStringLiteral( "重试执行" ) );
        }
        // #621/#642: the plan outcome must re-enter the LLM loop — without a
        // role:"tool" reply the model that proposed the steps never learns
        // what happened and the conversation stalls until the user prompts.
        if ( !toolCallJson.isEmpty() && !toolCallJson[QStringLiteral( "id" )].toString().isEmpty() )
        {
          Json::Value failure( Json::objectValue );
          failure["status"] = "error";
          if ( resultObj.isMember( "errorMessage" ) )
            failure["errorMessage"] = resultObj["errorMessage"];
          else
            failure["errorMessage"] = error.isEmpty() ? QStringLiteral( "plan execution failed" ).toStdString()
                                                      : error.toStdString();
          sendToolResultFollowUp( toolCallJson, failure );
        }
      }
      else
      {
        setRunStage( tr( "Completed" ) );
        if ( safeRunBtn )
        {
          safeRunBtn->setText( QStringLiteral( "已完成" ) );
        }
        if ( !toolCallJson.isEmpty() && !toolCallJson[QStringLiteral( "id" )].toString().isEmpty() )
          sendToolResultFollowUp( toolCallJson, resultObj );
      }
      updateRunInspector();
    }, this );
    if ( pipelineId >= 0 )
      m_submittedPipelineIds.insert( pipelineId );
  } );
}

AgentCopilotDockWidget::~AgentCopilotDockWidget()
{
  // Disable every async callback that still references this dock (A1).
  // The shared_ptr guard outlives us in copies held by the dispatcher /
  // executor, so those copies will no-op instead of touching a destroyed
  // widget. Clear the pending map to release those lambdas promptly.
  if ( m_completionGuard )
    *m_completionGuard = false;
  m_pendingToolCallCompletions.clear();

  if ( m_client )
  {
    m_client->disconnect( this );
    m_client->cancel();
  }
}

void AgentCopilotDockWidget::onLlmFinished()
{
  m_isStreaming = false;
  if ( m_sendBtn )
  {
    m_sendBtn->setText( QStringLiteral( "发送 ▶" ) );
  }
}

QString AgentCopilotDockWidget::runInspectorSummary() const
{
  QStringList parts;
  parts.append( QStringLiteral( "run=%1" ).arg( m_currentRunId.isEmpty() ? QStringLiteral( "-" ) : m_currentRunId.left( 8 ) ) );
  parts.append( QStringLiteral( "stage=%1" ).arg( m_currentRunStage.isEmpty() ? QStringLiteral( "-" ) : m_currentRunStage ) );
  parts.append( QStringLiteral( "tasks=%1" ).arg( m_submittedTaskIds.size() ) );
  parts.append( QStringLiteral( "calls=%1" ).arg( m_toolCallCards.size() ) );
  if ( !m_lastError.isEmpty() )
    parts.append( QStringLiteral( "error=%1" ).arg( m_lastError ) );
  return parts.join( QStringLiteral( " | " ) );
}

void AgentCopilotDockWidget::onErrorOccurred( const QString &errorMsg )
{
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( QString( "<font color='red'>错误: %1</font>" ).arg( errorMsg.toHtmlEscaped() ) );
  }
  m_lastError = errorMsg;
  setRunStage( tr( "Failed" ) );
  onLlmFinished();
}

} // namespace sicnu::agent
