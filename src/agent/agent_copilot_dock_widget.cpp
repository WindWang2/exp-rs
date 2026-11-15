// src/agent/agent_copilot_dock_widget.cpp
#include "agent_copilot_dock_widget.h"
#include "interaction_tool_registry.h"
#include "llm_settings_dialog.h"
#include "output_verifier.h"
#include "workspace_snapshot.h"
#include "workspace_state.h"

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "runtime/native_conversation_runtime.h"
#include "runtime/pi_conversation_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgsrubberband.h>
#include <qgsproject.h>

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUuid>

#include <stdexcept>
#include <algorithm>
#include <utility>

namespace sicnu::agent
{

namespace
{
sicnu::TaskCenter &bindTaskCenterAuthority( sicnu::TaskCenter &authority )
{
  if ( !sicnu::TaskCenter::bindSharedInstance( authority ) )
    throw std::logic_error( "Bind the host TaskCenter before initializing the agent module" );
  return authority;
}
}

AgentCopilotDockWidget::AgentCopilotDockWidget( QWidget *parent, sicnu::TaskCenter &taskCenter,
                                              const QString &installationPrefix )
  : QDockWidget( tr( "AI Copilot Assistant" ), parent )
  , m_taskCenter( bindTaskCenterAuthority( taskCenter ) )
  , m_installationPrefix( installationPrefix )
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
  m_providerCombo->setToolTip( tr( "Select AI model service profile" ) );
  m_settingsBtn = new QPushButton( tr( "Settings" ), mainWidget );
  m_settingsBtn->setToolTip( tr( "Open AI Copilot model and connection settings" ) );
  m_clearBtn = new QPushButton( tr( "Clear conversation" ), mainWidget );
  m_clearBtn->setToolTip( tr( "Clear conversation history" ) );

  headerLayout->addWidget( new QLabel( tr( "Model:" ), mainWidget ) );
  headerLayout->addWidget( m_providerCombo, 1 );
  headerLayout->addWidget( m_settingsBtn );
  headerLayout->addWidget( m_clearBtn );

  mainLayout->addLayout( headerLayout );

  // 2. Run Inspector (collapsible, lightweight observability panel)
  setupRunInspector();
  if ( m_runInspector.container )
    mainLayout->addWidget( m_runInspector.container.data() );

  m_includeWorkspaceContext = new QCheckBox(
    tr( "Share workspace context (asset names, paths, CRS, extent)" ), mainWidget );
  m_includeWorkspaceContext->setObjectName( QStringLiteral( "agentIncludeWorkspaceContext" ) );
  m_includeWorkspaceContext->setToolTip(
    tr( "Off by default. When enabled, the model request includes asset names and paths, "
        "the active layer, CRS, map extent and band metadata. Review before sending to a remote provider." ) );
  m_includeWorkspaceContext->setAccessibleDescription( m_includeWorkspaceContext->toolTip() );
  m_includeWorkspaceContext->setChecked(
    QSettings().value( QStringLiteral( "agent/includeWorkspaceContext" ), false ).toBool() );
  connect( m_includeWorkspaceContext, &QCheckBox::toggled, this, []( bool checked ) {
    QSettings().setValue( QStringLiteral( "agent/includeWorkspaceContext" ), checked );
  } );
  mainLayout->addWidget( m_includeWorkspaceContext );

  // 3. Chat Scroll Area
  m_scrollArea = new QScrollArea( mainWidget );
  m_scrollArea->setWidgetResizable( true );
  m_chatContainer = new QWidget( m_scrollArea );
  m_chatLayout = new QVBoxLayout( m_chatContainer );
  m_chatLayout->setAlignment( Qt::AlignTop );
  m_chatLayout->setSpacing( 8 );
  m_chatContainer->setLayout( m_chatLayout );
  makeWelcomeCard();
  m_scrollArea->setWidget( m_chatContainer );

  mainLayout->addWidget( m_scrollArea, 1 );

  // 3. Bottom Input Bar
  auto *inputLayout = new QHBoxLayout();
  m_inputEdit = new QTextEdit( mainWidget );
  m_inputEdit->setObjectName( QStringLiteral( "agentPromptInput" ) );
  m_inputEdit->setAccessibleName( tr( "Remote-sensing instruction" ) );
  m_inputEdit->setPlaceholderText( tr( "Enter an RS instruction (e.g. compute NDVI on the current Landsat image)..." ) );
  m_inputEdit->setFixedHeight( 60 );

  m_sendBtn = new QPushButton( tr( "Send" ), mainWidget );
  m_sendBtn->setProperty( "primary", true );
  m_sendBtn->setToolTip( tr( "Send RS instruction (Ctrl+Enter)" ) );
  m_sendBtn->setFixedHeight( 60 );

