#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_run.h"
#include "workflow/workflow_definition.h"

using namespace sicnu::workflow;

TEST_CASE( "WorkflowRunState valid and invalid transitions", "[workflow][v2][state]" )
{
  WorkflowRun run;
  REQUIRE( run.getState() == WorkflowRunState::Created );

  REQUIRE( run.transitionTo( WorkflowRunState::Planning ) );
  REQUIRE( run.getState() == WorkflowRunState::Planning );

  REQUIRE( run.transitionTo( WorkflowRunState::Ready ) );
  REQUIRE( run.getState() == WorkflowRunState::Ready );

  REQUIRE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE( run.getState() == WorkflowRunState::Running );

  REQUIRE( run.transitionTo( WorkflowRunState::WaitingResource ) );
  REQUIRE( run.getState() == WorkflowRunState::WaitingResource );

  REQUIRE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE( run.getState() == WorkflowRunState::Running );

  REQUIRE( run.transitionTo( WorkflowRunState::Cancelling ) );
  REQUIRE( run.getState() == WorkflowRunState::Cancelling );

  REQUIRE( run.transitionTo( WorkflowRunState::Canceled ) );
  REQUIRE( run.getState() == WorkflowRunState::Canceled );

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
  REQUIRE( run->getRunId() == "test-run-001" );
  REQUIRE( run->getWorkflowId() == "ndvi_pipeline" );
  REQUIRE( run->getState() == WorkflowRunState::Created );
  REQUIRE( run->getStepPlans().size() == 2 );

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
  REQUIRE( run->getProgress() == 1.0 );

  Json::Value json = run->toJson();
  std::string err;
  auto restored = WorkflowRun::fromJson( json, err );
  REQUIRE( restored != nullptr );
  REQUIRE( err.empty() );
  REQUIRE( restored->getRunId() == "run-42" );
  REQUIRE( restored->getWorkflowId() == "sample_wf" );
  REQUIRE( restored->getState() == WorkflowRunState::Running );
  REQUIRE( restored->getArtifact( "final_map" ) == "/data/final.tif" );
  REQUIRE( restored->getProgress() == 1.0 );

  const StepPlan *restoredPlan = restored->findStepPlan( "step_1" );
  REQUIRE( restoredPlan != nullptr );
  REQUIRE( restoredPlan->status == "Completed" );
  REQUIRE( restoredPlan->outputLayerPath == "/data/step1.tif" );
  REQUIRE( restoredPlan->fingerprint == "abc123sha256" );
  REQUIRE( restoredPlan->cacheHit == true );
}