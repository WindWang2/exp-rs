/***************************************************************************
 * workflow_session_controller.cpp  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#include "workflow_session_controller.h"

#include "task_panel_host.h"

#include "data/data_manager.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/output_committer_task_center.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/builtin_definitions.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_types.h"
#include "workflow/pipeline_canvas_widget.h"
#include "workflow/pipeline_scene.h"
#include "workflow/pipeline_node_item.h"
#include "workflow/pipeline_port_item.h"

#include <exception>
#include <string>
#include <unordered_set>
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
  , m_runtime()
{
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &WorkflowSessionController::onTaskUpdated, Qt::QueuedConnection );
}

void WorkflowSessionController::registerBuiltins()
{
  if ( m_builtinsRegistered )
    return;
  sicnu::workflow::registerBuiltinWorkflows( m_runtime );
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

void WorkflowSessionController::setDataManager( sicnu::data::DataManager *dataManager )
{
  m_dataManager = dataManager;
}

sicnu::data::TemporaryReapResult WorkflowSessionController::reapTaskTemporaries()
{
  if ( m_dataManager )
  {
    return m_dataManager->reapTaskTemporaries();
  }
  return {};
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
    m_panel->setFailed( tr( "Workflow definition not found: %1" ).arg( definitionId ) );
    emit statusMessage( tr( "Workflow definition not found: %1" ).arg( definitionId ) );
    return {};
  }

  m_activeSession = QString::fromStdString( sessionId );

  const auto def = m_runtime.findDefinitionShared( defId );
  if ( !def || def->steps.empty() )
  {
    m_panel->setFailed( tr( "Workflow has no steps: %1" ).arg( definitionId ) );
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

  const StepDef *step = findStep( def.get(), stepId );
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
      // Workbench 9.0 M6: bind dynamic enum sources at the shell boundary.
      // Operator schemas keep declaring plain x-ui-type ports; the host
      // annotates model/asset ports so the form resolves them live through
      // the WorkbenchEnumProvider (ModelCatalog / DataManager) instead of
      // rendering them as bare text inputs. Layer ports stay on the proven
      // push channel (setRasterLayerChoices / setVectorLayerChoices).
      schema = sicnu::app::applyEnumSourceAnnotations( schema );
      helpSummary = QString::fromStdString( op->description() );
      if ( title.isEmpty() )
        title = QString::fromStdString( op->displayName() );
    }
    else
    {
      helpSummary = tr( "Operator not registered: %1" ).arg( QString::fromStdString( step->operatorId ) );
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
    helpSummary = tr( "Workspace '%1' is open. Complete the interactive steps in the dedicated window;"
                      "The task panel shows parameters of operator steps (e.g. rs:obia_segment / rs:obia_classify)." )
                    .arg( QString::fromStdString( def->workspaceKind ) );
  }

  // Unified Help 6.0: pass the operator id so the form enriches each field
  // with parameter knowledge (unit / recommended / trade-off) from data/help.
  const QString operatorHelpContext =
      step->operatorId.empty() ? QString() : QString::fromStdString( step->operatorId );
  m_panel->showTool( title, helpSummary, schema, operatorHelpContext );
  m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
  ensureRunConnected();

  emit statusMessage( tr( "Opened tool: %1" ).arg( title ) );
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
  // #704: Stop affordance where the run started — cancel the in-flight step
  // task or the whole tracked pipeline.
  connect( m_panel, &TaskPanelHost::stopClicked, this, [this]() {
    if ( m_activePipelineId > 0 )
      sicnu::workflow::WorkflowRunCoordinator::instance().cancelRun( m_activePipelineId );
    else if ( m_pendingTaskId > 0 )
      sicnu::TaskCenter::instance().cancelTask( m_pendingTaskId );
  } );
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

  const auto def = m_runtime.findDefinitionShared( definitionId );
  const StepDef *step = findStep( def.get(), stepId );
  if ( !step || step->kind != StepKind::Operator || step->operatorId.empty() )
  {
    m_panel->setFailed( tr( "The current step is not a runnable operator" ) );
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
  emit statusMessage( tr( "Running..." ) );

  if ( taskId >= 0 )
  {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
    {
      onTaskUpdated( info );
    }
  }
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

  const auto def = m_runtime.findDefinitionShared( definitionId );
  if ( !def )
    return;

  for ( const auto &step : def->steps )
  {
    emit stepStatusChanged( QString::fromStdString( step.id ), "idle" );
  }

  // Tracked submission (#697): every step transition persists an atomic
  // checkpoint, interrupted runs recover at startup, and ArtifactGC sweeps
  // intermediates once the run completes.
  m_activePipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( *def, /*autoLoad=*/false );
  if ( m_activePipelineId < 0 )
  {
    emit statusMessage( tr( "Workflow DAG submission failed" ) );
    return;
  }

  m_runInFlight = true;
  if ( m_panel )
    m_panel->setRunning( true );
  emit statusMessage( tr( "Workflow TaskPipeline submitted to the Task Center for execution..." ) );
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

  const auto def = m_runtime.findDefinitionShared( definitionId );
  if ( !def )
    return;

  sicnu::workflow::WorkflowDefinition targetDef = *def;
  std::vector<std::string> ordered;
  std::string error;
  if ( topologicalSortSteps( *def, ordered, error ) )
  {
    std::unordered_set<std::string> keepStepIds;
    for ( const auto &sId : ordered )
    {
      keepStepIds.insert( sId );
      if ( sId == targetStepId.toStdString() )
        break;
    }
    std::vector<sicnu::workflow::StepDef> steps;
    for ( const auto &s : targetDef.steps )
    {
      if ( keepStepIds.count( s.id ) )
        steps.push_back( s );
    }
    targetDef.steps = steps;
  }

  // Tracked submission (#697): every step transition persists an atomic
  // checkpoint, interrupted runs recover at startup, and ArtifactGC sweeps
  // intermediates once the run completes.
  m_activePipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( targetDef, /*autoLoad=*/false );
  if ( m_activePipelineId < 0 )
  {
    emit statusMessage( tr( "Workflow DAG submission failed" ) );
    return;
  }

  m_runInFlight = true;
  if ( m_panel )
    m_panel->setRunning( true );
  emit statusMessage( tr( "Workflow TaskPipeline (up to %1) submitted for execution..." ).arg( targetStepId ) );
}