  inputLayout->addWidget( m_inputEdit, 1 );
  inputLayout->addWidget( m_sendBtn );

  mainLayout->addLayout( inputLayout );
  setWidget( mainWidget );

  m_toolHost = new runtime::AgentToolHost( m_taskCenter, this );
  connectToolHost();
  m_backendCombo = new QComboBox( mainWidget );
  m_backendCombo->setObjectName( QStringLiteral( "agentConversationBackend" ) );
  m_backendCombo->addItem( tr( "Native agent" ), QStringLiteral( "native" ) );
  m_backendCombo->addItem( tr( "Pi agent" ), QStringLiteral( "pi" ) );
  const QString backend = QSettings().value( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) ).toString();
  m_backendCombo->setCurrentIndex( backend == QStringLiteral( "pi" ) ? 1 : 0 );
  headerLayout->insertWidget( 0, m_backendCombo );
  createConversationRuntime();
  connect( m_backendCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]() {
    if ( m_isStreaming )
      return;
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), m_backendCombo->currentData() );
    createConversationRuntime();
  } );
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
  connect( QgsProject::instance(), &QgsProject::cleared, this, [this] {
    m_projectContextInvalid = true;
    m_toolHost->invalidateProject();
    onClearClicked();
  } );

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


}

void AgentCopilotDockWidget::setContext( data::DataManager *dataManager, QgsMapCanvas *canvas )
{
  QPointer<AgentCopilotDockWidget> safeThis( this );
  if ( !m_toolHost->setContext( dataManager,
       [safeThis]( const std::string &action, const Json::Value &args ) {
         if ( safeThis ) return safeThis->handleCanvasAction( action, args );
         Json::Value error( Json::objectValue );
         error["status"] = "error";
         error["errorMessage"] = "The canvas owner has closed";
         return error;
       }, []( const std::string &name, const Json::Value &args ) {
         return InteractionToolRegistry::instance().execute( name, args );
       } ) )
    return;
  m_dataManager = dataManager;
  m_canvas = canvas;
  AgentServices::instance().setDataManager( dataManager );
  AgentServices::instance().setMapCanvas( canvas );
  m_viewControlService.setDataManager( dataManager );
  m_viewControlService.setMapCanvas( canvas );
  m_rasterDisplayService.setDataManager( dataManager );
  m_rasterDisplayService.setMapCanvas( canvas );
  InteractionToolRegistry::instance().registerBuiltinTools( &m_viewControlService, &m_rasterDisplayService, dataManager );
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
  m_runInspector.titleLabel = new QLabel( tr( "Run monitor" ), container );
  titleLayout->addWidget( m_runInspector.titleLabel.data(), 1 );

  auto *toggleBtn = new QPushButton( tr( "Collapse" ), container );
  toggleBtn->setObjectName( QStringLiteral( "agentRunInspectorToggle" ) );
  toggleBtn->setFlat( true );
  titleLayout->addWidget( toggleBtn );
  layout->addLayout( titleLayout );

  auto makeLabel = [container]( const QString &text ) {
    auto *label = new QLabel( text, container );
    label->setWordWrap( true );
    return label;
  };

  m_runInspector.stageLabel = makeLabel( tr( "Phase: -" ) );
  m_runInspector.taskLabel = makeLabel( tr( "Task: -" ) );
  m_runInspector.callsLabel = makeLabel( tr( "Calls: 0" ) );
  m_runInspector.errorsLabel = makeLabel( tr( "Errors: 0" ) );
  m_runInspector.durationLabel = makeLabel( tr( "Elapsed: 0s" ) );

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
    toggleBtn->setText( m_runInspector.expanded ? tr( "Collapse" ) : tr( "Expand" ) );
  } );
  toggleBtn->click(); // Start compact until a run begins or the user expands it.
}

