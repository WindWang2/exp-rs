// tests/test_agent_workflow_executor.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/atomic_algorithm_registry.h"

using namespace sicnu::processing;

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
