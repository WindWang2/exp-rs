// src/processing/framework/agent_workflow_executor.cpp
#include "agent_workflow_executor.h"

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

} // namespace sicnu::processing