void AgentCopilotDockWidget::updateRunInspector()
{
  if ( !m_runInspector.container )
    return;

  if ( m_runInspector.titleLabel )
  {
    m_runInspector.titleLabel->setText(
      m_currentRunId.isEmpty()
        ? tr( "Run monitor" )
        : QString( tr( "Run monitor — %1" ) ).arg( m_currentRunId.left( 8 ) ) );
  }
  if ( m_runInspector.stageLabel )
    m_runInspector.stageLabel->setText( QString( tr( "Phase: %1" ) ).arg( m_currentRunStage.isEmpty() ? QStringLiteral( "-" ) : m_currentRunStage ) );

  QString taskText = tr( "Task: -" );
  if ( !m_submittedTaskIds.isEmpty() )
  {
    const long latestTaskId = *std::max_element( m_submittedTaskIds.cbegin(), m_submittedTaskIds.cend() );
    const sicnu::AlgorithmTaskInfo info = m_taskCenter.getTaskInfo( latestTaskId );
    taskText = QString( tr( "Task: %1 (ID %2)" ) ).arg( info.algorithmId.isEmpty() ? QStringLiteral( "-" ) : info.algorithmId ).arg( latestTaskId );
  }
  if ( m_runInspector.taskLabel )
    m_runInspector.taskLabel->setText( taskText );

  if ( m_runInspector.callsLabel )
  {
    QString callsText = QString( tr( "Calls: %1" ) ).arg( m_toolCallCards.size() );
    if ( m_refusedToolCallCount > 0 )
    {
      callsText += tr( " (%1 refused by the transport, never run)" )
                   .arg( m_refusedToolCallCount );
    }
    m_runInspector.callsLabel->setText( callsText );
  }

  int errorCount = m_lastError.isEmpty() ? 0 : 1;
  if ( m_runInspector.errorsLabel )
  {
    m_runInspector.errorsLabel->setText(
      QString( tr( "Errors: %1%2" ) )
        .arg( errorCount )
        .arg( errorCount ? QStringLiteral( " — %1" ).arg( m_lastError ) : QString() ) );
  }

  if ( m_runInspector.durationLabel && m_runStartTime.isValid() )
  {
    const qint64 elapsedSecs = m_runStartTime.secsTo( QDateTime::currentDateTimeUtc() );
    m_runInspector.durationLabel->setText( QString( tr( "Elapsed: %1s" ) ).arg( elapsedSecs ) );
  }
}

void AgentCopilotDockWidget::setRunStage( const QString &stage )
{
  m_currentRunStage = stage;
  updateRunInspector();
}

void AgentCopilotDockWidget::cancelCurrentRunTasks()
{
  if ( m_currentRunId.isEmpty() || m_cancelRequested ) return;
  m_cancelRequested = true;
  setRunStage( tr( "Canceling" ) );
  m_toolHost->cancelRun( m_currentRunId );
  if ( m_runtime ) m_runtime->cancelRun();
}

bool AgentCopilotDockWidget::stopForProjectChange( int timeoutMs )
{
  if ( !m_isStreaming ) return true;
  cancelCurrentRunTasks();
  QElapsedTimer deadline;
  deadline.start();
  while ( m_isStreaming && deadline.elapsed() < std::clamp( timeoutMs, 0, 30000 ) )
  {
    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents, 20 );
    QThread::msleep( 1 );
  }
  return !m_isStreaming;
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

  }
}

void AgentCopilotDockWidget::onClearClicked()
{
  if ( m_isStreaming ) cancelCurrentRunTasks();
  m_hideRunDelivery = true;
  m_currentReasoningLabel = nullptr;
  m_currentContentLabel = nullptr;
  m_toolCallCards.clear();
  m_approvalButtons.clear();
  QLayoutItem *child;
  while ( ( child = m_chatLayout->takeAt( 0 ) ) != nullptr )
  {
    if ( child->widget() ) child->widget()->deleteLater();
    delete child;
  }
  if ( !m_isStreaming )
  {
    if ( m_runtime ) m_runtime->closeSession();
    createConversationRuntime();
    m_currentRunId.clear();
    m_submittedTaskIds.clear();
    m_submittedPipelineIds.clear();
    m_currentRunStage.clear();
    m_lastError.clear();
  }
  makeWelcomeCard();
  updateRunInspector();
}

void AgentCopilotDockWidget::makeWelcomeCard()
{
  m_welcomeLabel = new QLabel(
    tr( "Ask the copilot to inspect data, explain a workflow or draft a remote-sensing operation.\n"
        "For example: \"Explain NDVI on the current raster\" or \"Draft a change-detection workflow\".\n\n"
        "Workspace context is not shared until you enable it above. Write-shaped tool calls still require approval." ),
    m_chatContainer );
  m_welcomeLabel->setObjectName( QStringLiteral( "agentWelcomeLabel" ) );
  m_welcomeLabel->setWordWrap( true );
  m_welcomeLabel->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
  m_chatLayout->addWidget( m_welcomeLabel );
}

void AgentCopilotDockWidget::onSendClicked()
{
  if ( m_isStreaming )
  {
    cancelCurrentRunTasks();
    return;
  }
  const QString prompt = m_inputEdit->toPlainText().trimmed();
  if ( prompt.isEmpty() ) return;
  m_inputEdit->clear();
  sendPrompt( prompt );
}

