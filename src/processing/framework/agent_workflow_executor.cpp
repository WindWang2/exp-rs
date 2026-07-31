// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"
#include "data/data_manager.h"
#include "task_center.h"
#include "workflow/workflow_types.h"
#include "workflow/workflow_definition.h"
#include "workflow/placeholder_grammar.h"
#include "jobs/job_types.h"

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



} // namespace

AgentWorkflowExecutor::AgentWorkflowExecutor( data::DataManager *dataManager )
  : mDataManager( dataManager )
{
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

  Json::Value planResult( Json::objectValue );
  planResult["status"] = "error";
  planResult["completedSteps"] = 0;
  planResult["totalSteps"] = 0;
  planResult["stepResults"] = Json::Value( Json::arrayValue );
  planResult["errorMessage"] = "";
  planResult["pipelineId"] = -1;

  sicnu::workflow::WorkflowDefinition def;
  std::string parseErr;
  if ( !sicnu::workflow::workflowDefinitionFromJson( planJson, def, parseErr ) || def.steps.empty() )
  {
    planResult["errorMessage"] = parseErr.empty() ? "Agent plan contained no operator steps." : parseErr;
    return planResult;
  }

  // Normalize algorithm IDs on parsed steps if needed
  for ( auto &step : def.steps )
  {
    step.operatorId = normalizeAlgorithmId( step.operatorId );
    if ( step.title.empty() )
      step.title = step.operatorId;
  }

  planResult["totalSteps"] = static_cast<int>( def.steps.size() );

  const long pipelineId = TaskCenter::instance().submitPipeline( def, /*autoLoad=*/false );
  planResult["pipelineId"] = static_cast<Json::Int64>( pipelineId );
  if ( pipelineId < 0 )
  {
    planResult["errorMessage"] = "TaskCenter rejected the agent plan pipeline.";
    return planResult;
  }

  const auto pipeInfo = TaskCenter::instance().waitForPipeline( pipelineId, std::chrono::minutes( 60 ) );
  if ( pipeInfo.pipelineId < 0 )
  {
    planResult["errorMessage"] = "TaskCenter pipeline execution failed";
    return planResult;
  }

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

} // namespace sicnu::processing
