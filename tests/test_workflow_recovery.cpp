#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include <QTemporaryDir>
#include <QFile>

using namespace sicnu::workflow;

TEST_CASE( "WorkflowCheckpointManager atomic save and load", "[workflow][v2][checkpoint]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_checkpoint_test";
  def.title = "Checkpoint Test";

  StepDef step;
  step.id = "s1";
  step.operatorId = "rs:test";
  def.steps.push_back( step );

  auto run = WorkflowRun::createFromDefinition( def, "run-check-1" );
  run->transitionTo( WorkflowRunState::Planning );
  run->transitionTo( WorkflowRunState::Ready );
  run->transitionTo( WorkflowRunState::Running );
  run->setArtifact( "out", "/path/out.tif" );

  QString savedPath = manager.saveCheckpoint( *run, tmpDir.path() );
  REQUIRE( !savedPath.isEmpty() );
  REQUIRE( QFile::exists( savedPath ) );

  QString err;
  auto loaded = manager.loadCheckpoint( savedPath, &err );
  REQUIRE( loaded != nullptr );
  REQUIRE( err.isEmpty() );
  REQUIRE( loaded->getRunId() == "run-check-1" );
  REQUIRE( loaded->getWorkflowId() == "wf_checkpoint_test" );
  REQUIRE( loaded->getState() == WorkflowRunState::Running );
  REQUIRE( loaded->getArtifact( "out" ) == "/path/out.tif" );
}

TEST_CASE( "WorkflowCheckpointManager recovery of interrupted runs", "[workflow][v2][recovery]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_recovery";
  def.title = "Recovery Test";

  // Run 1: Running (interrupted candidate)
  auto run1 = WorkflowRun::createFromDefinition( def, "run-1" );
  run1->transitionTo( WorkflowRunState::Planning );
  run1->transitionTo( WorkflowRunState::Ready );
  run1->transitionTo( WorkflowRunState::Running );
  manager.saveCheckpoint( *run1, tmpDir.path() );

  // Run 2: Completed (should not be touched)
  auto run2 = WorkflowRun::createFromDefinition( def, "run-2" );
  run2->transitionTo( WorkflowRunState::Planning );
  run2->transitionTo( WorkflowRunState::Ready );
  run2->transitionTo( WorkflowRunState::Running );
  run2->transitionTo( WorkflowRunState::Completed );
  manager.saveCheckpoint( *run2, tmpDir.path() );

  // Run 3: WaitingResource (interrupted candidate)
  auto run3 = WorkflowRun::createFromDefinition( def, "run-3" );
  run3->transitionTo( WorkflowRunState::Planning );
  run3->transitionTo( WorkflowRunState::Ready );
  run3->transitionTo( WorkflowRunState::WaitingResource );
  manager.saveCheckpoint( *run3, tmpDir.path() );

  // Execute recovery
  auto recovered = manager.recoverInterruptedRuns( tmpDir.path() );
  REQUIRE( recovered.size() == 2 );

  // Verify on-disk updates
  QString err;
  auto loaded1 = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-1.json" ), &err );
  REQUIRE( loaded1 != nullptr );
  REQUIRE( loaded1->getState() == WorkflowRunState::Interrupted );

  auto loaded2 = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-2.json" ), &err );
  REQUIRE( loaded2 != nullptr );
  REQUIRE( loaded2->getState() == WorkflowRunState::Completed );

  auto loaded3 = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-3.json" ), &err );
  REQUIRE( loaded3 != nullptr );
  REQUIRE( loaded3->getState() == WorkflowRunState::Interrupted );
}
