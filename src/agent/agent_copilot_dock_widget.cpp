// src/agent/agent_copilot_dock_widget.cpp
#include "agent_copilot_dock_widget.h"
#include "llm_settings_dialog.h"
#include "workspace_snapshot.h"

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"

#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgsrubberband.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>

namespace sicnu::agent
{

AgentCopilotDockWidget::AgentCopilotDockWidget( QWidget *parent )
  : QDockWidget( QStringLiteral( "🤖 AI Copilot 智能助手" ), parent )
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
  m_toolCallDispatcher.setDataManager( dataManager );
  // Wire the agent→canvas write-back seam (ADR 0021 sibling). canvas: actions
  // (draw_roi) run synchronously in-process and never reach Task Center.
  m_toolCallDispatcher.setCanvasActionHandler(
    [this]( const std::string &action, const Json::Value &args ) {
      return handleCanvasAction( action, args );
    } );
}

Json::Value AgentCopilotDockWidget::handleCanvasAction( const std::string &action,
                                                        const Json::Value &arguments )
{
  Json::Value result( Json::objectValue );
  result["action"] = action;

  if ( action != "draw_roi" )
  {
    result["status"] = "error";
    result["errorMessage"] = "Unknown canvas action: " + action +
                             " (only 'draw_roi' is supported)";
    return result;
  }

  if ( !m_canvas )
  {
    result["status"] = "error";
    result["errorMessage"] = "No active map canvas to draw on";
    return result;
  }

  QgsMapCanvas *canvas = m_canvas;
  // Drawing a QgsRubberBand mutates the QGraphicsScene, which is not
  // thread-safe — canvas actions must run on the canvas's (GUI) thread.
  if ( QThread::currentThread() != canvas->thread() )
  {
    result["status"] = "error";
    result["errorMessage"] = "canvas: actions must run on the GUI thread";
    return result;
  }

  const QgsCoordinateReferenceSystem crs = canvas->mapSettings().destinationCrs();
  if ( !crs.isValid() )
  {
    result["status"] = "error";
    result["errorMessage"] = "Canvas has no valid CRS; cannot place a ROI";
    return result;
  }

  // Parse the ROI geometry: prefer a WKT `geometry` string; fall back to a
  // `bbox` object {xmin, ymin, xmax, ymax} of numeric values. CRS is the canvas
  // CRS by default (the agent reads it from the workspace snapshot).
  QgsGeometry geom;
  const Json::Value &geometryArg = arguments["geometry"];
  const Json::Value &bboxArg = arguments["bbox"];
  if ( geometryArg.isString() && !geometryArg.asString().empty() )
  {
    geom = QgsGeometry::fromWkt(
      QString::fromStdString( geometryArg.asString() ) );
  }
  else if ( bboxArg.isObject() && bboxArg["xmin"].isNumeric() &&
            bboxArg["ymin"].isNumeric() && bboxArg["xmax"].isNumeric() &&
            bboxArg["ymax"].isNumeric() )
  {
    const QgsRectangle rect( bboxArg["xmin"].asDouble(), bboxArg["ymin"].asDouble(),
                             bboxArg["xmax"].asDouble(), bboxArg["ymax"].asDouble() );
    geom = QgsGeometry::fromRect( rect );
  }

  if ( geom.isNull() || geom.isEmpty() )
  {
    result["status"] = "error";
    result["errorMessage"] =
      "ROI geometry is missing or invalid; provide 'geometry' (WKT) or "
      "'bbox' {xmin,ymin,xmax,ymax} (numbers) in canvas CRS";
    return result;
  }

  // Replace any previous band: QgsRubberBand is a QGraphicsItem owned by the
  // canvas scene (not QObject-parented), so it persists until explicitly
  // removed — deleting the old band before drawing a new one keeps one ROI on
  // screen and stops the scene accumulating stale items.
  delete m_canvasRoiBand;
  m_canvasRoiBand = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
  m_canvasRoiBand->setColor( QColor( 255, 80, 0, 120 ) );
  m_canvasRoiBand->setStrokeColor( QColor( 255, 80, 0 ) );
  m_canvasRoiBand->setToGeometry( geom, crs );
  m_canvasRoiBand->show();
  canvas->refresh();

  // Store the ROI (WKT, canvas CRS) for later tool calls to consume.
  m_lastCanvasRoiWkt = geom.asWkt();

  result["status"] = "success";
  result["geometry"] = m_lastCanvasRoiWkt.toStdString();
  result["crs"] = crs.authid().toStdString();
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
  QString systemPrompt = WorkspaceSnapshot::capture(
                           m_dataManager, m_canvas,
                           m_canvas && m_canvas->currentLayer()
                             ? m_canvas->currentLayer()->name()
                             : QString() )
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

  // The caller owns execution policy, so tool selection lives here: export
  // the algorithm catalog and hand the transport the exact schemas to put on
  // the wire (ADR 0049). Conversion reuses the shared Json↔QVariant helper.
  const Json::Value cppTools = processing::AtomicAlgorithmRegistry::instance().exportOpenAiToolDefinitions();
  const QJsonArray tools = QJsonArray::fromVariantList( processing::jsonValueToVariant( cppTools ).toList() );

  m_client->sendChatCompletion( m_messageHistory, tools );
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
    appendPlanApprovalCard( QJsonObject::fromVariantMap( processing::jsonObjectToVariantMap( planArgs ) ) );
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
  // via the watcher (never blocks the GUI thread). Outputs are committed and
  // layers auto-displayed by the dispatcher; the completion payload has no
  // further consumer since the toolExecutionFinished signal was removed as
  // dead (ADR 0047).
  QString error;
  const bool ok = m_toolCallDispatcher.submit( cppEnvelope, []( const Json::Value &/*resultPayload*/ ) {}, &error );

  if ( !ok )
  {
    handleToolCallRejection( error );
  }
}

void AgentCopilotDockWidget::handleToolCallRejection( const QString &errorMsg )
{
  appendErrorMessage( errorMsg.isEmpty() ? QStringLiteral( "工具调用失败。" ) : errorMsg );
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
    // Execute the approved plan asynchronously. AgentWorkflowExecutor owns
    // pipeline watching and marshals the completion callback onto this
    // widget's thread — no detached std::thread (ADR 0047).
    m_workflowExecutor.executeAgentPlanAsync( processing::jsonValueFromQJson( planJson ),
                                              [this]( const Json::Value &resultPayload ) {
      // Completion payload shape is owned by the workflow executor; read it
      // in Json-land instead of round-tripping through QJson (ADR 0048).
      const Json::Value resultObj = resultPayload.isObject() ? resultPayload : Json::Value( Json::objectValue );
      if ( resultObj["status"].asString() != "success" )
      {
        appendErrorMessage( QString::fromStdString( resultObj["errorMessage"].asString() ) );
      }
    }, this );
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
