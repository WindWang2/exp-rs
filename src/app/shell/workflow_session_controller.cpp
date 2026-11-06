/***************************************************************************
 * workflow_session_controller.cpp  —  bridge WorkflowRuntime ↔ TaskPanelHost
 ***************************************************************************/
#include "workflow_session_controller.h"

#include "task_panel_host.h"
#include "operator_preflight.h"

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

#include "help/error_diagnostics_bridge.h"
#include "help/help_registry.h"

#include <QWidget>

#include <exception>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
// F20 (error diagnostics): when a failure message carries a machine code that
// resolves to a curated diagnostics page, pin that topic on the panel (F1
// over the failed panel opens it) and append the link to the shown text.
// Messages without a curated code stay untouched — no guessing.
QString withDiagnosticLink( QWidget *panel, const QString &error,
                            const QString &hintTemplate )
{
    if ( error.isEmpty() )
        return error;
    const sicnu::help::ErrorDiagnosticsBridge bridge( sicnu::help::globalHelpRegistry() );
    const QString helpId = bridge.helpIdFromMessage( error );
    if ( helpId.isEmpty() )
        return error;
    if ( panel )
        panel->setProperty( "helpId", helpId );
    return QStringLiteral( "%1\n%2" ).arg( error, hintTemplate.arg( helpId ) );
}
} // namespace

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
  // R28-T04 (WP05): rebinding must REPLACE the wiring — the old code just
  // reassigned the pointer, so a detached-but-alive old panel still fired
  // onRunClicked against the NEW panel's form.
  if ( m_panel.data() == panel )
    return;
  if ( m_panel )
  {
    disconnect( m_runClickedConn );
    disconnect( m_stopClickedConn );
    disconnect( m_formValuesConn );
  }
  m_panel = panel;
  m_runConnected = false;
  if ( m_panel )
  {
    if ( !m_layerIds.isEmpty() || !m_layerNames.isEmpty() )
      m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
    ensureRunConnected();
    // R28-T04 (WP04): editing a parameter invalidates the result summary
    // still on screen — the form no longer shows the parameters that
    // produced it.
    m_formValuesConn =
      connect( m_panel->form(), &SchemaFormBuilder::valuesChanged, this,
               &WorkflowSessionController::invalidateStaleResult );
  }
}

void WorkflowSessionController::bindCanvas( sicnu::workflow::gui::PipelineCanvasWidget *canvas )
{
  // R28-T04 (WP05): a double bind duplicated every connection (double
  // run-up-to handling, double status updates); a rebind to a different
  // canvas leaked the old canvas' connections.
  if ( m_canvas.data() == canvas )
    return;
  if ( m_canvas )
  {
    disconnect( m_runUpToConn );
    disconnect( m_statusConn );
    disconnect( m_nodeSelConn );
  }
  m_canvas = canvas;
  // The sync identity belongs to the previous canvas — force a resync on
  // the next run/select so a stale signature match cannot skip it.
  m_syncedSessionId.clear();
  m_syncedCanvasDefinitionId.clear();
  m_syncedCanvasSignature.clear();
  if ( m_canvas )
  {
    m_runUpToConn =
      connect( m_canvas, &sicnu::workflow::gui::PipelineCanvasWidget::runUpToNodeRequested,
               this, &WorkflowSessionController::runUpToNode );
    m_statusConn =
      connect( this, &WorkflowSessionController::stepStatusChanged,
               m_canvas, &sicnu::workflow::gui::PipelineCanvasWidget::updateStepStatus );
    // Workbench 10.0: node selection shows that step's parameter form in the
    // TaskPanelHost — the editor's parameter inspector rides the existing
    // single schema-form surface instead of building a second one.
    m_nodeSelConn =
      connect( m_canvas, &sicnu::workflow::gui::PipelineCanvasWidget::nodeSelected,
               this, &WorkflowSessionController::selectStep );
  }
}

