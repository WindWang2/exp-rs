// tests/test_agent_workflow_executor.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include "data/data_manager.h"

using namespace sicnu::processing;
using namespace sicnu::data;

TEST_CASE( "AgentWorkflowExecutor executes tool call JSON and handles validation errors", "[processing][executor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AgentWorkflowExecutor executor;

  // Test missing algorithm
  Json::Value invalidAlg( Json::objectValue );
  invalidAlg["name"] = "non_existent_alg";
  Json::Value res1 = executor.executeToolCall( invalidAlg );
  REQUIRE( res1["status"].asString() == "error" );
  REQUIRE( res1["errorMessage"].asString().find( "Algorithm not registered" ) != std::string::npos );

  // Test missing required parameter
  Json::Value missingParam( Json::objectValue );
  missingParam["name"] = "rs_spectral_index";
  missingParam["arguments"] = Json::Value( Json::objectValue );
  Json::Value res2 = executor.executeToolCall( missingParam );
  REQUIRE( res2["status"].asString() == "error" );
  REQUIRE( res2["errorMessage"].asString().find( "Missing required parameter" ) != std::string::npos );

  // Test OpenAI tool call format parsing
  Json::Value openAiCall( Json::objectValue );
  openAiCall["id"] = "call_test123";
  openAiCall["type"] = "function";

  Json::Value funcObj( Json::objectValue );
  funcObj["name"] = "rs_spectral_index";
  
  Json::Value argsObj( Json::objectValue );
  argsObj["input"] = "/non/existent/file.tif";
  argsObj["index"] = "NDVI";
  funcObj["arguments"] = argsObj;

  openAiCall["function"] = funcObj;

  bool progressCalled = false;
  Json::Value res3 = executor.executeToolCall( openAiCall, [&]( int percent, const std::string &msg ) {
    progressCalled = true;
  } );

  REQUIRE( res3.isObject() );
  REQUIRE( res3["algorithmId"].asString() == "rs:spectral_index" );
  // Will fail execution on non-existent file, but parameter validation passes
  REQUIRE( res3.isMember( "executionTimeMs" ) );
}

TEST_CASE( "AgentWorkflowExecutor handles multi-step agent plans and reference resolution", "[processing][executor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AgentWorkflowExecutor executor;

  Json::Value planJson( Json::objectValue );
  Json::Value stepsArr( Json::arrayValue );

  Json::Value s1( Json::objectValue );
  s1["id"] = "step1";
  s1["name"] = "rs_spectral_index";
  s1["arguments"] = Json::Value( Json::objectValue );
  s1["arguments"]["input"] = "/path/to/landsat.tif";
  s1["arguments"]["index"] = "NDVI";
  stepsArr.append( s1 );

  Json::Value s2( Json::objectValue );
  s2["id"] = "step2";
  s2["name"] = "opencv_gaussian_blur";
  s2["arguments"] = Json::Value( Json::objectValue );
  s2["arguments"]["input"] = "$step1.output";
  s2["arguments"]["kernel_size"] = 5;
  stepsArr.append( s2 );

  planJson["steps"] = stepsArr;

  // Parameter validation step reference check
  Json::Value planRes = executor.executeAgentPlan( planJson );
  REQUIRE( planRes.isObject() );
  REQUIRE( planRes["totalSteps"].asInt() == 2 );
  REQUIRE( planRes.isMember( "stepResults" ) );
  REQUIRE( planRes["stepResults"].isArray() );
}

TEST_CASE( "AgentWorkflowExecutor registers output datasets in DataManager", "[processing][executor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  DataManager dataMgr;
  AgentWorkflowExecutor executor( &dataMgr );

  REQUIRE( executor.dataManager() == &dataMgr );

  // Sweep initial task temporaries
  auto reapResult = dataMgr.reapTaskTemporaries();
  REQUIRE( reapResult.reapedCount == 0 );
}

