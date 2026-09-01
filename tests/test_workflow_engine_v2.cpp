#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_run.h"
#include "workflow/workflow_definition.h"

#include <set>
#include <string>

using namespace sicnu::workflow;

namespace {

WorkflowDefinition makeSingleStepDefinition()
{
  WorkflowDefinition def;
  def.id = "wf_test";
  def.title = "Test Workflow";
  StepDef step;
  step.id = "step_1";
  step.operatorId = "rs:bandmath";
  step.params["exp"] = "B1+B2";
  def.steps.push_back( step );
  return def;
}

} // namespace

TEST_CASE( "WorkflowRunState valid and invalid transitions", "[workflow][v2][state]" )
{
  WorkflowRun run;
  REQUIRE( run.state() == WorkflowRunState::Created );

  REQUIRE( run.transitionTo( WorkflowRunState::Planning ) );
  REQUIRE( run.state() == WorkflowRunState::Planning );

  REQUIRE( run.transitionTo( WorkflowRunState::Ready ) );
  REQUIRE( run.state() == WorkflowRunState::Ready );

  REQUIRE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE( run.state() == WorkflowRunState::Running );

  REQUIRE( run.transitionTo( WorkflowRunState::WaitingResource ) );
  REQUIRE( run.state() == WorkflowRunState::WaitingResource );

  REQUIRE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE( run.state() == WorkflowRunState::Running );

  REQUIRE( run.transitionTo( WorkflowRunState::Cancelling ) );
  REQUIRE( run.state() == WorkflowRunState::Cancelling );

  REQUIRE( run.transitionTo( WorkflowRunState::Canceled ) );
  REQUIRE( run.state() == WorkflowRunState::Canceled );

  REQUIRE_FALSE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE_FALSE( run.transitionTo( WorkflowRunState::Planning ) );
}

TEST_CASE( "WorkflowRun createFromDefinition and step plan initialization", "[workflow][v2][create]" )
{
  WorkflowDefinition def;
  def.id = "ndvi_pipeline";
  def.title = "NDVI Pipeline";

  StepDef step1;
  step1.id = "step_clip";
  step1.operatorId = "gdal:clip";
  step1.resourceEstimateMb = 512;
  step1.params["input"] = "input.tif";
  step1.params["output"] = "clipped.tif";
  def.steps.push_back( step1 );

  StepDef step2;
  step2.id = "step_ndvi";
  step2.operatorId = "rs:ndvi";
  step2.resourceEstimateMb = 256;
  step2.params["nir"] = 4;
  step2.params["red"] = 3;
  step2.inputs.push_back( StepConnection{ "step_clip", "output", "input" } );
  def.steps.push_back( step2 );

  auto run = WorkflowRun::createFromDefinition( def, "test-run-001" );
  REQUIRE( run != nullptr );
  REQUIRE( run->runId() == "test-run-001" );
  REQUIRE( run->workflowId() == "ndvi_pipeline" );
  REQUIRE( run->state() == WorkflowRunState::Created );
  REQUIRE( run->stepPlans().size() == 2 );

  StepPlan *p1 = run->findStepPlan( "step_clip" );
  REQUIRE( p1 != nullptr );
  REQUIRE( p1->operatorId == "gdal:clip" );
  REQUIRE( p1->resourceEstimateMb == 512 );
  REQUIRE( p1->dependencies.empty() );
  REQUIRE( p1->status == "Pending" );

  StepPlan *p2 = run->findStepPlan( "step_ndvi" );
  REQUIRE( p2 != nullptr );
  REQUIRE( p2->operatorId == "rs:ndvi" );
  REQUIRE( p2->dependencies.size() == 1 );
  REQUIRE( p2->dependencies[0] == "step_clip" );
}

TEST_CASE( "WorkflowRun JSON serialization roundtrip", "[workflow][v2][json]" )
{
  WorkflowDefinition def;
  def.id = "sample_wf";
  def.title = "Sample Workflow";

  StepDef step;
  step.id = "step_1";
  step.operatorId = "rs:bandmath";
  step.params["exp"] = "B1+B2";
  def.steps.push_back( step );

  auto run = WorkflowRun::createFromDefinition( def, "run-42" );
  run->transitionTo( WorkflowRunState::Planning );
  run->transitionTo( WorkflowRunState::Ready );
  run->transitionTo( WorkflowRunState::Running );
  run->setArtifact( "final_map", "/data/final.tif" );

  StepPlan *p = run->findStepPlan( "step_1" );
  REQUIRE( p != nullptr );
  p->status = "Completed";
  p->outputLayerPath = "/data/step1.tif";
  p->fingerprint = "abc123sha256";
  p->cacheHit = true;
  run->recalculateProgress();
  REQUIRE( run->progress() == 1.0 );

  Json::Value json = run->toJson();
  std::string err;
  auto restored = WorkflowRun::fromJson( json, err );
  REQUIRE( restored != nullptr );
  REQUIRE( err.empty() );
  REQUIRE( restored->runId() == "run-42" );
  REQUIRE( restored->workflowId() == "sample_wf" );
  REQUIRE( restored->state() == WorkflowRunState::Running );
  REQUIRE( restored->artifact( "final_map" ) == "/data/final.tif" );
  REQUIRE( restored->progress() == 1.0 );

  const StepPlan *restoredPlan = restored->findStepPlan( "step_1" );
  REQUIRE( restoredPlan != nullptr );
  REQUIRE( restoredPlan->status == "Completed" );
  REQUIRE( restoredPlan->outputLayerPath == "/data/step1.tif" );
  REQUIRE( restoredPlan->fingerprint == "abc123sha256" );
  REQUIRE( restoredPlan->cacheHit == true );
}

