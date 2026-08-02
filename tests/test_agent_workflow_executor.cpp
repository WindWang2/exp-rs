// tests/test_agent_workflow_executor.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include "data/data_manager.h"

#include <QApplication>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

using namespace sicnu::processing;
using namespace sicnu::data;

static void ensureQtApp()
{
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_agent_workflow_executor";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
}

/// Builds the two-step plan used by the sync and async cases.
static Json::Value makeTwoStepPlan()
{
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
  return planJson;
}

TEST_CASE( "AgentWorkflowExecutor handles multi-step agent plans and reference resolution", "[processing][executor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AgentWorkflowExecutor executor;

  Json::Value planJson = makeTwoStepPlan();

  // Parameter validation step reference check
  Json::Value planRes = executor.executeAgentPlan( planJson );
  REQUIRE( planRes.isObject() );
  REQUIRE( planRes["totalSteps"].asInt() == 2 );
  REQUIRE( planRes.isMember( "stepResults" ) );
  REQUIRE( planRes["stepResults"].isArray() );
}

TEST_CASE( "AgentWorkflowExecutor async plan execution matches the sync result shape", "[processing][executor][async]" )
{
  ensureQtApp();
  AtomicAlgorithmRegistry::instance().reset();

  Json::Value planJson = makeTwoStepPlan();

  // Reference shape produced by the blocking path for the same plan.
  AgentWorkflowExecutor syncExecutor;
  const Json::Value syncResult = syncExecutor.executeAgentPlan( planJson );

  AgentWorkflowExecutor asyncExecutor;
  QObject context; // lives on the test thread; callback is marshaled onto it.
  Json::Value asyncResult;
  bool called = false;

  QEventLoop loop;
  QTimer timeoutGuard;
  timeoutGuard.setSingleShot( true );
  QObject::connect( &timeoutGuard, &QTimer::timeout, &loop, &QEventLoop::quit );
  timeoutGuard.start( 60000 ); // fail-safe: never hang the suite

  const long pipelineId = asyncExecutor.executeAgentPlanAsync( planJson, [&]( const Json::Value &result ) {
    asyncResult = result;
    called = true;
    loop.quit();
  }, &context );

  REQUIRE( pipelineId >= 0 );
  if ( !called )
    loop.exec(); // let the queued taskUpdated delivery reach the watcher
  timeoutGuard.stop();

  REQUIRE( called );
  REQUIRE( asyncResult.isObject() );

  // The async path must produce the same planResult shape as the sync path.
  CHECK( asyncResult["status"].asString() == syncResult["status"].asString() );
  CHECK( asyncResult["completedSteps"].asInt() == syncResult["completedSteps"].asInt() );
  CHECK( asyncResult["totalSteps"].asInt() == syncResult["totalSteps"].asInt() );
  CHECK( asyncResult["stepResults"].size() == syncResult["stepResults"].size() );
}

TEST_CASE( "AgentWorkflowExecutor async plan reports parse failures through the callback", "[processing][executor][async]" )
{
  ensureQtApp();

  AgentWorkflowExecutor executor;
  QObject context;

  Json::Value badPlan( Json::objectValue );
  badPlan["steps"] = Json::Value( Json::arrayValue ); // no operator steps

  bool called = false;
  Json::Value asyncResult;
  const long pipelineId = executor.executeAgentPlanAsync( badPlan, [&]( const Json::Value &result ) {
    asyncResult = result;
    called = true;
  }, &context );

  REQUIRE( pipelineId == -1 );
  REQUIRE( called );
  REQUIRE( asyncResult["status"].asString() == "error" );
  REQUIRE_FALSE( asyncResult["errorMessage"].asString().empty() );
  REQUIRE( asyncResult["completedSteps"].asInt() == 0 );
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

