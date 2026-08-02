// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"
#include "data/data_manager.h"
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

  const long pipelineId = TaskCenter::instance().submitPipeline( def, /*autoLoad=*/false );
  if ( pipelineId < 0 )
    return makePlanErrorResult( static_cast<int>( def.steps.size() ), -1, "TaskCenter rejected the agent plan pipeline." );

  const auto pipeInfo = TaskCenter::instance().waitForPipeline( pipelineId, std::chrono::minutes( 60 ) );
  if ( pipeInfo.pipelineId < 0 )
    return makePlanErrorResult( static_cast<int>( def.steps.size() ), pipelineId, "TaskCenter pipeline execution failed" );

  return assemblePlanResult( static_cast<int>( def.steps.size() ), pipelineId, pipeInfo );
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

  const long pipelineId = TaskCenter::instance().submitPipeline( def, /*autoLoad=*/false );
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
    planResult = assemblePlanResult( pending.totalSteps, pipelineId, info );

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
