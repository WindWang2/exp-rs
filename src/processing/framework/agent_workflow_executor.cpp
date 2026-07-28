// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"
#include "data/data_manager.h"

#include <QString>
#include <chrono>
#include <iostream>

namespace sicnu::processing {

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

  // Parse tool call structure (supports OpenAI function call or flat format)
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

  // Resolve normalized name (e.g. "rs_spectral_index" -> "rs:spectral_index")
  std::string algorithmId = rawName;
  auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    // Try replacing first '_' with ':'
    auto underscorePos = rawName.find( '_' );
    if ( underscorePos != std::string::npos )
    {
      std::string altId = rawName;
      altId[underscorePos] = ':';
      adapter = AtomicAlgorithmRegistry::instance().findAdapter( altId );
      if ( adapter )
        algorithmId = altId;
    }
  }

  if ( !adapter )
  {
    resultPayload["errorMessage"] = "Algorithm not registered: " + rawName;
    return resultPayload;
  }

  resultPayload["algorithmId"] = algorithmId;
  AlgorithmDescriptor desc = adapter->descriptor();

  // Validate required parameters
  for ( const auto &inputPort : desc.inputs )
  {
    if ( inputPort.required )
    {
      if ( !argumentsPayload.isMember( inputPort.name ) )
      {
        resultPayload["errorMessage"] = "Missing required parameter: " + inputPort.name;
        return resultPayload;
      }
    }
  }

  // Execute algorithm
  try
  {
    Json::Value output = adapter->execute( argumentsPayload, progressCb );
    auto endTime = std::chrono::steady_clock::now();
    int elapsedMs = static_cast<int>( std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime ).count() );

    resultPayload["status"] = "success";
    resultPayload["output"] = output;
    resultPayload["executionTimeMs"] = elapsedMs;

    // Data Manager registration seam (ADR 0009/0010 compliant)
    if ( mDataManager && output.isObject() )
    {
      std::string outPath;
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

    return resultPayload;
  }
  catch ( const std::exception &ex )
  {
    auto endTime = std::chrono::steady_clock::now();
    int elapsedMs = static_cast<int>( std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime ).count() );

    resultPayload["status"] = "error";
    resultPayload["executionTimeMs"] = elapsedMs;
    resultPayload["errorMessage"] = ex.what();
    return resultPayload;
  }
}

Json::Value AgentWorkflowExecutor::executeAgentPlan( const Json::Value &planJson, ProgressCallback progressCb )
{
  Json::Value planResult( Json::objectValue );
  planResult["status"] = "error";
  planResult["completedSteps"] = 0;
  planResult["totalSteps"] = 0;
  planResult["stepResults"] = Json::Value( Json::arrayValue );
  planResult["errorMessage"] = "";

  if ( !planJson.isObject() || !planJson.isMember( "steps" ) || !planJson["steps"].isArray() )
  {
    planResult["errorMessage"] = "Agent plan must contain a 'steps' array.";
    return planResult;
  }

  const auto &stepsArr = planJson["steps"];
  planResult["totalSteps"] = static_cast<int>( stepsArr.size() );

  std::unordered_map<std::string, Json::Value> completedStepOutputs;

  for ( Json::ArrayIndex i = 0; i < stepsArr.size(); ++i )
  {
    const auto &stepObj = stepsArr[i];
    std::string stepId = stepObj.isMember( "id" ) && stepObj["id"].isString() ? stepObj["id"].asString() : ( "step_" + std::to_string( i ) );

    // Clone stepObj to resolve references
    Json::Value stepPayload = stepObj;
    if ( stepPayload.isMember( "arguments" ) && stepPayload["arguments"].isObject() )
    {
      auto &args = stepPayload["arguments"];
      for ( const auto &key : args.getMemberNames() )
      {
        if ( args[key].isString() )
        {
          std::string strVal = args[key].asString();
          if ( !strVal.empty() && strVal[0] == '$' )
          {
            // Parse reference e.g. "$step1.output"
            std::string ref = strVal.substr( 1 );
            auto dotPos = ref.find( '.' );
            std::string refStepId = ( dotPos != std::string::npos ) ? ref.substr( 0, dotPos ) : ref;
            std::string refKey = ( dotPos != std::string::npos ) ? ref.substr( dotPos + 1 ) : "output";

            if ( completedStepOutputs.count( refStepId ) > 0 )
            {
              const auto &refOut = completedStepOutputs[refStepId];
              if ( refOut.isObject() && refOut.isMember( refKey ) && refOut[refKey].isString() )
              {
                args[key] = refOut[refKey].asString();
              }
            }
          }
        }
      }
    }

    Json::Value stepRes = executeToolCall( stepPayload, progressCb );
    stepRes["stepId"] = stepId;
    planResult["stepResults"].append( stepRes );

    if ( stepRes["status"].asString() != "success" )
    {
      planResult["errorMessage"] = "Step '" + stepId + "' failed: " + stepRes["errorMessage"].asString();
      return planResult;
    }

    completedStepOutputs[stepId] = stepRes["output"];
    planResult["completedSteps"] = static_cast<int>( i + 1 );
  }

  planResult["status"] = "success";
  return planResult;
}

} // namespace sicnu::processing
