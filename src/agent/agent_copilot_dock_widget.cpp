// src/agent/agent_copilot_dock_widget.cpp
#include "agent_copilot_dock_widget.h"
#include "llm_settings_dialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointer>
#include <thread>

namespace sicnu::agent
{

AgentCopilotDockWidget::AgentCopilotDockWidget( QWidget *parent )
  : QDockWidget( QStringLiteral( "🤖 AI Copilot 智能助手" ), parent )
  , m_toolCallDispatcher(
      // Production sink: submit typed tool calls to TaskCenter for background
      // scheduling, progress, and cancel support (ADR 0016 / ADR 0021). The
      // output is committed by OutputCommitter on completion, so auto-loading
      // the raw task output path here would race the commit's file move.
      []( const QString &algorithmId, const QVariantMap &params ) -> long {
        return sicnu::TaskCenter::instance().enqueueTask(
          algorithmId, params, /*autoLoad=*/false, sicnu::TaskPriority::Normal,
          QList<long>(), /*autoDispatch=*/true );
      },
      // Production watcher: observe TaskCenter completion signals. taskUpdated
      // is emitted from the JobEngine watcher thread; the queued connection to
      // onTaskCenterTaskUpdated marshals the callback onto the GUI thread.
      [this]( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete ) {
        watchToolCallCompletion( taskId, std::move( onComplete ) );
      } )
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
  m_settingsBtn = new QPushButton( QStringLiteral( "⚙️ 设置" ), mainWidget );
  m_clearBtn = new QPushButton( QStringLiteral( "🧹 清空对话" ), mainWidget );

  headerLayout->addWidget( new QLabel( QStringLiteral( "模型:" ), mainWidget ) );
  headerLayout->addWidget( m_providerCombo, 1 );
  headerLayout->addWidget( m_settingsBtn );
  headerLayout->addWidget( m_clearBtn );

  mainLayout->addLayout( headerLayout );

  // 2. Chat Scroll Area
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
  m_inputEdit->setPlaceholderText( QStringLiteral( "输入遥感指令 (例: 对当前 Landsat 图像计算 NDVI)..." ) );
  m_inputEdit->setFixedHeight( 60 );

