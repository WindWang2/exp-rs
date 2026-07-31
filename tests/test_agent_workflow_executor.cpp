// tests/test_agent_workflow_executor.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include "data/data_manager.h"

using namespace sicnu::processing;
using namespace sicnu::data;

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