void AgentCopilotDockWidget::sendPrompt( const QString &promptText )
{
  if ( m_isStreaming || promptText.trimmed().isEmpty() ) return;
  if ( m_projectContextInvalid )
  {
    setContext( m_dataManager, m_canvas );
    m_projectContextInvalid = false;
  }
  const auto catalog = m_toolHost->catalogSnapshot();
  if ( !catalog.error.isEmpty() )
  {
    appendRefusalNotice( catalog.error );
    return;
  }
  m_currentRunId = QUuid::createUuid().toString( QUuid::WithoutBraces );
  m_submittedTaskIds.clear();
  m_submittedPipelineIds.clear();
  m_lastError.clear();
  m_refusedToolCallCount = 0;
  m_runRepairAttempts = 0;
  m_runStartTime = QDateTime::currentDateTimeUtc();
  m_toolCallCards.clear();
  m_approvalButtons.clear();
  m_cancelRequested = false;
  m_hideRunDelivery = false;
  m_conversationSettled = false;
  m_hostIdle = true;
  m_pendingPrompt = promptText;
  m_currentReasoningLabel = nullptr;
  m_currentContentLabel = nullptr;
  m_toolRevision = catalog.revision;
  if ( m_welcomeLabel ) m_welcomeLabel->hide();
  appendUserMessageCard( promptText );
  setRunStage( tr( "Preparing agent" ) );
  m_isStreaming = true;
  setRunControlsEnabled( false );
  QString systemPrompt = QStringLiteral(
    "[WORKSPACE CONTEXT]\nWorkspace context sharing is disabled by the user. "
    "Do not assume any project assets, layer paths or map state. Ask for clarification when needed." );
  if ( m_includeWorkspaceContext && m_includeWorkspaceContext->isChecked() )
    systemPrompt = WorkspaceSnapshot::capture( m_dataManager, m_canvas,
      m_canvas && m_canvas->currentLayer() ? m_canvas->currentLayer()->name() : QString(),
      m_rasterDisplayService.displayRevision() ).toSystemPromptHeader();
  runtime::ConversationConfig config;
  config.profile = LlmConfigManager::activeProfile();
  config.sessionId = QUuid::createUuid().toString( QUuid::WithoutBraces );
  config.systemPrompt = systemPrompt;
  config.toolRevision = catalog.revision;
  config.tools = catalog.tools;
  if ( m_backendCombo->currentData().toString() == QStringLiteral( "pi" ) )
    createConversationRuntime();
  m_runtime->startSession( config );
}

void AgentCopilotDockWidget::appendUserMessageCard( const QString &text )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "userBubble" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );
  auto *label = new QLabel( tr( "<b>You:</b> %1" ).arg( text.toHtmlEscaped() ), card );
  label->setObjectName( QStringLiteral( "agentUserMessage" ) );
  label->setWordWrap( true );
  label->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
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
  m_currentReasoningLabel->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
  m_currentReasoningLabel->setVisible( false );
  layout->addWidget( m_currentReasoningLabel );

  m_currentContentLabel = new QLabel( card );
  m_currentContentLabel->setWordWrap( true );
  m_currentContentLabel->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
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
    m_currentReasoningLabel->setText( tr( "<b>Reasoning:</b><br/>%1" ).arg( m_accumulatedReasoning.toHtmlEscaped() ) );
  }
}

void AgentCopilotDockWidget::onMalformedToolCall( const QJsonObject &detail )
{
  // The transport refuses a tool call it cannot hand over and says why
  // (llm_streaming_client.cpp emits {reason, finish_reason?, name?, error?}).
  // The client never executes tool calls, so a refusal is a fact about the
  // stream, not a run failure: the model may re-issue the call. It must still
  // be VISIBLE — a silently dropped call leaves the user staring at a turn
  // that produced no work, and the run inspector reporting zero errors.
  ++m_refusedToolCallCount;

  QString reason = detail.value( QStringLiteral( "reason" ) ).toString();
  if ( reason.isEmpty() )
    reason = tr( "unknown reason" );
  else
    reason = reason.toHtmlEscaped();
  const QString name = detail.value( QStringLiteral( "name" ) ).toString();
  const QString finish = detail.value( QStringLiteral( "finish_reason" ) ).toString();

  QString text = tr( "Refused a streamed tool call: %1" ).arg( reason );
  if ( !name.isEmpty() )
    text += QLatin1Char( ' ' ) + tr( "(tool: %1)" ).arg( name.toHtmlEscaped() );
  if ( !finish.isEmpty() )
    text += QLatin1Char( ' ' ) + tr( "[finish_reason=%1]" ).arg( finish.toHtmlEscaped() );

  appendRefusalNotice( text );
  updateRunInspector();
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
  // Keep the existing Qt slot for in-process callers; all admission and
  // execution, including this route, belongs to the current-project host.
  if ( m_currentRunId.isEmpty() )
  {
    const auto catalog = m_toolHost->catalogSnapshot();
    if ( !catalog.error.isEmpty() ) { appendRefusalNotice( catalog.error ); return; }
    m_currentRunId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    m_toolRevision = catalog.revision;
    if ( !m_toolHost->beginRun( m_currentRunId, m_toolRevision ) ) return;
    m_conversationSettled = true;
    m_hostIdle = false;
    m_isStreaming = true;
    m_hideRunDelivery = false;
    setRunControlsEnabled( false );
  }
  const QString id = toolCallJson.value( QStringLiteral( "id" ) ).toString();
  if ( !m_toolCallCards.contains( id ) ) m_toolCallCards.insert( id, appendToolCallCard( toolCallJson ) );
  m_toolHost->requestTool( m_currentRunId, toolCallJson, m_toolRevision );
  if ( m_conversationSettled ) m_toolHost->finishConversation( m_currentRunId );
  updateRunInspector();
}