TEST_CASE( "Generated run ids are unique within the same millisecond", "[workflow][v2][runid]" )
{
  const WorkflowDefinition def = makeSingleStepDefinition();

  std::set<std::string> ids;
  for ( int i = 0; i < 200; ++i )
  {
    auto run = WorkflowRun::createFromDefinition( def );
    REQUIRE( run != nullptr );
    const std::string id = run->runId();
    REQUIRE( isValidRunId( id ) );
    ids.insert( id );
  }
  REQUIRE( ids.size() == 200 );
}

TEST_CASE( "createFromDefinition rejects unsafe caller-provided run ids", "[workflow][v2][runid]" )
{
  const WorkflowDefinition def = makeSingleStepDefinition();

  REQUIRE( WorkflowRun::createFromDefinition( def, "../escape" ) == nullptr );
  REQUIRE( WorkflowRun::createFromDefinition( def, "run/with/slash" ) == nullptr );
  REQUIRE( WorkflowRun::createFromDefinition( def, ".hidden" ) == nullptr );
  REQUIRE( WorkflowRun::createFromDefinition( def, "" ) != nullptr ); // empty -> generated id
  REQUIRE( WorkflowRun::createFromDefinition( def, "run-ok_1.2" ) != nullptr );
}

TEST_CASE( "fromJson rejects invalid payloads instead of defaulting", "[workflow][v2][json][validation]" )
{
  const WorkflowDefinition def = makeSingleStepDefinition();
  auto run = WorkflowRun::createFromDefinition( def, "run-v1" );
  REQUIRE( run != nullptr );

  SECTION( "valid payload round-trips" )
  {
    std::string err;
    auto restored = WorkflowRun::fromJson( run->toJson(), err );
    REQUIRE( restored != nullptr );
    REQUIRE( err.empty() );
  }

  SECTION( "missing version is rejected" )
  {
    Json::Value json = run->toJson();
    json.removeMember( "version" );
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "future version is rejected" )
  {
    Json::Value json = run->toJson();
    json["version"] = kWorkflowRunSerializationVersion + 1;
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "unknown state string is rejected, not defaulted to Created" )
  {
    Json::Value json = run->toJson();
    json["state"] = "HalfFinished";
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "unsafe runId is rejected" )
  {
    Json::Value json = run->toJson();
    json["runId"] = "../../etc/passwd";
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "missing definition is rejected" )
  {
    Json::Value json = run->toJson();
    json.removeMember( "definition" );
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "missing stepPlans is rejected" )
  {
    Json::Value json = run->toJson();
    json.removeMember( "stepPlans" );
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }

  SECTION( "out-of-range step kind is rejected" )
  {
    Json::Value json = run->toJson();
    json["stepPlans"][0]["kind"] = 99;
    std::string err;
    REQUIRE( WorkflowRun::fromJson( json, err ) == nullptr );
    REQUIRE( !err.empty() );
  }
}
// ---------------------------------------------------------------------------
// #697 residue: WorkflowRun::fromJson rejects unknown step statuses and
// duplicate stepPlans ids (a torn/edited checkpoint must not silently dodge
// recovery reconciliation).
// ---------------------------------------------------------------------------
TEST_CASE( "WorkflowRun::fromJson rejects unknown step status and duplicate ids (#697)",
           "[workflow][v2][checkpoint]" )
{
  auto baseRun = WorkflowRun::createFromDefinition( makeSingleStepDefinition(), "validate-run" );
  REQUIRE( baseRun != nullptr );
  std::string err;
  const Json::Value good = baseRun->toJson();
  REQUIRE( WorkflowRun::fromJson( good, err ) != nullptr );

  // Unknown status vocabulary.
  Json::Value badStatus = good;
  badStatus["stepPlans"][0]["status"] = "Runing"; // typo
  err.clear();
  REQUIRE( WorkflowRun::fromJson( badStatus, err ) == nullptr );
  REQUIRE( err.find( "unknown step status" ) != std::string::npos );

  // Duplicate plan ids skew progress/lookup.
  Json::Value dupIds = good;
  dupIds["stepPlans"].append( good["stepPlans"][0] ); // same stepId twice
  err.clear();
  REQUIRE( WorkflowRun::fromJson( dupIds, err ) == nullptr );
  REQUIRE( err.find( "duplicate stepPlans id" ) != std::string::npos );
}
