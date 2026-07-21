/***************************************************************************
 * workflow_session_controller.cpp  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#include "workflow_session_controller.h"

#include "task_panel_host.h"

#include "operators/framework/rs_operator_registry.h"
#include "workflow/builtin_definitions.h"

#include <QMetaObject>
#include <QPointer>

#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using sicnu::operators::RSOperatorRegistry;
using sicnu::workflow::CanRunResult;
using sicnu::workflow::StepDef;
using sicnu::workflow::StepKind;

WorkflowSessionController::WorkflowSessionController( QObject *parent )
  : QObject( parent )
  , m_registry()
  , m_runtime( m_registry )
{
}

void WorkflowSessionController::registerBuiltins()
{
  if ( m_builtinsRegistered )
    return;
  sicnu::workflow::registerBuiltinWorkflows( m_registry );
  m_builtinsRegistered = true;
}

void WorkflowSessionController::bindPanel( TaskPanelHost *panel )
{
  m_panel = panel;
  m_runConnected = false;
  if ( m_panel )
  {
    if ( !m_layerIds.isEmpty() || !m_layerNames.isEmpty() )
      m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
    ensureRunConnected();
  }
}

QString WorkflowSessionController::openTool( const QString &definitionId )
{
  if ( !m_panel )
    return {};

  registerBuiltins();

  const std::string defId = definitionId.toStdString();
  const std::string sessionId = m_runtime.open( defId );
  if ( sessionId.empty() )
  {
    m_panel->setFailed( tr( "未找到工作流定义：%1" ).arg( definitionId ) );
    emit statusMessage( tr( "未找到工作流定义：%1" ).arg( definitionId ) );
    return {};
  }

  m_activeSession = QString::fromStdString( sessionId );

  const auto *def = m_registry.find( defId );
  if ( !def || def->steps.empty() )
  {
    m_panel->setFailed( tr( "工作流无步骤：%1" ).arg( definitionId ) );
    return {};
  }

  std::string stepId;
  try
  {
    stepId = m_runtime.state( sessionId ).currentStepId;
  }
  catch ( const std::exception &e )
  {
    m_panel->setFailed( QString::fromUtf8( e.what() ) );
    return {};
  }

  const StepDef *step = nullptr;
  for ( const auto &s : def->steps )
  {
    if ( s.id == stepId )
    {
      step = &s;
      break;
    }
  }
  if ( !step )
    step = &def->steps.front();

  m_activeStepId = QString::fromStdString( step->id );

  QString title = QString::fromStdString( def->title );
  QString helpSummary;
  Json::Value schema( Json::objectValue );

  if ( step->kind == StepKind::Operator && !step->operatorId.empty() )
  {
    auto op = RSOperatorRegistry::instance().create( step->operatorId );
    if ( op )
    {
      schema = op->schema();
      helpSummary = QString::fromStdString( op->description() );
      if ( title.isEmpty() )
        title = QString::fromStdString( op->displayName() );
    }
    else
    {
      helpSummary = tr( "算子未注册：%1" ).arg( QString::fromStdString( step->operatorId ) );
    }
  }
  else if ( !step->title.empty() )
  {
    helpSummary = QString::fromStdString( step->title );
  }

  if ( title.isEmpty() )
    title = definitionId;

  m_panel->showTool( title, helpSummary, schema );
  m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
  ensureRunConnected();

  emit statusMessage( tr( "已打开工具：%1" ).arg( title ) );
  return m_activeSession;
}

void WorkflowSessionController::setLayerChoices( const QStringList &ids, const QStringList &names )
{
  m_layerIds = ids;
  m_layerNames = names;
  if ( m_panel )
    m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
}

void WorkflowSessionController::ensureRunConnected()
{
  if ( m_runConnected || !m_panel )
    return;
  connect( m_panel, &TaskPanelHost::runClicked,
           this, &WorkflowSessionController::onRunClicked );
  m_runConnected = true;
}

void WorkflowSessionController::onRunClicked()
{
  if ( !m_panel || m_activeSession.isEmpty() || m_activeStepId.isEmpty() )
    return;
  if ( m_runInFlight )
    return;

  const std::string sessionId = m_activeSession.toStdString();
  const std::string stepId = m_activeStepId.toStdString();

  const Json::Value params = m_panel->formValues();
  try
  {
    m_runtime.setParams( sessionId, stepId, params );
  }
  catch ( const std::exception &e )
  {
    m_panel->setFailed( QString::fromUtf8( e.what() ) );
    return;
  }

  const CanRunResult can = m_runtime.canRun( sessionId, stepId );
  if ( !can.ok )
  {
    QStringList hints;
    hints.reserve( static_cast<int>( can.hints.size() ) );
    for ( const auto &h : can.hints )
      hints.append( QString::fromStdString( h ) );
    m_panel->setHints( hints );
    return;
  }

  m_runInFlight = true;
  m_panel->setRunning( true );
  emit statusMessage( tr( "正在运行…" ) );

  const bool loadToMap = m_panel->loadResultToMap();
  // Capture panel via QPointer so completion is safe if panel is destroyed.
  QPointer<TaskPanelHost> panel = m_panel;
  QPointer<WorkflowSessionController> self = this;

  std::thread( [self, panel, sessionId, stepId, loadToMap]() {
    Json::Value result;
    QString error;
    bool ok = false;
    try
    {
      if ( self )
      {
        result = self->m_runtime.runStep( sessionId, stepId );
        ok = true;
      }
      else
      {
        error = QStringLiteral( "Session controller destroyed" );
      }
    }
    catch ( const std::exception &e )
    {
      error = QString::fromUtf8( e.what() );
    }
    catch ( ... )
    {
      error = QStringLiteral( "Unknown error" );
    }

    if ( !self )
      return;

    // Marshal completion back to the GUI thread.
    QMetaObject::invokeMethod( self, [self, panel, result, error, ok, loadToMap]() {
      if ( !self )
        return;

      self->m_runInFlight = false;

      if ( panel )
        panel->setRunning( false );

      if ( !ok )
      {
        if ( panel )
          panel->setFailed( error );
        emit self->statusMessage( error );
        return;
      }

      QString outputPath;
      if ( result.isMember( "output" ) && result["output"].isString() )
        outputPath = QString::fromStdString( result["output"].asString() );

      if ( loadToMap && !outputPath.isEmpty() )
        emit self->requestLoadRaster( outputPath );

      const QString msg = outputPath.isEmpty()
                            ? QObject::tr( "运行成功" )
                            : QObject::tr( "运行成功：%1" ).arg( outputPath );
      if ( panel )
        panel->setSuccess( msg );
      emit self->statusMessage( msg );
    }, Qt::QueuedConnection );
  } ).detach();
}