void AgentCopilotDockWidget::appendErrorMessage( const QString &errorMsg )
{
  if ( !m_currentContentLabel ) appendAssistantMessageCard();
  if ( m_currentContentLabel )
  {
    m_currentContentLabel->setText( tr( "<font color='red'>Error: %1</font>" ).arg( errorMsg.toHtmlEscaped() ) );
  }
}

void AgentCopilotDockWidget::appendRefusalNotice( const QString &text )
{
  // Own card (not the streaming content label): a refusal outlives — and must
  // not be overwritten by — the answer tokens that keep arriving after it.
  if ( !m_chatContainer || !m_chatLayout )
    return;

  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "refusalNotice" ) );
  card->setFrameShape( QFrame::StyledPanel );
  auto *layout = new QVBoxLayout( card );

  auto *label = new QLabel( text, card );
  label->setObjectName( QStringLiteral( "RefusalNoticeLabel" ) );
  label->setWordWrap( true );
  layout->addWidget( label );

  m_chatLayout->addWidget( card );
}

QPointer<QWidget> AgentCopilotDockWidget::appendToolCallCard( const QJsonObject &toolCallJson )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "toolCallCard" ) );
  card->setFrameShape( QFrame::StyledPanel );

  auto *layout = new QVBoxLayout( card );
  QJsonObject funcObj = toolCallJson[QStringLiteral( "function" )].toObject();
  QString algName = funcObj[QStringLiteral( "name" )].toString();
  card->setProperty( "toolName", algName );

  auto *title = new QLabel( QString( tr( "Preparing to run tool: <b>%1</b>" ) ).arg( algName.toHtmlEscaped() ), card );
  title->setObjectName( QStringLiteral( "ToolCallCardTitle" ) );
  title->setWordWrap( true );
  layout->addWidget( title );

  auto *details = new QLabel( tr( "Status: submitted" ), card );
  details->setObjectName( QStringLiteral( "ToolCallCardDetails" ) );
  details->setTextFormat( Qt::PlainText );
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
    const QString baseTitle = tr( "Preparing to run tool: <b>%1</b>" )
                                  .arg( card->property( "toolName" ).toString().toHtmlEscaped() );
    title->setText( QString( "%1 <span style=\"color:%2;\">[%3]</span>" )
                      .arg( baseTitle, statusText.contains( tr( "Failed" ) ) ? QStringLiteral( "#f87171" ) : QStringLiteral( "#4ade80" ), statusText.toHtmlEscaped() ) );
  }

  auto *details = card->findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
  if ( details )
    details->setText( detailText );
}