void WorkflowSessionController::cancelActiveRun()
{
  if ( m_activePipelineId >= 0 )
  {
    sicnu::TaskCenter::instance().cancelPipeline( m_activePipelineId );
    m_activePipelineId = -1;
  }

  if ( m_pendingTaskId >= 0 )
  {
    sicnu::TaskCenter::instance().cancelTask( m_pendingTaskId );
    m_pendingTaskId = -1;
  }

  m_runInFlight = false;
  if ( m_panel )
    m_panel->setRunning( false );

  emit statusMessage( tr( "Workflow run stopped" ) );
}

void WorkflowSessionController::stopWorkflow()
{
  cancelActiveRun();
}

void WorkflowSessionController::onTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  bool isSingleJob = ( m_pendingTaskId >= 0 && info.taskId == m_pendingTaskId );
  bool isPipelineJob = ( m_activePipelineId >= 0 && info.pipelineId == m_activePipelineId );

  if ( !isSingleJob && !isPipelineJob )
    return;

  QString targetStepId = isSingleJob ? m_activeStepId : info.stepId;
  if ( targetStepId.isEmpty() && isSingleJob )
    targetStepId = m_activeStepId;

  if ( info.status == sicnu::TaskStatus::Running
       || info.status == sicnu::TaskStatus::WaitingResource
       || info.status == sicnu::TaskStatus::Dispatching
       || info.status == sicnu::TaskStatus::Cancelling )
  {
    emit stepStatusChanged( targetStepId, "running" );
    return;
  }

  if ( info.status != sicnu::TaskStatus::Completed
       && info.status != sicnu::TaskStatus::Failed
       && info.status != sicnu::TaskStatus::Canceled )
    return;

  if ( isSingleJob )
  {
    m_pendingTaskId = -1;
    m_runInFlight = false;
    if ( m_panel )
      m_panel->setRunning( false );
  }

  if ( info.status != sicnu::TaskStatus::Completed )
  {
    QString error = info.errorMessage;
    if ( error.isEmpty() )
    {
      if ( info.status == sicnu::TaskStatus::Canceled )
        error = tr( "Cancelled" );
      else
        error = tr( "Run Failed" );
    }
    emit stepStatusChanged( targetStepId, "failed" );
    if ( isPipelineJob )
    {
      m_runInFlight = false;
      m_activePipelineId = -1;
      if ( m_panel )
        m_panel->setFailed( error );
    }
    else if ( m_panel && isSingleJob )
    {
      m_panel->setFailed( error );
    }
    emit statusMessage( error );
    return;
  }

  // Structured result: surface the operator result JSON in the shared
  // summary view (UX 4.0 task/result contract).
  if ( m_panel && isSingleJob && info.resultPayload.isObject()
       && !info.resultPayload.empty() )
  {
    m_panel->showResult( info.resultPayload, info.algorithmId );
  }

  const std::string sessionId = m_activeSession.toStdString();
  const std::string stepIdStr = targetStepId.toStdString();
  if ( !sessionId.empty() && !stepIdStr.empty() )
    applyJobResultToSession( sessionId, stepIdStr, info.resultPayload );

  emit stepStatusChanged( targetStepId, "success" );

  QString outputPath;
  if ( info.resultPayload.isMember( "output" ) && info.resultPayload["output"].isString() )
    outputPath = QString::fromStdString( info.resultPayload["output"].asString() );

  if ( !outputPath.isEmpty() && m_dataManager )
  {
    sicnu::OutputCommitter committer( m_dataManager, this );
    sicnu::commitTaskOutput( &committer,
                             &sicnu::TaskCenter::instance(),
                             info.taskId,
                             sicnu::data::AssetKind::Raster,
                             outputPath,
                             sicnu::data::PersistencePolicy::TaskTemporary,
                             /*autoLoad=*/false,
                             {} );
  }

  bool shouldLoadToMap = m_pendingLoadToMap;
  if ( m_canvas && m_canvas->pipelineScene() )
  {
    auto *nodeItem = m_canvas->pipelineScene()->findNode( targetStepId );
    if ( nodeItem )
    {
      for ( auto *outPort : nodeItem->outputPorts() )
      {
        if ( outPort && outPort->addToMap() )
        {
          shouldLoadToMap = true;
          break;
        }
      }
    }
  }

  if ( shouldLoadToMap && !outputPath.isEmpty() )
    emit requestLoadRaster( outputPath );

  if ( isPipelineJob )
  {
    const auto pipeInfo = sicnu::TaskCenter::instance().getPipelineInfo( m_activePipelineId );
    if ( pipeInfo.isCompleted )
    {
      m_runInFlight = false;
      m_activePipelineId = -1;
      const QString msg = pipeInfo.isFailed
                            ? tr( "Workflow ended (some steps failed)" )
                            : tr( "Workflow finished end to end!" );
      if ( m_panel )
      {
        if ( pipeInfo.isFailed )
          m_panel->setFailed( msg );
        else
          m_panel->setSuccess( msg );
      }
      emit statusMessage( msg );
    }
    return;
  }

  const QString msg = outputPath.isEmpty()
                        ? tr( "Run Succeeded" )
                        : tr( "Run succeeded: %1" ).arg( outputPath );
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

  const auto def = m_runtime.findDefinitionShared( definitionId );
  const StepDef *step = findStep( def.get(), stepId );

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