  m_sendBtn = new QPushButton( QStringLiteral( "发送 ▶" ), mainWidget );
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

void AgentCopilotDockWidget::setContext( data::DataManager *dataManager, ActiveViewHost *viewHost )
{
  m_dataManager = dataManager;
  m_viewHost = viewHost;
  m_workflowExecutor.setDataManager( dataManager );
  m_toolCallDispatcher.setDataManager( dataManager );
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
    m_client->cancel();
    onLlmFinished();
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
  appendUserMessageCard( promptText );

  // Build Workspace System Context
  QJsonObject snapshot = AgentContextResolver::buildContextSnapshot( m_dataManager, m_viewHost );
  QString systemPrompt = AgentContextResolver::formatSystemContextPrompt( snapshot );

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

  m_client->sendChatCompletion( m_messageHistory, true );
}

void AgentCopilotDockWidget::appendUserMessageCard( const QString &text )
{
  auto *card = new QFrame( m_chatContainer );
  card->setFrameShape( QFrame::StyledPanel );
  card->setStyleSheet( QStringLiteral( "background-color: #0284c7; color: white; border-radius: 6px; padding: 6px;" ) );

  auto *layout = new QVBoxLayout( card );
  auto *label = new QLabel( QString( "<b>你:</b> %1" ).arg( text.toHtmlEscaped() ), card );
  label->setWordWrap( true );
  layout->addWidget( label );

  m_chatLayout->addWidget( card );
}

void AgentCopilotDockWidget::appendAssistantMessageCard()
{
  auto *card = new QFrame( m_chatContainer );
  card->setFrameShape( QFrame::StyledPanel );
  card->setStyleSheet( QStringLiteral( "background-color: #1e293b; color: #f8fafc; border-radius: 6px; padding: 6px;" ) );

  auto *layout = new QVBoxLayout( card );

  m_currentReasoningLabel = new QLabel( card );
  m_currentReasoningLabel->setWordWrap( true );
  m_currentReasoningLabel->setStyleSheet( QStringLiteral( "color: #94a3b8; font-style: italic;" ) );
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
  m_currentReasoningLabel->setVisible( true );
  m_currentReasoningLabel->setText( QString( "🧠 思考过程:\n%1" ).arg( m_accumulatedReasoning.toHtmlEscaped() ) );
}

void AgentCopilotDockWidget::onContentTokenReceived( const QString &text )
{
  m_accumulatedContent += text;
  m_currentContentLabel->setText( m_accumulatedContent );
}

void AgentCopilotDockWidget::onToolCallParsed( const QJsonObject &toolCallJson )
{
  Json::Value cppEnvelope;
  std::string jsonStr = QJsonDocument( toolCallJson ).toJson( QJsonDocument::Compact ).toStdString();
  Json::CharReaderBuilder builder;
  std::string errs;
  std::istringstream sstream( jsonStr );
  Json::parseFromStream( builder, sstream, &cppEnvelope, &errs );

  // The dispatcher owns parsing, classification, id normalization, and
  // submission (ADR 0021). Plan requests keep the existing approval flow.
  const auto classification = m_toolCallDispatcher.classify( cppEnvelope );
  if ( classification == processing::ToolCallClassification::PlanRequest )
  {
    appendPlanApprovalCard( planArgumentsFor( toolCallJson ) );
    return;
  }

  appendToolCallCard( toolCallJson );

  if ( classification == processing::ToolCallClassification::Invalid )
  {
    // rejectionReason reports the same reason submit() would give, without
    // the side-effect of a doomed submission.
    handleToolCallRejection( m_toolCallDispatcher.rejectionReason( cppEnvelope ) );
    return;
  }

  // Single tool call: submit asynchronously; the completion payload arrives
  // via the watcher (never blocks the GUI thread).
  QString error;
  const bool ok = m_toolCallDispatcher.submit( cppEnvelope, [this]( const Json::Value &resultPayload ) {
    QJsonDocument doc = QJsonDocument::fromJson( QByteArray::fromStdString( resultPayload.toStyledString() ) );
    QJsonObject resultJson = doc.isObject() ? doc.object() : QJsonObject();
    emit toolExecutionFinished( resultJson );
  }, &error );

  if ( !ok )
  {
    handleToolCallRejection( error );
  }
}

void AgentCopilotDockWidget::handleToolCallRejection( const QString &errorMsg )
{
  appendErrorMessage( errorMsg.isEmpty() ? QStringLiteral( "工具调用失败。" ) : errorMsg );
  QJsonObject errorResult;
  errorResult[QStringLiteral( "status" )] = QStringLiteral( "error" );
  errorResult[QStringLiteral( "error" )] = errorMsg;
  emit toolExecutionFinished( errorResult );
}

QJsonObject AgentCopilotDockWidget::planArgumentsFor( const QJsonObject &toolCallJson ) const
{
  QJsonObject funcObj = toolCallJson[QStringLiteral( "function" )].toObject();

  QJsonObject argsObj;
  const QJsonValue funcArgs = funcObj[QStringLiteral( "arguments" )];
  if ( funcArgs.isObject() )
    argsObj = funcArgs.toObject();
  else if ( funcArgs.isString() )
  {
    QJsonDocument doc = QJsonDocument::fromJson( funcArgs.toString().toUtf8() );
    if ( doc.isObject() )
      argsObj = doc.object();
  }
  else if ( toolCallJson[QStringLiteral( "arguments" )].isObject() )
    argsObj = toolCallJson[QStringLiteral( "arguments" )].toObject();

  if ( argsObj.contains( QStringLiteral( "steps" ) ) )
    return argsObj;
  if ( !funcObj.isEmpty() )
    return funcObj;
  return toolCallJson;
}

void AgentCopilotDockWidget::watchToolCallCompletion( long taskId, processing::ToolCallDispatcher::CompletionCallback onComplete )
{
  // The task may already be terminal (completed synchronously): deliver the
  // payload immediately instead of waiting for a signal that already fired.
  const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( isTerminalStatus( info.status ) )
  {
    onComplete( processing::ToolCallDispatcher::buildTaskResultPayload( info, m_toolCallDispatcher.outputCommitterHandler() ) );
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
  callback( processing::ToolCallDispatcher::buildTaskResultPayload( info, m_toolCallDispatcher.outputCommitterHandler() ) );
}

void AgentCopilotDockWidget::appendErrorMessage( const QString &errorMsg )
{
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( QString( "<font color='red'>错误: %1</font>" ).arg( errorMsg.toHtmlEscaped() ) );
  }
}

void AgentCopilotDockWidget::appendToolCallCard( const QJsonObject &toolCallJson )
{
  auto *card = new QFrame( m_chatContainer );
  card->setFrameShape( QFrame::StyledPanel );
  card->setStyleSheet( QStringLiteral( "background-color: #0f172a; border: 1px solid #38bdf8; border-radius: 6px; padding: 6px;" ) );

  auto *layout = new QVBoxLayout( card );
  QJsonObject funcObj = toolCallJson[QStringLiteral( "function" )].toObject();
  QString algName = funcObj[QStringLiteral( "name" )].toString();

  auto *title = new QLabel( QString( "⚡ 准备执行工具: <b>%1</b>" ).arg( algName ), card );
  title->setStyleSheet( QStringLiteral( "color: #38bdf8;" ) );
  layout->addWidget( title );

  m_chatLayout->addWidget( card );
}

void AgentCopilotDockWidget::appendPlanApprovalCard( const QJsonObject &planJson )
{
  auto *card = new QFrame( m_chatContainer );
  card->setFrameShape( QFrame::StyledPanel );
  card->setStyleSheet( QStringLiteral( "background-color: #0f172a; border: 1px solid #10b981; border-radius: 6px; padding: 8px;" ) );

  auto *layout = new QVBoxLayout( card );

  int stepCount = 0;
  if ( planJson.contains( QStringLiteral( "steps" ) ) && planJson[QStringLiteral( "steps" )].isArray() )
    stepCount = planJson[QStringLiteral( "steps" )].toArray().size();

  auto *title = new QLabel( QString( "📋 AI Agent 提出了 <b>%1 步骤</b> 的遥感处理工作流计划" ).arg( stepCount ), card );
  title->setStyleSheet( QStringLiteral( "color: #10b981; font-weight: bold;" ) );
  layout->addWidget( title );

  auto *btnLayout = new QHBoxLayout();
  auto *previewBtn = new QPushButton( QStringLiteral( "👁️ 在画布中预览" ), card );
  auto *runBtn = new QPushButton( QStringLiteral( "▶ 确认并执行" ), card );
  runBtn->setStyleSheet( QStringLiteral( "background-color: #059669; color: white; font-weight: bold;" ) );

  btnLayout->addWidget( previewBtn );
  btnLayout->addWidget( runBtn );
  layout->addLayout( btnLayout );

  m_chatLayout->addWidget( card );

  connect( previewBtn, &QPushButton::clicked, this, [this, planJson]() {
    emit viewPlanInCanvasRequested( planJson );
  } );

  connect( runBtn, &QPushButton::clicked, this, [this, planJson]() {
    emit planApprovalRequested( planJson );

    QPointer<AgentCopilotDockWidget> self( this );
    std::thread( [self, planJson]() {
      Json::Value cppPlan;
      std::string jsonStr = QJsonDocument( planJson ).toJson( QJsonDocument::Compact ).toStdString();
      Json::CharReaderBuilder builder;
      std::string errs;
      std::istringstream sstream( jsonStr );
      Json::parseFromStream( builder, sstream, &cppPlan, &errs );

      Json::Value resultPayload = self ? self->m_workflowExecutor.executeAgentPlan( cppPlan ) : Json::Value();
      QJsonDocument doc = QJsonDocument::fromJson( QByteArray::fromStdString( resultPayload.toStyledString() ) );
      QJsonObject resultJson = doc.isObject() ? doc.object() : QJsonObject();

      if ( self )
      {
        QMetaObject::invokeMethod( self, [self, resultJson]() {
          if ( self )
          {
            emit self->toolExecutionFinished( resultJson );
          }
        }, Qt::QueuedConnection );
      }
    } ).detach();
  } );
}

void AgentCopilotDockWidget::onLlmFinished()
{
  m_isStreaming = false;
  m_sendBtn->setText( QStringLiteral( "发送 ▶" ) );
}

void AgentCopilotDockWidget::onErrorOccurred( const QString &errorMsg )
{
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( QString( "<font color='red'>错误: %1</font>" ).arg( errorMsg ) );
  }
  onLlmFinished();
}

} // namespace sicnu::agent