void AgentCopilotDockWidget::appendPlanApprovalCard( const QJsonObject &planJson, const QJsonObject &toolCallJson,
                                                          const QString &token )
{
  auto *card = new QFrame( m_chatContainer );
  card->setObjectName( QStringLiteral( "planApprovalCard" ) );
  card->setFrameShape( QFrame::StyledPanel );
  auto *layout = new QVBoxLayout( card );
  auto *title = new QLabel( tr( "Review the proposed operation before running it" ), card );
  title->setWordWrap( true );
  layout->addWidget( title );
  auto *review = new QPushButton( tr( "Review parameters" ), card );
  review->setObjectName( QStringLiteral( "agentReviewPlanParameters" ) );
  review->setCheckable( true );
  auto *parameters = new QTextEdit( card );
  parameters->setObjectName( QStringLiteral( "agentPlanParameters" ) );
  parameters->setAccessibleName( tr( "Workflow plan parameters" ) );
  parameters->setReadOnly( true );
  parameters->setPlainText( QString::fromUtf8( QJsonDocument( planJson ).toJson( QJsonDocument::Indented ) ) );
  parameters->setMaximumHeight( 180 );
  parameters->hide();
  layout->addWidget( review );
  layout->addWidget( parameters );
  connect( review, &QPushButton::toggled, parameters, &QWidget::setVisible );
  auto *buttons = new QHBoxLayout();
  auto *preview = new QPushButton( tr( "Preview on canvas" ), card );
  auto *reject = new QPushButton( tr( "Reject" ), card );
  reject->setObjectName( QStringLiteral( "agentRejectOperation" ) );
  auto *run = new QPushButton( tr( "Confirm & run" ), card );
  run->setObjectName( QStringLiteral( "agentApproveOperation" ) );
  run->setProperty( "primary", true );
  buttons->addWidget( preview );
  buttons->addWidget( reject );
  buttons->addWidget( run );
  layout->addLayout( buttons );
  m_chatLayout->addWidget( card );
  const QString callId = toolCallJson.value( QStringLiteral( "id" ) ).toString();
  const QString runId = m_currentRunId;
  m_approvalButtons.insert( callId, run );
  connect( preview, &QPushButton::clicked, this, [this, planJson]() { emit viewPlanInCanvasRequested( planJson ); } );
  auto decide = [this, runId, token, run, reject, preview]( bool approved ) {
    if ( runId != m_currentRunId || m_cancelRequested ) return;
    run->setEnabled( false );
    reject->setEnabled( false );
    preview->setEnabled( false );
    run->setText( approved ? tr( "Running…" ) : tr( "Rejected" ) );
    m_toolHost->resolveApproval( token, approved );
  };
  connect( run, &QPushButton::clicked, this, [decide]() { decide( true ); } );
  connect( reject, &QPushButton::clicked, this, [decide]() { decide( false ); } );
}

AgentCopilotDockWidget::~AgentCopilotDockWidget()
{
  if ( m_runtime )
  {
    m_runtime->disconnect( this );
    m_runtime->cancelRun();
    m_runtime->closeSession();
  }
  if ( m_toolHost )
  {
    m_toolHost->disconnect( this );
    m_toolHost->cancelRun( m_currentRunId );
    m_toolHost->finishConversation( m_currentRunId );
  }
}

void AgentCopilotDockWidget::onLlmFinished()
{
  m_conversationSettled = true;
  m_toolHost->finishConversation( m_currentRunId );
  m_hostIdle = m_toolHost->isRunIdle( m_currentRunId );
  finishRunIfIdle();
}

QString AgentCopilotDockWidget::runInspectorSummary() const
{
  QStringList parts;
  parts.append( QStringLiteral( "run=%1" ).arg( m_currentRunId.isEmpty() ? QStringLiteral( "-" ) : m_currentRunId.left( 8 ) ) );
  parts.append( QStringLiteral( "stage=%1" ).arg( m_currentRunStage.isEmpty() ? QStringLiteral( "-" ) : m_currentRunStage ) );
  parts.append( QStringLiteral( "tasks=%1" ).arg( m_submittedTaskIds.size() ) );
  parts.append( QStringLiteral( "calls=%1" ).arg( m_toolCallCards.size() ) );
  if ( m_refusedToolCallCount > 0 )
    parts.append( QStringLiteral( "refused=%1" ).arg( m_refusedToolCallCount ) );
  if ( !m_lastError.isEmpty() )
    parts.append( QStringLiteral( "error=%1" ).arg( m_lastError ) );
  return parts.join( QStringLiteral( " | " ) );
}

QJsonArray AgentCopilotDockWidget::pruneHistory( const QJsonArray &history )
{
  return runtime::NativeConversationRuntime::pruneHistory( history );
}

void AgentCopilotDockWidget::onErrorOccurred( const QString &errorMsg )
{
  if ( !m_hideRunDelivery ) appendErrorMessage( errorMsg );
  m_lastError = errorMsg;
  setRunStage( tr( "Failed" ) );
}

