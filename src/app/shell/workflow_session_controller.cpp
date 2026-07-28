/***************************************************************************
 * workflow_session_controller.cpp  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#include "workflow_session_controller.h"

#include "task_panel_host.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/builtin_definitions.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_types.h"
#include "workflow/pipeline_canvas_widget.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

using sicnu::jobs::JobRequest;
using sicnu::operators::RSOperatorRegistry;
using sicnu::workflow::CanRunResult;
using sicnu::workflow::HostKind;
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
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &WorkflowSessionController::onTaskUpdated, Qt::QueuedConnection );
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

void WorkflowSessionController::bindCanvas( sicnu::workflow::gui::PipelineCanvasWidget *canvas )
{
  m_canvas = canvas;
  if ( m_canvas )
  {
    connect( m_canvas, &sicnu::workflow::gui::PipelineCanvasWidget::runUpToNodeRequested,
             this, &WorkflowSessionController::runUpToNode );
    connect( this, &WorkflowSessionController::stepStatusChanged,
             m_canvas, &sicnu::workflow::gui::PipelineCanvasWidget::updateStepStatus );
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

  // Workspace labs (OBIA / classify) open their dedicated shell windows.
  if ( def->host == HostKind::Workspace && !def->workspaceKind.empty() )
  {
    emit requestOpenWorkspace( QString::fromStdString( def->workspaceKind ) );
    helpSummary = tr( "工作空间「%1」已打开。请在专用窗口中完成交互步骤；"
                      "任务面板可查看算子型步骤（如 rs:obia_segment / rs:obia_classify）的参数。" )
                    .arg( QString::fromStdString( def->workspaceKind ) );
  }

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

  JobRequest req;
  req.algorithmId = step->operatorId;
  req.params = params;
  req.title = m_activeTitle.isEmpty()
                ? ( step->title.empty() ? step->operatorId : step->title )
                : m_activeTitle.toStdString();
  req.source = "task_panel";

  const long taskId = sicnu::TaskCenter::instance().submitJob( req );

  m_runInFlight = true;
  m_pendingTaskId = taskId;
  m_pendingLoadToMap = m_panel->loadResultToMap();
  m_panel->setRunning( true );
  emit stepStatusChanged( m_activeStepId, "running" );
  emit statusMessage( tr( "正在运行…" ) );
}

void WorkflowSessionController::runFullWorkflow()
{
  if ( m_activeSession.isEmpty() )
    return;

  std::string definitionId;
  try
  {
    definitionId = m_runtime.state( m_activeSession.toStdString() ).definitionId;
  }
  catch ( const std::exception &e )
  {
    emit statusMessage( QString::fromUtf8( e.what() ) );
    return;
  }

  const auto *def = m_registry.find( definitionId );
  if ( !def )
    return;

  std::vector<std::string> ordered;
  std::string error;
  if ( !topologicalSortSteps( *def, ordered, error ) )
  {
    emit statusMessage( tr( "DAG排序错误: %1" ).arg( QString::fromStdString( error ) ) );
    return;
  }

  m_executionQueue = ordered;
  m_currentQueueIndex = 0;
  m_isBatchExecuting = true;

  for ( const auto &stepId : m_executionQueue )
  {
    emit stepStatusChanged( QString::fromStdString( stepId ), "idle" );
  }

  runNextQueuedStep();
}

void WorkflowSessionController::runUpToNode( const QString &targetStepId )
{
  if ( m_activeSession.isEmpty() || targetStepId.isEmpty() )
    return;

  std::string definitionId;
  try
  {
    definitionId = m_runtime.state( m_activeSession.toStdString() ).definitionId;
  }
  catch ( const std::exception &e )
  {
    emit statusMessage( QString::fromUtf8( e.what() ) );
    return;
  }

  const auto *def = m_registry.find( definitionId );
  if ( !def )
    return;

  std::vector<std::string> ordered;
  std::string error;
  if ( !topologicalSortSteps( *def, ordered, error ) )
  {
    emit statusMessage( tr( "DAG排序错误: %1" ).arg( QString::fromStdString( error ) ) );
    return;
  }

  std::vector<std::string> targetQueue;
  for ( const auto &sId : ordered )
  {
    targetQueue.push_back( sId );
    if ( sId == targetStepId.toStdString() )
      break;
  }

  m_executionQueue = targetQueue;
  m_currentQueueIndex = 0;
  m_isBatchExecuting = true;

  runNextQueuedStep();
}

void WorkflowSessionController::stopWorkflow()
{
  m_isBatchExecuting = false;
  m_executionQueue.clear();

  if ( m_pendingTaskId >= 0 )
  {
    sicnu::TaskCenter::instance().cancelTask( m_pendingTaskId );
    m_pendingTaskId = -1;
  }

  m_runInFlight = false;
  if ( m_panel )
    m_panel->setRunning( false );

  emit statusMessage( tr( "工作流运行已停止" ) );
}

void WorkflowSessionController::runNextQueuedStep()
{
  if ( !m_isBatchExecuting || m_currentQueueIndex >= m_executionQueue.size() )
  {
    m_isBatchExecuting = false;
    emit statusMessage( tr( "工作流全流程运行完成！" ) );
    return;
  }

  std::string nextStepId = m_executionQueue[m_currentQueueIndex];
  m_activeStepId = QString::fromStdString( nextStepId );

  emit stepStatusChanged( m_activeStepId, "running" );
  emit statusMessage( tr( "正在运行步骤 [%1]..." ).arg( m_activeStepId ) );

  // Execute step via TaskCenter
  std::string definitionId;
  try
  {
    definitionId = m_runtime.state( m_activeSession.toStdString() ).definitionId;
  }
  catch ( const std::exception &e )
  {
    emit statusMessage( QString::fromUtf8( e.what() ) );
    m_isBatchExecuting = false;
    return;
  }

  const StepDef *step = findStep( m_registry.find( definitionId ), nextStepId );
  if ( !step || step->kind != StepKind::Operator || step->operatorId.empty() )
  {
    // Non-operator or interactive step completed automatically
    m_runtime.markStepComplete( m_activeSession.toStdString(), nextStepId );
    emit stepStatusChanged( m_activeStepId, "success" );
    m_currentQueueIndex++;
    runNextQueuedStep();
    return;
  }

  JobRequest req;
  req.algorithmId = step->operatorId;
  req.params = ( m_panel ? m_panel->formValues() : Json::Value( Json::objectValue ) );
  req.title = step->title.empty() ? step->operatorId : step->title;
  req.source = "pipeline_editor";

  m_pendingTaskId = sicnu::TaskCenter::instance().submitJob( req );
  m_runInFlight = true;
}

void WorkflowSessionController::onTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  if ( m_pendingTaskId < 0 || info.taskId != m_pendingTaskId )
    return;
  if ( info.status != sicnu::TaskStatus::Completed
       && info.status != sicnu::TaskStatus::Failed
       && info.status != sicnu::TaskStatus::Canceled )
    return;

  m_pendingTaskId = -1;
  m_runInFlight = false;

  if ( m_panel )
    m_panel->setRunning( false );

  if ( info.status != sicnu::TaskStatus::Completed )
  {
    QString error = info.errorMessage;
    if ( error.isEmpty() )
    {
      if ( info.status == sicnu::TaskStatus::Canceled )
        error = tr( "已取消" );
      else
        error = tr( "运行失败" );
    }
    emit stepStatusChanged( m_activeStepId, "failed" );
    if ( m_panel )
      m_panel->setFailed( error );
    emit statusMessage( error );

    m_isBatchExecuting = false;
    m_executionQueue.clear();
    return;
  }

  const std::string sessionId = m_activeSession.toStdString();
  const std::string stepId = m_activeStepId.toStdString();
  if ( !sessionId.empty() && !stepId.empty() )
    applyJobResultToSession( sessionId, stepId, info.resultPayload );

  emit stepStatusChanged( m_activeStepId, "success" );

  QString outputPath;
  if ( info.resultPayload.isMember( "output" ) && info.resultPayload["output"].isString() )
    outputPath = QString::fromStdString( info.resultPayload["output"].asString() );

  if ( m_pendingLoadToMap && !outputPath.isEmpty() )
    emit requestLoadRaster( outputPath );

  const QString msg = outputPath.isEmpty()
                        ? tr( "运行成功" )
                        : tr( "运行成功：%1" ).arg( outputPath );
  if ( m_panel )
    m_panel->setSuccess( msg );
  emit statusMessage( msg );

  if ( m_isBatchExecuting )
  {
    m_currentQueueIndex++;
    runNextQueuedStep();
  }
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