void WorkflowSessionController::selectStep( const QString &stepId )
{
  if ( stepId.isEmpty() )
    return;
  // R28-T04 (WP05): a node click on a freshly loaded canvas used to dead-
  // end here — no session existed until the first full RUN created one, so
  // the node → form seam stayed dead after Open. Bootstrap the canvas
  // definition session exactly like a run would.
  if ( m_activeSession.isEmpty() )
  {
    if ( !syncCanvasDefinitionForRun( nullptr ) )
      return;
  }
  if ( m_activeSession.isEmpty() || !m_panel )
    return;
  const auto def = m_runtime.findDefinitionShared( m_runtime.state(
                                                       m_activeSession.toStdString() )
                                                       .definitionId );
  if ( !def )
    return;
  const StepDef *step = findStep( def.get(), stepId.toStdString() );
  if ( !step )
    return;
  // Same rendering path as openTool, scoped to the chosen step: gotoStep
  // moves the session cursor, then the form is rebuilt from that step.
  if ( !m_runtime.gotoStep( m_activeSession.toStdString(), stepId.toStdString() ) )
    return;

  m_activeStepId = stepId;
  QString title = QString::fromStdString( step->title );
  QString helpSummary;
  Json::Value schema( Json::objectValue );
  if ( step->kind == StepKind::Operator && !step->operatorId.empty() )
  {
    if ( auto op = RSOperatorRegistry::instance().create( step->operatorId ) )
    {
      schema = sicnu::app::applyEnumSourceAnnotations( op->schema() );
      helpSummary = QString::fromStdString( op->description() );
      if ( title.isEmpty() )
        title = QString::fromStdString( op->displayName() );
    }
    else
    {
      helpSummary = tr( "Operator not registered: %1" ).arg( QString::fromStdString( step->operatorId ) );
    }
  }
  if ( title.isEmpty() )
    title = QString::fromStdString( step->id );

  Json::Value currentParams;
  try
  {
    const auto snap = m_runtime.state( m_activeSession.toStdString() );
    if ( snap.paramsByStep.isObject() && snap.paramsByStep.isMember( stepId.toStdString() ) )
      currentParams = snap.paramsByStep[stepId.toStdString()];
  }
  catch ( const std::exception & )
  {
    // Unknown session: still render the form from the definition itself.
  }
  const QString operatorHelpContext =
    step->operatorId.empty() ? QString() : QString::fromStdString( step->operatorId );
  m_panel->showTool( title, helpSummary, schema, operatorHelpContext );
  if ( currentParams.isObject() && !currentParams.empty() )
    m_panel->setFormValues( currentParams );
  // R25-D: the panel's preflight/estimate line goes live — the sidecar-derived
  // knowledge agents select algorithms with now reaches the human before Run.
  m_panel->setEstimate( sicnu::app::operatorPreflightSummary( step->operatorId ) );
  m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
  emit statusMessage( tr( "Node selected: %1" ).arg( title ) );
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
  // R25-D: same preflight line on the single-tool surface — one knowledge
  // source (AlgorithmMetaStore sidecars), no per-site re-derivation.
  m_panel->setEstimate( sicnu::app::operatorPreflightSummary( step->operatorId ) );
  m_panel->setRasterLayerChoices( m_layerIds, m_layerNames );
  ensureRunConnected();

  emit statusMessage( tr( "Opened tool: %1" ).arg( title ) );
  return m_activeSession;
}