void AgentCopilotDockWidget::createConversationRuntime()
{
  if ( m_runtime )
  {
    m_runtime->disconnect( this );
    m_runtime->closeSession();
    m_runtime->deleteLater();
  }
  if ( m_backendCombo->currentData().toString() == QStringLiteral( "pi" ) )
  {
    const QString prefix = m_installationPrefix.isEmpty()
      ? QDir::cleanPath( QCoreApplication::applicationDirPath() + QStringLiteral( "/.." ) ) : m_installationPrefix;
    m_runtime = new runtime::PiConversationRuntime( prefix, this );
  }
  else
    m_runtime = new runtime::NativeConversationRuntime( this );

  const auto *source = m_runtime;
  connect( m_runtime, &runtime::AgentConversationRuntime::ready, this, [this, source]() {
    if ( source != m_runtime || m_cancelRequested || m_pendingPrompt.isEmpty() ) return;
    if ( !m_toolHost->beginRun( m_currentRunId, m_toolRevision ) )
    {
      onErrorOccurred( tr( "Another agent is still using this project. Wait for its tasks to finish." ) );
      onLlmFinished();
      return;
    }
    m_hostIdle = false;
    const QString prompt = std::exchange( m_pendingPrompt, QString() );
    setRunStage( tr( "Understanding" ) );
    if ( !m_runtime->submitPrompt( m_currentRunId, prompt ) )
    {
      onErrorOccurred( tr( "The agent could not start the request." ) );
      onLlmFinished();
    }
  } );
  connect( m_runtime, &runtime::AgentConversationRuntime::responseStarted, this,
    [this]( const QString &run ) { if ( run == m_currentRunId && !m_hideRunDelivery ) appendAssistantMessageCard(); } );
  connect( m_runtime, &runtime::AgentConversationRuntime::reasoningDelta, this,
    [this]( const QString &run, const QString &text ) { if ( run == m_currentRunId && !m_hideRunDelivery ) onReasoningTokenReceived( text ); } );
  connect( m_runtime, &runtime::AgentConversationRuntime::contentDelta, this,
    [this]( const QString &run, const QString &text ) { if ( run == m_currentRunId && !m_hideRunDelivery ) onContentTokenReceived( text ); } );
  connect( m_runtime, &runtime::AgentConversationRuntime::toolCallRequested, this,
    [this]( const QString &run, const QJsonObject &call, const QString &revision ) {
      if ( run != m_currentRunId || m_cancelRequested ) return;
      if ( revision != m_toolRevision )
      {
        m_toolHost->requestTool( run, call, revision );
        return;
      }
      onToolCallParsed( call );
    } );
  connect( m_runtime, &runtime::AgentConversationRuntime::toolCallRefused, this,
    [this]( const QString &run, const QJsonObject &detail ) { if ( run == m_currentRunId && !m_hideRunDelivery ) onMalformedToolCall( detail ); } );
  connect( m_runtime, &runtime::AgentConversationRuntime::cancelOwnedToolsRequested, this,
    [this]( const QString &run ) {
      if ( !run.isEmpty() && run == m_currentRunId )
      {
        m_cancelRequested = true;
        m_toolHost->cancelRun( run );
        setRunStage( tr( "Canceling" ) );
      }
    } );
  connect( m_runtime, &runtime::AgentConversationRuntime::statusChanged, this,
    [this]( const QString &run, const QString &code ) {
      if ( !run.isEmpty() && run != m_currentRunId ) return;
      if ( code == QStringLiteral( "compaction_started" ) ) setRunStage( tr( "Compacting conversation" ) );
      else if ( code == QStringLiteral( "compaction_failed" ) ) appendRefusalNotice( tr( "Conversation compaction failed." ) );
    } );
  connect( m_runtime, &runtime::AgentConversationRuntime::settled, this,
    [this]( const QString &run, bool aborted, const QString &error ) {
      if ( !run.isEmpty() && run != m_currentRunId ) return;
      m_pendingPrompt.clear();
      m_cancelRequested |= aborted;
      if ( !error.isEmpty() ) onErrorOccurred( error );
      onLlmFinished();
    } );
}

