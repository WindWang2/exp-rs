// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"
#include "data/data_manager.h"
#include "task_center.h"
#include "workflow/workflow_types.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"

#include <QString>
#include <chrono>
#include <sstream>
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

Json::Value AgentWorkflowExecutor::executeToolCall( const Json::Value &toolCallJson, ProgressCallback progressCb )
{
  auto startTime = std::chrono::steady_clock::now();

  Json::Value resultPayload( Json::objectValue );
  resultPayload["status"] = "error";
  resultPayload["algorithmId"] = "";
  resultPayload["output"] = Json::Value( Json::objectValue );
  resultPayload["executionTimeMs"] = 0;
  resultPayload["errorMessage"] = "";

  if ( !toolCallJson.isObject() )
  {
    resultPayload["errorMessage"] = "Tool call request must be a JSON object.";
    return resultPayload;
  }

  std::string rawName;
  Json::Value argumentsPayload( Json::objectValue );

  if ( toolCallJson.isMember( "function" ) && toolCallJson["function"].isObject() )
  {
    const auto &funcObj = toolCallJson["function"];
    if ( funcObj.isMember( "name" ) && funcObj["name"].isString() )
      rawName = funcObj["name"].asString();

    if ( funcObj.isMember( "arguments" ) )
    {
      if ( funcObj["arguments"].isObject() )
      {
        argumentsPayload = funcObj["arguments"];
      }
      else if ( funcObj["arguments"].isString() )
      {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream ss( funcObj["arguments"].asString() );
        Json::parseFromStream( readerBuilder, ss, &argumentsPayload, &errs );
      }
    }
  }
  else
  {
    if ( toolCallJson.isMember( "name" ) && toolCallJson["name"].isString() )
      rawName = toolCallJson["name"].asString();

    if ( toolCallJson.isMember( "arguments" ) && toolCallJson["arguments"].isObject() )
      argumentsPayload = toolCallJson["arguments"];
    else if ( toolCallJson.isMember( "params" ) && toolCallJson["params"].isObject() )
      argumentsPayload = toolCallJson["params"];
  }

  if ( rawName.empty() )
  {
    resultPayload["errorMessage"] = "Tool call request missing algorithm function name.";
    return resultPayload;
  }

  std::string algorithmId = normalizeAlgorithmId( rawName );
  auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    resultPayload["errorMessage"] = "Algorithm not registered: " + rawName;
    return resultPayload;
  }

  resultPayload["algorithmId"] = algorithmId;
  AlgorithmDescriptor desc = adapter->descriptor();

  for ( const auto &inputPort : desc.inputs )
  {
    if ( inputPort.required && !argumentsPayload.isMember( inputPort.name ) )
    {
      resultPayload["errorMessage"] = "Missing required parameter: " + inputPort.name;
      return resultPayload;
    }
  }

  // Track in TaskCenter, execute via JobEngine custom executor that calls the atomic adapter.
  sicnu::jobs::JobRequest req;
  req.algorithmId = algorithmId;
  req.params = argumentsPayload;
  req.title = algorithmId;
  req.source = "agent";

  const long taskId = TaskCenter::instance().submitJob(
    req,
    [adapter, progressCb]( const sicnu::jobs::JobRequest &jobReq, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      return adapter->execute( jobReq.params, [&]( int percent, const std::string &message ) {
        if ( progressCb )
          progressCb( percent, message );
        if ( !message.empty() )
          ctx.logInfo( message );
        if ( percent >= 0 )
          ctx.reportProgress( percent / 100.0, message );
      } );
    } );

  resultPayload["taskId"] = static_cast<Json::Int64>( taskId );

  using clock = std::chrono::steady_clock;
  const auto info = TaskCenter::instance().waitForTask( taskId, std::chrono::minutes( 30 ) );

  auto endTime = clock::now();
  resultPayload["executionTimeMs"] = static_cast<int>(
    std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime ).count() );

  if ( info.status == TaskStatus::Completed )
  {
    resultPayload["status"] = "success";
    resultPayload["output"] = info.resultPayload.isNull() ? Json::Value( Json::objectValue ) : info.resultPayload;
    if ( !info.outputLayerPath.isEmpty() && resultPayload["output"].isObject()
         && !resultPayload["output"].isMember( "output" ) )
    {
      resultPayload["output"]["output"] = info.outputLayerPath.toStdString();
    }

    if ( mDataManager && resultPayload["output"].isObject() )
    {
      std::string outPath;
      const auto &output = resultPayload["output"];
      if ( output.isMember( "output" ) && output["output"].isString() )
        outPath = output["output"].asString();
      else if ( output.isMember( "outputPath" ) && output["outputPath"].isString() )
        outPath = output["outputPath"].asString();

      if ( !outPath.empty() )
      {
        data::RegisterRequest regReq;
        regReq.source.canonicalSource = QString::fromStdString( outPath );
        regReq.persistence = data::PersistencePolicy::TaskTemporary;
        regReq.additionalCapabilities = data::AssetCapability::DeletableSource;
        mDataManager->registerSource( regReq );
      }
    }
  }
  else if ( isTerminalStatus( info.status ) )
  {
    resultPayload["status"] = "error";
    resultPayload["errorMessage"] = info.errorMessage.toStdString();
  }
  else
  {
    // Wait timed out before the task reached a terminal status.
    resultPayload["errorMessage"] = kToolCallTimeoutMessage.toStdString();
  }
  return resultPayload;
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

  if ( !planJson.isObject() || !planJson.isMember( "steps" ) || !planJson["steps"].isArray() )
  {
    planResult["errorMessage"] = "Agent plan must contain a 'steps' array.";
    return planResult;
  }

  const auto &stepsArr = planJson["steps"];
  planResult["totalSteps"] = static_cast<int>( stepsArr.size() );

  sicnu::workflow::WorkflowDefinition def;
  def.id = planJson.isMember( "id" ) && planJson["id"].isString()
             ? planJson["id"].asString()
             : "agent_plan";
  def.title = planJson.isMember( "title" ) && planJson["title"].isString()
                ? planJson["title"].asString()
                : "Agent Plan";

  for ( Json::ArrayIndex i = 0; i < stepsArr.size(); ++i )
  {
    const auto &stepObj = stepsArr[i];
    if ( !stepObj.isObject() )
      continue;

    sicnu::workflow::StepDef step;
    step.id = stepObj.isMember( "id" ) && stepObj["id"].isString()
                ? stepObj["id"].asString()
                : ( "step_" + std::to_string( i ) );
    step.kind = sicnu::workflow::StepKind::Operator;

    std::string rawName;
    if ( stepObj.isMember( "name" ) && stepObj["name"].isString() )
      rawName = stepObj["name"].asString();
    else if ( stepObj.isMember( "operatorId" ) && stepObj["operatorId"].isString() )
      rawName = stepObj["operatorId"].asString();
    step.operatorId = normalizeAlgorithmId( rawName );
    step.title = step.operatorId;

    Json::Value argumentsPayload( Json::objectValue );
    if ( stepObj.isMember( "arguments" ) && stepObj["arguments"].isObject() )
      argumentsPayload = stepObj["arguments"];
    else if ( stepObj.isMember( "params" ) && stepObj["params"].isObject() )
      argumentsPayload = stepObj["params"];
    step.params = argumentsPayload;

    // Infer parent edges from $stepId.output placeholders in string params.
    for ( const auto &key : argumentsPayload.getMemberNames() )
    {
      if ( !argumentsPayload[key].isString() )
        continue;
      const std::string strVal = argumentsPayload[key].asString();
      if ( strVal.empty() || strVal[0] != '$' )
        continue;
      std::string ref = strVal.substr( 1 );
      auto dotPos = ref.find( '.' );
      std::string refStepId = ( dotPos != std::string::npos ) ? ref.substr( 0, dotPos ) : ref;
      std::string refPort = ( dotPos != std::string::npos ) ? ref.substr( dotPos + 1 ) : "output";
      if ( refStepId.empty() )
        continue;
      sicnu::workflow::StepConnection conn;
      conn.fromStepId = refStepId;
      conn.fromPort = refPort;
      conn.toPort = key;
      step.inputs.push_back( conn );
    }

    def.steps.push_back( step );
  }

  if ( def.steps.empty() )
  {
    planResult["errorMessage"] = "Agent plan contained no operator steps.";
    return planResult;
  }

  const long pipelineId = TaskCenter::instance().submitPipeline( def, /*autoLoad=*/false );
  planResult["pipelineId"] = static_cast<Json::Int64>( pipelineId );
  if ( pipelineId < 0 )
  {
    planResult["errorMessage"] = "TaskCenter rejected the agent plan pipeline.";
    return planResult;
  }

  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::minutes( 60 );
  for ( ;; )
  {
    const auto pipeInfo = TaskCenter::instance().getPipelineInfo( pipelineId );
    if ( pipeInfo.isCompleted )
    {
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

    if ( clock::now() > deadline )
    {
      planResult["errorMessage"] = "Agent plan pipeline timed out in TaskCenter";
      return planResult;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
  }
}

} // namespace sicnu::processing