QString WorkflowSessionController::openBareOperator( const QString &operatorId )
{
  if ( operatorId.isEmpty() || !m_panel )
    return {};
  auto op = RSOperatorRegistry::instance().create( operatorId.toStdString() );
  if ( !op )
  {
    emit statusMessage( tr( "Operator not registered: %1" ).arg( operatorId ) );
    return {};
  }
  sicnu::workflow::WorkflowDefinition def;
  def.id = QStringLiteral( "rsop.%1" ).arg( operatorId ).toStdString();
  def.title = op->displayName();
  sicnu::workflow::StepDef step;
  step.id = "step1";
  step.title = op->displayName();
  step.operatorId = operatorId.toStdString();
  step.artifactOnSuccess = "output";
  def.steps.push_back( step );
  // Capture the id BEFORE the move: a moved-from std::string is valid but
  // unspecified, so reading def.id afterwards would open the tool with an
  // empty definition id (clang-tidy bugprone-use-after-move).
  const std::string definitionId = def.id;
  m_runtime.registerDefinition( std::move( def ) );
  return openTool( QString::fromStdString( definitionId ) );
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
  m_runClickedConn =
    connect( m_panel, &TaskPanelHost::runClicked,
             this, &WorkflowSessionController::onRunClicked );
  // #704: Stop affordance where the run started — cancel the in-flight step
  // task or the whole tracked pipeline.
  m_stopClickedConn = connect( m_panel, &TaskPanelHost::stopClicked, this, [this]() {
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

  // A refused submission (taskId < 0) never produces a terminal callback,
  // so arming the in-flight flag here would brick every entry the
  // duplicate-submission guard now shares (runFullWorkflow/runUpToNode/
  // onRunClicked). Fail closed to idle with a typed message instead.
  if ( taskId < 0 )
  {
    m_panel->setFailed( tr( "Task submission refused" ) );
    emit statusMessage( tr( "Task submission refused" ) );
    return;
  }
  m_runInFlight = true;
  m_cancelRequested = false; // swarm-review APP-S-2: a fresh submission supersedes any pending cancel
  m_pendingTaskId = taskId;
  // R28-T04 (R2 F-1): a new run supersedes every armed tracking slot — a
  // cancelled-but-still-armed PIPELINE's late step events must not stomp
  // this job's bookkeeping (and vice versa at pipeline submission below).
  m_activePipelineId = -1;
  // R28-T04 (WP05): capture the session/step AT SUBMISSION — the terminal
  // event may land long after the user opened another tool, and its result
  // belongs HERE, not to whatever the panel shows by then.
  m_pendingSessionId = m_activeSession;
  m_pendingStepId = m_activeStepId;
  m_pendingLoadToMap = m_panel->loadResultToMap();
  m_panel->setRunning( true );
  emit stepStatusChanged( m_activeStepId, "running" );
  emit statusMessage( tr( "Running..." ) );

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

bool WorkflowSessionController::syncCanvasDefinitionForRun( QString *reason )
{
    auto *scene = m_canvas ? m_canvas->pipelineScene() : nullptr;
    if ( !scene || scene->nodes().empty() )
    {
        // No canvas content: an open session stays authoritative; without one
        // the caller reports the desync instead of silently doing nothing.
        if ( m_activeSession.isEmpty() && reason )
            *reason = tr( "The workflow canvas is empty — add steps before running." );
        return !m_activeSession.isEmpty();
    }

    // Document-as-truth: the loaded definition supplies id/title/host, the
    // export rebuilds the steps (params/gates backfilled by APP-R-1).
    sicnu::workflow::WorkflowDefinition canvasDef = scene->lastLoadedDefinition();
    if ( canvasDef.id.empty() )
    {
        canvasDef.id = "canvas";
        canvasDef.title = tr( "Canvas Workflow" ).toStdString();
    }
    canvasDef = scene->exportWorkflowDefinition( canvasDef );

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string signature =
        Json::writeString( builder, sicnu::workflow::workflowDefinitionToJson( canvasDef ) );

    // Register under a canvas-scoped id so builtin definitions are never
    // overwritten by a canvas edit.
    canvasDef.id += ".canvas";
    const QString canvasDefinitionId = QString::fromStdString( canvasDef.id );

    if ( !m_activeSession.isEmpty() && m_activeSession == m_syncedSessionId
         && m_syncedCanvasDefinitionId == canvasDefinitionId
         && m_syncedCanvasSignature == signature )
    {
        return true; // canvas unchanged since the last sync — session is current
    }

    m_runtime.registerDefinition( canvasDef );
    const std::string previousSession = m_activeSession.toStdString();
    const std::string sessionId = m_runtime.open( canvasDef.id );
    if ( sessionId.empty() )
    {
        if ( reason )
            *reason = tr( "Cannot open a workflow session for the canvas definition." );
        return false;
    }
    if ( !previousSession.empty() )
        m_runtime.close( previousSession );

    m_activeSession = QString::fromStdString( sessionId );
    m_syncedSessionId = m_activeSession;
    m_syncedCanvasDefinitionId = canvasDefinitionId;
    m_syncedCanvasSignature = signature;

    // Keep the panel's step cursor valid when the selected step was deleted
    // from the canvas between runs.
    const auto def = m_runtime.findDefinitionShared( canvasDef.id );
    if ( m_activeStepId.isEmpty() || !findStep( def.get(), m_activeStepId.toStdString() ) )
    {
        m_activeStepId = ( def && !def->steps.empty() )
                             ? QString::fromStdString( def->steps.front().id )
                             : QString();
    }
    return true;
}

void WorkflowSessionController::runFullWorkflow()
{
  // One pipeline in flight at a time (duplicate-submission guard): a second
  // startTrackedPipeline would overwrite m_activePipelineId and orphan the
  // first run — Stop could then only cancel the latest.
  if ( m_runInFlight )
  {
    emit statusMessage( tr( "A workflow run is already in flight — stop it before starting another" ) );
    return;
  }

  // swarm-review APP-R-2: the canvas Run button executed whatever definition
  // the session happened to hold (deleted steps included), or silently did
  // nothing when no session was open. Sync the canvas into the runtime first
  // so Run executes what the canvas shows.
  QString syncReason;
  if ( !syncCanvasDefinitionForRun( &syncReason ) )
  {
    emit statusMessage( syncReason );
    return;
  }

  if ( m_activeSession.isEmpty() )
  {
    emit statusMessage( tr( "No workflow session is open — open a tool or load a workflow on the canvas first." ) );
    return;
  }

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

  // R28-T04 (WP05): one parameter store. The panel's current form values
  // are pushed into the session first, then the session's accumulated
  // params are overlaid onto the definition this pipeline executes — the
  // old path ran the loaded JSON's params, silently ignoring form edits.
  if ( m_panel && !m_activeStepId.isEmpty() )
  {
    try
    {
      m_runtime.setParams( m_activeSession.toStdString(), m_activeStepId.toStdString(),
                           m_panel->formValues() );
    }
    catch ( const std::exception & )
    {
      // Session churned between the sync and here — the snapshot overlay
      // below still carries the params pushed before the churn.
    }
  }
  sicnu::workflow::WorkflowDefinition defToRun = *def;
  try
  {
    overlayPanelParams( defToRun, m_runtime.state( m_activeSession.toStdString() ).paramsByStep );
  }
  catch ( const std::exception & )
  {
    // No session params to overlay — run the document as loaded.
  }

  for ( const auto &step : def->steps )
  {
    emit stepStatusChanged( QString::fromStdString( step.id ), "idle" );
  }

  // R28-T04 (WP05): the pipeline belongs to the session it started from.
  // R2 F-1: submission supersedes the OTHER type's armed tracking (a
  // cancelled-but-still-armed single job's late terminal must not clear
  // the live pipeline's flags/captured session mid-run).
  m_pendingSessionId = m_activeSession;
  m_pendingStepId.clear();
  m_pendingTaskId = -1;
  m_pendingLoadToMap = false; // R2 F-6: no stale single-job choice seeds pipeline loads

  // Tracked submission (#697): every step transition persists an atomic
  // checkpoint, interrupted runs recover at startup, and ArtifactGC sweeps
  // intermediates once the run completes.
  m_activePipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( defToRun, /*autoLoad=*/false );
  if ( m_activePipelineId < 0 )
  {
    emit statusMessage( tr( "Workflow DAG submission failed" ) );
    return;
  }

  m_runInFlight = true;
  m_cancelRequested = false; // swarm-review APP-S-2: a fresh submission supersedes any pending cancel
  if ( m_panel )
    m_panel->setRunning( true );
  emit statusMessage( tr( "Workflow TaskPipeline submitted to the Task Center for execution..." ) );
}

void WorkflowSessionController::runUpToNode( const QString &targetStepId )
{
  // Duplicate-submission guard FIRST (R28-T04, WP05): the sync below
  // CLOSES and reopens the live session when the canvas changed — running
  // it before the guard let a rejected partial run destroy the in-flight
  // run's session. Same guard as runFullWorkflow: the partial-DAG pipeline
  // shares m_activePipelineId with the full one.
  if ( m_runInFlight )
  {
    emit statusMessage( tr( "A workflow run is already in flight — stop it before starting another" ) );
    return;
  }
  // swarm-review APP-R-2: same canvas→runtime sync as runFullWorkflow, so a
  // partial run also executes the canvas state (not a stale session snapshot).
  QString syncReason;
  if ( !syncCanvasDefinitionForRun( &syncReason ) )
  {
    emit statusMessage( syncReason );
    return;
  }
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

  // R28-T04 (WP05): same one-parameter-store discipline as the full run.
  if ( m_panel && !m_activeStepId.isEmpty() )
  {
    try
    {
      m_runtime.setParams( m_activeSession.toStdString(), m_activeStepId.toStdString(),
                           m_panel->formValues() );
    }
    catch ( const std::exception & )
    {
    }
  }
  sicnu::workflow::WorkflowDefinition targetDef = *def;
  try
  {
    overlayPanelParams( targetDef, m_runtime.state( m_activeSession.toStdString() ).paramsByStep );
  }
  catch ( const std::exception & )
  {
  }
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
  // R28-T04 (WP05): the pipeline belongs to the session it started from.
  // R2 F-1/F-6: same supersede/clear discipline as the full run.
  m_pendingSessionId = m_activeSession;
  m_pendingStepId.clear();
  m_pendingTaskId = -1;
  m_pendingLoadToMap = false;
  m_activePipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( targetDef, /*autoLoad=*/false );
  if ( m_activePipelineId < 0 )
  {
    emit statusMessage( tr( "Workflow DAG submission failed" ) );
    return;
  }

  m_runInFlight = true;
  m_cancelRequested = false; // swarm-review APP-S-2: a fresh submission supersedes any pending cancel
  if ( m_panel )
    m_panel->setRunning( true );
  emit statusMessage( tr( "Workflow TaskPipeline (up to %1) submitted for execution..." ).arg( targetStepId ) );
}

void WorkflowSessionController::cancelActiveRun()
{
  // swarm-review APP-S-2: cancelling used to clear the tracked ids
  // synchronously, so work that had already finished when the cancel landed
  // (or that finalizes as Canceled) could no longer match onTaskUpdated: the
  // Completed result was dropped — no session artifact, no committed output,
  // orphan temp files. Only REQUEST the cancel here; the terminal callback
  // owns the ids and performs commitTaskOutput / applyJobResultToSession
  // before resetting them.
  const bool hadTrackedWork = ( m_activePipelineId >= 0 || m_pendingTaskId >= 0 );

  if ( m_activePipelineId >= 0 )
    sicnu::TaskCenter::instance().cancelPipeline( m_activePipelineId );

  if ( m_pendingTaskId >= 0 )
    sicnu::TaskCenter::instance().cancelTask( m_pendingTaskId );

  m_runInFlight = false;
  m_cancelRequested = hadTrackedWork;
  if ( m_panel )
    m_panel->setRunning( false );

  emit statusMessage( m_cancelRequested ? tr( "Workflow run stopping..." )
                                        : tr( "Workflow run stopped" ) );
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

  // R28-T04 (WP05): single-job results target the step captured at
  // submission — not whatever the panel happens to show now.
  QString targetStepId = isSingleJob
                           ? ( m_pendingStepId.isEmpty() ? m_activeStepId : m_pendingStepId )
                           : info.stepId;
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

  // swarm-review APP-S-2: the tracked work reached a terminal state, so the
  // cancelling window is over. Reset the flag here (not in cancelActiveRun)
  // so a Completed result that raced the cancel still runs the bookkeeping
  // below — the ids it needs were deliberately left armed.
  m_cancelRequested = false;

  // R28-T04 (WP05): the session this run belongs to MUST be captured
  // BEFORE the single-job bookkeeping clears it below (R1 P1-1: computing
  // it after the clear read an already-emptied id, making the capture dead
  // code and committing late results into whatever session the user had
  // switched to).
  const QString sessionQ =
    m_pendingSessionId.isEmpty() ? m_activeSession : m_pendingSessionId;
  const bool panelShowsThisJob =
    !m_panel.isNull() && sessionQ == m_activeSession
    && ( !isSingleJob || targetStepId == m_activeStepId );

  if ( isSingleJob )
  {
    m_pendingTaskId = -1;
    m_pendingSessionId.clear();
    m_pendingStepId.clear();
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
    else
    {
      error = withDiagnosticLink( m_panel, error,
                                   tr( "Diagnostic help: press F1 for guidance (topic %1)" ) );
    }
    emit stepStatusChanged( targetStepId, "failed" );
    if ( isPipelineJob )
    {
      m_runInFlight = false;
      m_activePipelineId = -1;
      // R28-T04 (WP05): a pipeline failure the user has switched away
      // from must not clobber the tool form they are looking at.
      if ( panelShowsThisJob && m_panel )
        m_panel->setFailed( error );
    }
    else if ( panelShowsThisJob && isSingleJob && m_panel )
    {
      m_panel->setFailed( error );
    }
    emit statusMessage( error );
    return;
  }

  // Structured result: surface the operator result JSON in the shared
  // summary view (UX 4.0 task/result contract).
  if ( panelShowsThisJob && isSingleJob && info.resultPayload.isObject()
       && !info.resultPayload.empty() )
  {
    m_panel->showResult( info.resultPayload, info.algorithmId );
    m_lastResultStepId = targetStepId; // staleness invalidation anchor
  }

  const std::string sessionId = sessionQ.toStdString();
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
      // R1 P2-4: same switch-away guard as the failure path — a pipeline
      // terminal message must not stamp over a form the user opened since.
      if ( panelShowsThisJob && m_panel )
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

  // clear a failure-pinned diagnostic topic so F1 no longer opens it
  if ( m_panel )
    m_panel->setProperty( "helpId", {} );

  const QString msg = outputPath.isEmpty()
                        ? tr( "Run Succeeded" )
                        : tr( "Run succeeded: %1" ).arg( outputPath );
  if ( panelShowsThisJob && m_panel ) // R1 P2-4: switch-away guard
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


void WorkflowSessionController::invalidateStaleResult()
{
  // WP04 (R28-T04): the displayed result was produced by parameters that
  // no longer match the form — drop it and demote the node's success
  // state so the UI never claims the edits have run.
  if ( m_runInFlight || m_lastResultStepId.isEmpty() )
    return;
  if ( m_lastResultStepId != m_activeStepId )
    return;
  if ( m_panel )
    m_panel->clearResult();
  emit stepStatusChanged( m_lastResultStepId, "idle" );
  m_lastResultStepId.clear();
}

void WorkflowSessionController::overlayPanelParams( sicnu::workflow::WorkflowDefinition &def,
                                                    const Json::Value &paramsByStep )
{
  if ( !paramsByStep.isObject() )
    return;
  for ( auto &step : def.steps )
  {
    const std::string key = step.id;
    if ( !paramsByStep.isMember( key ) || !paramsByStep[key].isObject() )
      continue;
    const Json::Value &edited = paramsByStep[key];
    for ( const auto &member : edited.getMemberNames() )
      step.params[member] = edited[member];
  }
}


std::vector<QString> WorkflowSessionController::completedStepIds( const QString &sessionId ) const
{
  std::vector<QString> out;
  try
  {
    const auto snap = m_runtime.state( sessionId.toStdString() );
    for ( const auto &id : snap.completedStepIds )
      out.push_back( QString::fromStdString( id ) );
  }
  catch ( const std::exception & )
  {
    // Unknown session: empty answer (callers treat as no completions).
  }
  return out;
}