void AgentCopilotDockWidget::connectToolHost()
{
  connect( m_toolHost, &runtime::AgentToolHost::approvalRequested, this,
    [this]( const QString &run, const QString &token, const QJsonObject &plan, const QJsonObject &call ) {
      if ( run != m_currentRunId || m_cancelRequested || m_hideRunDelivery ) return;
      appendPlanApprovalCard( plan, call, token );
      setRunStage( tr( "Awaiting approval" ) );
    } );
  connect( m_toolHost, &runtime::AgentToolHost::toolUpdated, this,
    [this]( const QString &run, const QString &call, const QString &state, const QString &detail ) {
      if ( run != m_currentRunId ) return;
      m_submittedTaskIds = m_toolHost->taskIds( run );
      m_submittedPipelineIds = m_toolHost->pipelineIds( run );
      if ( !m_hideRunDelivery ) updateToolCallCard( call, translatedToolState( state ), detail );
      if ( !m_cancelRequested )
        setRunStage( state == QStringLiteral( "completed" ) && !m_conversationSettled
          ? tr( "Understanding" ) : translatedToolState( state ) );
      updateRunInspector();
    } );
  connect( m_toolHost, &runtime::AgentToolHost::toolCompleted, this,
    [this]( const QString &run, const QString &callId, const QJsonObject &result ) {
      if ( run != m_currentRunId ) return;
      const bool ok = result.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "success" )
                   && result.value( QStringLiteral( "verified" ) ).toBool( true );
      QString detail = QString::fromUtf8( QJsonDocument( result ).toJson( QJsonDocument::Indented ) );
      if ( !ok && !m_cancelRequested ) m_lastError = result.value( QStringLiteral( "errorMessage" ) ).toString();
      if ( !m_hideRunDelivery )
      {
        updateToolCallCard( callId, ok ? tr( "Succeeded" ) : tr( "Failed" ), detail );
        if ( auto button = m_approvalButtons.value( callId ) )
        {
          button->setEnabled( false );
          button->setText( ok ? tr( "Completed" ) : tr( "Failed" ) );
        }
        const QString asset = result.value( QStringLiteral( "assetId" ) ).toString();
        if ( ok && !asset.isEmpty() )
        {
          Json::Value zoom( Json::objectValue );
          zoom["asset_id"] = asset.toStdString();
          m_viewControlService.zoomToAsset( zoom );
          if ( auto card = m_toolCallCards.value( callId ) )
          {
            auto *show = new QPushButton( tr( "Show result on map" ), card );
            show->setObjectName( QStringLiteral( "agentShowResult" ) );
            card->layout()->addWidget( show );
            connect( show, &QPushButton::clicked, this, [this, zoom]() { m_viewControlService.zoomToAsset( zoom ); } );
          }
        }
      }
      if ( m_runtime ) m_runtime->completeTool( run, callId, result );
      updateRunInspector();
    } );
  connect( m_toolHost, &runtime::AgentToolHost::cancelConversationRequested, this,
    [this]( const QString &run ) { if ( run == m_currentRunId ) cancelCurrentRunTasks(); } );
  connect( m_toolHost, &runtime::AgentToolHost::runIdle, this,
    [this]( const QString &run ) {
      if ( run != m_currentRunId ) return;
      m_hostIdle = true;
      finishRunIfIdle();
    } );
}

void AgentCopilotDockWidget::setRunControlsEnabled( bool enabled )
{
  m_providerCombo->setEnabled( enabled );
  m_backendCombo->setEnabled( enabled );
  m_settingsBtn->setEnabled( enabled );
  m_includeWorkspaceContext->setEnabled( enabled );
  m_sendBtn->setText( enabled ? tr( "Send ▶" ) : tr( "Stop ⏹" ) );
}

void AgentCopilotDockWidget::finishRunIfIdle()
{
  if ( !m_conversationSettled || !m_hostIdle ) return;
  m_isStreaming = false;
  setRunControlsEnabled( true );
  setRunStage( !m_lastError.isEmpty() ? tr( "Failed" ) : m_cancelRequested ? tr( "Canceled" ) : tr( "Completed" ) );
  for ( auto button : std::as_const( m_approvalButtons ) ) if ( button ) button->setEnabled( false );
}

QString AgentCopilotDockWidget::translatedToolState( const QString &state ) const
{
  if ( state == QStringLiteral( "queued" ) ) return tr( "Queued" );
  if ( state == QStringLiteral( "approval" ) ) return tr( "Awaiting approval" );
  if ( state == QStringLiteral( "preparing_approval" ) || state == QStringLiteral( "verifying_approval" ) ) return tr( "Checking Python approval identity" );
  if ( state == QStringLiteral( "running" ) ) return tr( "Running" );
  if ( state == QStringLiteral( "awaiting_commit" ) ) return tr( "Verifying and publishing result" );
  if ( state == QStringLiteral( "cancelling" ) || state == QStringLiteral( "canceling" ) ) return tr( "Canceling" );
  if ( state == QStringLiteral( "completed" ) || state == QStringLiteral( "terminal" ) ) return tr( "Completed" );
  if ( state == QStringLiteral( "rejected" ) ) return tr( "Rejected" );
  if ( state == QStringLiteral( "failed" ) ) return tr( "Failed" );
  return tr( "Running" );
}

QJsonArray AgentCopilotDockWidget::messageHistory() const
{
  const auto *native = qobject_cast<runtime::NativeConversationRuntime *>( m_runtime );
  return native ? native->messageHistory() : QJsonArray();
}

} // namespace sicnu::agent
