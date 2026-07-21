/***************************************************************************
 * workflow_session_controller.cpp  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#include "workflow_session_controller.h"

#include "job_engine_qt_bridge.h"
#include "task_panel_host.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/builtin_definitions.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::jobs::JobState;
using sicnu::operators::RSOperatorRegistry;
using sicnu::workflow::CanRunResult;
using sicnu::workflow::StepDef;
using sicnu::workflow::StepKind;

namespace {

std::string jsonValueToArtifactString( const Json::Value &v )
{
  if ( v.isString() )
    return v.asString();
  if ( v.isNull() )
    return {};
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString( builder, v );
}

const StepDef *findStep( const sicnu::workflow::WorkflowDefinition *def, const std::string &stepId )
{
  if ( !def )
    return nullptr;
  for ( const auto &s : def->steps )
  {
    if ( s.id == stepId )
      return &s;
  }
  return nullptr;
}

} // namespace

WorkflowSessionController::WorkflowSessionController( QObject *parent )
  : QObject( parent )
  , m_registry()
  , m_runtime( m_registry )
{
  ensureJobBridgeConnected();
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

  const StepDef *step = findStep( def, stepId );
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

  m_activeTitle = title;

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

void WorkflowSessionController::ensureJobBridgeConnected()
{
  if ( m_jobBridgeConnected )
    return;
  auto *bridge = JobEngineQtBridge::instance();
  connect( bridge, &JobEngineQtBridge::jobFinished,
           this, &WorkflowSessionController::onJobFinished );
  m_jobBridgeConnected = true;
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

  // Resolve operator id from the session definition step.
  std::string definitionId;
  try
  {
    definitionId = m_runtime.state( sessionId ).definitionId;
  }
  catch ( const std::exception &e )
  {
    m_panel->setFailed( QString::fromUtf8( e.what() ) );
    return;
  }

  const StepDef *step = findStep( m_registry.find( definitionId ), stepId );
  if ( !step || step->kind != StepKind::Operator || step->operatorId.empty() )
  {
    m_panel->setFailed( tr( "当前步骤不是可运行算子" ) );
    return;
  }

  ensureJobBridgeConnected();

  JobRequest req;
  req.algorithmId = step->operatorId;
  req.params = params;
  req.title = m_activeTitle.isEmpty()
                ? ( step->title.empty() ? step->operatorId : step->title )
                : m_activeTitle.toStdString();
  req.source = "task_panel";

  const std::string jobId = JobEngine::instance().submit( std::move( req ) );

  m_runInFlight = true;
  m_pendingJobId = QString::fromStdString( jobId );
  m_pendingLoadToMap = m_panel->loadResultToMap();
  m_panel->setRunning( true );
  emit statusMessage( tr( "正在运行…" ) );
}

void WorkflowSessionController::onJobFinished( const QString &jobId )
{
  if ( m_pendingJobId.isEmpty() || jobId != m_pendingJobId )
    return;

  m_pendingJobId.clear();
  m_runInFlight = false;

  if ( m_panel )
    m_panel->setRunning( false );

  const auto snapOpt = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snapOpt )
  {
    if ( m_panel )
      m_panel->setFailed( tr( "任务记录丢失" ) );
    emit statusMessage( tr( "任务记录丢失" ) );
    return;
  }

  const auto &rec = *snapOpt;
  if ( rec.state != JobState::Succeeded )
  {
    QString error = QString::fromStdString( rec.error );
    if ( error.isEmpty() )
    {
      if ( rec.state == JobState::Cancelled )
        error = tr( "已取消" );
      else
        error = tr( "运行失败" );
    }
    if ( m_panel )
      m_panel->setFailed( error );
    emit statusMessage( error );
    return;
  }

  const std::string sessionId = m_activeSession.toStdString();
  const std::string stepId = m_activeStepId.toStdString();
  if ( !sessionId.empty() && !stepId.empty() )
    applyJobResultToSession( sessionId, stepId, rec.result );

  QString outputPath;
  if ( rec.result.isMember( "output" ) && rec.result["output"].isString() )
    outputPath = QString::fromStdString( rec.result["output"].asString() );

  if ( m_pendingLoadToMap && !outputPath.isEmpty() )
    emit requestLoadRaster( outputPath );

  const QString msg = outputPath.isEmpty()
                        ? tr( "运行成功" )
                        : tr( "运行成功：%1" ).arg( outputPath );
  if ( m_panel )
    m_panel->setSuccess( msg );
  emit statusMessage( msg );
}

void WorkflowSessionController::applyJobResultToSession( const std::string &sessionId,
                                                         const std::string &stepId,
                                                         const Json::Value &result )
{
  std::string definitionId;
  try
  {
    definitionId = m_runtime.state( sessionId ).definitionId;
  }
  catch ( const std::exception & )
  {
    return;
  }

  const StepDef *step = findStep( m_registry.find( definitionId ), stepId );

  // Mirror WorkflowRuntime::runStep artifact side-effects.
  if ( result.isMember( "output" ) && result["output"].isString() )
  {
    const std::string name =
      ( !step || step->artifactOnSuccess.empty() ) ? "output" : step->artifactOnSuccess;
    m_runtime.setArtifact( sessionId, name, result["output"].asString() );
  }
  else if ( result.isMember( "result" ) )
  {
    const std::string name =
      ( !step || step->artifactOnSuccess.empty() || step->artifactOnSuccess == "output" )
        ? "result"
        : step->artifactOnSuccess;
    m_runtime.setArtifact( sessionId, name, jsonValueToArtifactString( result["result"] ) );
  }
  else if ( step && !step->artifactOnSuccess.empty() )
  {
    m_runtime.setArtifact( sessionId, step->artifactOnSuccess, jsonValueToArtifactString( result ) );
  }

  m_runtime.markStepComplete( sessionId, stepId );
}
