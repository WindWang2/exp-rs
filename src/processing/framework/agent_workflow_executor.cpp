// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"

#include "workflow/workflow_run_coordinator.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/data_asset.h"
#include "task_center.h"
#include "workflow/workflow_types.h"
#include "workflow/workflow_definition.h"
#include "workflow/placeholder_grammar.h"
#include "jobs/job_types.h"

#include <QMetaObject>
#include <QString>
#include <chrono>
#include <thread>
#include <unordered_map>

#include <gdal.h>
#include <gdal_priv.h>

namespace sicnu::processing {

namespace {

std::string normalizeAlgorithmId( const std::string &rawName )
{
  std::string algorithmId = rawName;
  auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( adapter )
    return algorithmId;

  auto underscorePos = rawName.find( '_' );
  if ( underscorePos != std::string::npos )
  {
    std::string altId = rawName;
    altId[underscorePos] = ':';
    if ( AtomicAlgorithmRegistry::instance().findAdapter( altId ) )
      return altId;
  }
  return algorithmId;
}

/// Parses and normalizes a plan request into a WorkflowDefinition. Returns an
/// empty string on success, otherwise the error message. Shared by the
/// blocking and asynchronous execution paths — one owner of the parse
/// contract.
std::string preparePlanDefinition( const Json::Value &planJson, sicnu::workflow::WorkflowDefinition &def )
{
  std::string parseErr;
  if ( !sicnu::workflow::workflowDefinitionFromJson( planJson, def, parseErr ) || def.steps.empty() )
    return parseErr.empty() ? "Agent plan contained no operator steps." : parseErr;

  // Normalize algorithm IDs on parsed steps if needed
  for ( auto &step : def.steps )
  {
    step.operatorId = normalizeAlgorithmId( step.operatorId );
    if ( step.title.empty() )
      step.title = step.operatorId;
  }
  return std::string();
}

/// Error-shaped planResult skeleton used by both execution paths.
Json::Value makePlanErrorResult( int totalSteps, long pipelineId, const std::string &errorMessage )
{
  Json::Value planResult( Json::objectValue );
  planResult["status"] = "error";
  planResult["completedSteps"] = 0;
  planResult["totalSteps"] = totalSteps;
  planResult["stepResults"] = Json::Value( Json::arrayValue );
  planResult["errorMessage"] = errorMessage;
  planResult["pipelineId"] = static_cast<Json::Int64>( pipelineId );
  return planResult;
}

/// Execution Plane 7.0 (P1-E1): register each completed plan-step output in
/// place as a governed asset with a derivation record — the same contract
/// the CLI pipeline runner applies (ADR 0023 register-in-place: NO
/// temp->stable move, plan steps chain outputs by path). Registration
/// failures are recorded per step ("registrationError") and never flip the
/// step's status to success; a skipped registration (no catalog) is
/// reported as such instead of being silent.
/// THREAD AFFINITY: DataManager mutators are catalog-thread-affine. The
/// blocking plan path runs this on the CALLER's thread — callers off the
/// catalog thread get per-step "asset registration failed" entries
/// (fail-closed, visible) instead of registered assets; the async path
/// runs on the executor's event-loop thread and delivers registrations.
void stampPlanResultProvenance( data::DataManager *dataManager, long pipelineId,
                                Json::Value *planResultInOut )
{
  Json::Value &planResult = *planResultInOut;
  if ( !dataManager )
  {
    planResult["provenance"] = "skipped:no catalog wired for this agent session";
    return;
  }
  if ( !planResult.isMember( "stepResults" ) || !planResult["stepResults"].isArray() )
    return;

  auto &taskCenter = TaskCenter::instance();
  const auto pipeInfo = taskCenter.getPipelineInfo( pipelineId );
  const auto trackedRun =
    sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline( pipelineId );

  int registeredCount = 0;
  for ( const auto &stepId : pipeInfo.orderedStepIds )
  {
    if ( !pipeInfo.stepToTaskId.contains( stepId ) )
      continue;
    const long taskId = pipeInfo.stepToTaskId[stepId];
    const auto task = taskCenter.getTaskInfo( taskId );
    if ( task.status != TaskStatus::Completed || task.outputLayerPath.isEmpty() )
      continue;

    // Locate the matching step result entry.
    Json::Value *stepRes = nullptr;
    for ( auto &entry : planResult["stepResults"] )
    {
      if ( entry.isObject() && entry["stepId"].asString() == stepId )
      {
        stepRes = &entry;
        break;
      }
    }
    if ( !stepRes )
      continue;

    const QString executionFingerprint = task.resultPayload.isObject()
        && task.resultPayload.isMember( "executionFingerprint" )
        && task.resultPayload["executionFingerprint"].isString()
      ? QString::fromStdString( task.resultPayload["executionFingerprint"].asString() )
      : QString();

    // Kind probe (same contract as the CLI runner): raster vs vector by the
    // openable band count; an unopenable output is not registered.
    GDALDatasetH ds = GDALOpen( task.outputLayerPath.toUtf8().constData(), GA_ReadOnly );
    const bool isRaster = ds != nullptr && GDALGetRasterCount( ds ) > 0;
    if ( ds )
      GDALClose( ds );
    if ( !ds )
    {
      (*stepRes)["registrationError"] = "output not openable; asset not registered";
      continue;
    }

    sicnu::data::SourceDescriptor source;
    source.providerKey = isRaster ? QStringLiteral( "gdal" ) : QStringLiteral( "ogr" );
    source.canonicalSource = task.outputLayerPath;
    sicnu::data::RegisterRequest request;
    request.source = source;
    request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
    request.notifyUpdateOnReuse = true;
    request.executionFingerprint = executionFingerprint;
    const auto registered = dataManager->registerSource( request );
    if ( registered.assetId.isNull() )
    {
      (*stepRes)["registrationError"] = "asset registration failed";
      continue;
    }

    const sicnu::data::InputLineage lineage =
      sicnu::data::resolveInputLineageForParams( dataManager, task.parameterMap, { task.outputLayerPath } );
    sicnu::data::DerivationRecord derivation =
      sicnu::data::makeWorkflowDerivation(
        task.algorithmId,
        QJsonObject::fromVariantMap( task.parameterMap ),
        trackedRun ? QString::fromStdString( trackedRun->workflowId() ) : QString(),
        trackedRun ? QString::fromStdString( trackedRun->runId() ) : QString(),
        task.stepId,
        QString::number( taskId ),
        lineage.inputs,
        lineage.unresolvedPaths,
        lineage.collectionId,
        lineage.collectionRevision );
    derivation.executionFingerprint = executionFingerprint;
    dataManager->attachDerivationRecord( registered.assetId, derivation );

    (*stepRes)["assetId"] = registered.assetId.toString().toStdString();
    ++registeredCount;
  }
  planResult["registeredOutputs"] = registeredCount;
}

} // namespace

AgentWorkflowExecutor::AgentWorkflowExecutor( data::DataManager *dataManager, QObject *parent )
  : QObject( parent )
  , mDataManager( dataManager )
{
  // Single watcher for all async plans; the connection is auto-disconnected
  // when this executor is destroyed. taskUpdated is emitted outside
  // TaskCenter's mutex, so the slot may safely re-enter via getPipelineInfo.
  connect( &TaskCenter::instance(), &TaskCenter::taskUpdated,
           this, &AgentWorkflowExecutor::onTaskCenterTaskUpdated );
}

void AgentWorkflowExecutor::setDataManager( data::DataManager *dataManager )
{
  mDataManager = dataManager;
}

data::DataManager* AgentWorkflowExecutor::dataManager() const
{
  return mDataManager;
}

Json::Value AgentWorkflowExecutor::executeAgentPlan( const Json::Value &planJson, ProgressCallback progressCb )
{
  Q_UNUSED( progressCb );

  sicnu::workflow::WorkflowDefinition def;
  const std::string parseError = preparePlanDefinition( planJson, def );
  if ( !parseError.empty() )
    return makePlanErrorResult( 0, -1, parseError );

  // Tracked submission: plan execution gets checkpoint/recovery/GC (#697).
  // P1-E1 (7.0): step outputs are registered in the governed catalog with
  // derivation records after completion (stampPlanResultProvenance).
  const long pipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( def, /*autoLoad=*/false );
  if ( pipelineId < 0 )
    return makePlanErrorResult( static_cast<int>( def.steps.size() ), -1, "TaskCenter rejected the agent plan pipeline." );

  const auto pipeInfo = TaskCenter::instance().waitForPipeline( pipelineId, std::chrono::minutes( 60 ) );
  if ( pipeInfo.pipelineId < 0 )
    return makePlanErrorResult( static_cast<int>( def.steps.size() ), pipelineId, "TaskCenter pipeline execution failed" );

  Json::Value planResult = assemblePlanResult( static_cast<int>( def.steps.size() ), pipelineId, pipeInfo );
  // P1-E1 (7.0): step outputs land in the governed catalog with derivation
  // records so the agent can reference stable asset ids.
  stampPlanResultProvenance( mDataManager, pipelineId, &planResult );
  return planResult;
}

long AgentWorkflowExecutor::executeAgentPlanAsync( const Json::Value &planJson, PlanCompletionCallback callback, QObject *context )
{
  sicnu::workflow::WorkflowDefinition def;
  const std::string parseError = preparePlanDefinition( planJson, def );
  if ( !parseError.empty() )
  {
    deliverPlanResult( callback, context, makePlanErrorResult( 0, -1, parseError ) );
    return -1;
  }

  // Tracked submission: plan execution gets checkpoint/recovery/GC (#697).
  // P1-E1 (7.0): the async path stamps the same governed provenance as the
  // blocking path (see checkPendingPlan).
  const long pipelineId = sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipeline( def, /*autoLoad=*/false );
  if ( pipelineId < 0 )
  {
    deliverPlanResult( callback, context, makePlanErrorResult( static_cast<int>( def.steps.size() ), -1, "TaskCenter rejected the agent plan pipeline." ) );
    return -1;
  }

  PendingPlan pending;
  pending.callback = std::move( callback );
  pending.context = context;
  pending.totalSteps = static_cast<int>( def.steps.size() );
  m_pendingPlans.insert( pipelineId, pending );

  // The watcher is armed before the terminal-state probe: submitPipeline may
  // already have flushed a taskUpdated for a pipeline that finished
  // synchronously, so the probe catches anything that completed before the
  // watch, and every update emitted after this point reaches the slot.
  checkPendingPlan( pipelineId );
  return pipelineId;
}

void AgentWorkflowExecutor::onTaskCenterTaskUpdated( const sicnu::AlgorithmTaskInfo &/*info*/ )
{
  if ( m_pendingPlans.isEmpty() )
    return;
  const auto pipelineIds = m_pendingPlans.keys();
  for ( const long pipelineId : pipelineIds )
    checkPendingPlan( pipelineId );
}

void AgentWorkflowExecutor::checkPendingPlan( long pipelineId )
{
  auto it = m_pendingPlans.find( pipelineId );
  if ( it == m_pendingPlans.end() )
    return;

  // Terminal states mirror waitForPipeline(): pipeline gone, completed, or
  // failed/canceled.
  const auto info = TaskCenter::instance().getPipelineInfo( pipelineId );
  if ( info.pipelineId >= 0 && !info.isCompleted && !info.isFailed )
    return;

  PendingPlan pending = it.value();
  m_pendingPlans.erase( it );

  Json::Value planResult;
  if ( info.pipelineId < 0 )
    planResult = makePlanErrorResult( pending.totalSteps, pipelineId, "TaskCenter pipeline execution failed" );
  else
  {
    planResult = assemblePlanResult( pending.totalSteps, pipelineId, info );
    // P1-E1 (7.0): same governed registration as the blocking path.
    stampPlanResultProvenance( mDataManager, pipelineId, &planResult );
  }

  deliverPlanResult( pending.callback, pending.context, planResult );
}

Json::Value AgentWorkflowExecutor::assemblePlanResult( int totalSteps, long pipelineId, const sicnu::PipelineExecutionInfo &pipeInfo ) const
{
  Json::Value planResult = makePlanErrorResult( totalSteps, pipelineId, std::string() );

  int completed = 0;
  for ( auto it = pipeInfo.stepToTaskId.begin(); it != pipeInfo.stepToTaskId.end(); ++it )
  {
    const auto info = TaskCenter::instance().getTaskInfo( it.value() );
    Json::Value stepRes( Json::objectValue );
    stepRes["stepId"] = it.key();
    stepRes["taskId"] = static_cast<Json::Int64>( it.value() );
    stepRes["algorithmId"] = info.algorithmId.toStdString();
    if ( info.status == TaskStatus::Completed )
    {
      stepRes["status"] = "success";
      stepRes["output"] = info.resultPayload.isNull() ? Json::Value( Json::objectValue ) : info.resultPayload;
      ++completed;
    }
    else
    {
      stepRes["status"] = "error";
      stepRes["errorMessage"] = info.errorMessage.toStdString();
    }
    planResult["stepResults"].append( stepRes );
  }

  planResult["completedSteps"] = completed;
  if ( pipeInfo.isFailed )
  {
    planResult["status"] = "error";
    planResult["errorMessage"] = pipeInfo.errorMessage.isEmpty()
                                   ? "Pipeline failed"
                                   : pipeInfo.errorMessage.toStdString();
  }
  else
  {
    planResult["status"] = "success";
  }

  return planResult;
}

void AgentWorkflowExecutor::deliverPlanResult( const PlanCompletionCallback &callback, QObject *context, const Json::Value &planResult )
{
  if ( !callback )
    return;
  if ( context )
  {
    // Marshal onto the context's thread; AutoConnection runs the functor
    // directly when the caller already is on that thread and queues otherwise.
    QMetaObject::invokeMethod( context, [callback, planResult]() {
      callback( planResult );
    } );
  }
  else
  {
    callback( planResult );
  }
}

} // namespace sicnu::processing
