#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <string>

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
  REQUIRE( loaded->runId() == "run-check-1" );
  REQUIRE( loaded->workflowId() == "wf_checkpoint_test" );
  REQUIRE( loaded->state() == WorkflowRunState::Running );
  REQUIRE( loaded->artifact( "out" ) == "/path/out.tif" );
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
  REQUIRE( loaded1->state() == WorkflowRunState::Interrupted );

  auto loaded2 = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-2.json" ), &err );
  REQUIRE( loaded2 != nullptr );
  REQUIRE( loaded2->state() == WorkflowRunState::Completed );

  auto loaded3 = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-3.json" ), &err );
  REQUIRE( loaded3 != nullptr );
  REQUIRE( loaded3->state() == WorkflowRunState::Interrupted );
}

TEST_CASE( "WorkflowCheckpointManager atomic replace leaves no tmp orphans", "[workflow][v2][checkpoint]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_replace";
  def.title = "Replace Test";
  StepDef step;
  step.id = "s1";
  step.operatorId = "rs:test";
  def.steps.push_back( step );

  auto run = WorkflowRun::createFromDefinition( def, "run-replace-1" );
  REQUIRE( run != nullptr );

  // Save repeatedly over the same final path: every save must leave exactly
  // one loadable checkpoint and no leftover tmp files behind.
  for ( int i = 0; i < 5; ++i )
  {
    run->setErrorMessage( "iteration " + std::to_string( i ) );
    const QString saved = manager.saveCheckpoint( *run, tmpDir.path() );
    REQUIRE( !saved.isEmpty() );

    QString err;
    auto loaded = manager.loadCheckpoint( saved, &err );
    REQUIRE( loaded != nullptr );
    REQUIRE( err.isEmpty() );
    REQUIRE( loaded->errorMessage() == "iteration " + std::to_string( i ) );
  }

  const QStringList leftovers = QDir( tmpDir.path() )
                                  .entryList( QStringList{ "checkpoint_*.tmp*" }, QDir::Files );
  REQUIRE( leftovers.isEmpty() );
}

TEST_CASE( "Recovery resets step plans stuck in Running to Pending", "[workflow][v2][recovery]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_reconcile";
  def.title = "Reconcile Test";
  StepDef stepA;
  stepA.id = "sa";
  stepA.operatorId = "rs:a";
  StepDef stepB;
  stepB.id = "sb";
  stepB.operatorId = "rs:b";
  stepB.inputs.push_back( StepConnection{ "sa", "output", "input" } );
  def.steps.push_back( stepA );
  def.steps.push_back( stepB );

  auto run = WorkflowRun::createFromDefinition( def, "run-reconcile-1" );
  REQUIRE( run != nullptr );
  run->transitionTo( WorkflowRunState::Planning );
  run->transitionTo( WorkflowRunState::Ready );
  run->transitionTo( WorkflowRunState::Running );

  // Simulate a crash mid-flight: one step completed, one still marked Running.
  run->findStepPlan( "sa" )->status = "Completed";
  run->findStepPlan( "sb" )->status = "Running";
  run->recalculateProgress();
  REQUIRE( manager.saveCheckpoint( *run, tmpDir.path() ).isEmpty() == false );

  auto recovered = manager.recoverInterruptedRuns( tmpDir.path() );
  REQUIRE( recovered.size() == 1 );

  QString err;
  auto loaded = manager.loadCheckpoint( tmpDir.filePath( "checkpoint_run-reconcile-1.json" ), &err );
  REQUIRE( loaded != nullptr );
  REQUIRE( loaded->state() == WorkflowRunState::Interrupted );
  REQUIRE( loaded->findStepPlan( "sa" )->status == "Completed" );
  REQUIRE( loaded->findStepPlan( "sb" )->status == "Pending" );
}

TEST_CASE( "Corrupt and hollow checkpoints are skipped without blocking recovery", "[workflow][v2][recovery]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_skip";
  def.title = "Skip Test";
  StepDef step;
  step.id = "s1";
  step.operatorId = "rs:test";
  def.steps.push_back( step );

  auto run = WorkflowRun::createFromDefinition( def, "run-good-1" );
  run->transitionTo( WorkflowRunState::Planning );
  run->transitionTo( WorkflowRunState::Running );
  REQUIRE( manager.saveCheckpoint( *run, tmpDir.path() ).isEmpty() == false );

  // Garbage bytes
  QFile garbage( tmpDir.filePath( "checkpoint_run-garbage.json" ) );
  REQUIRE( garbage.open( QIODevice::WriteOnly ) );
  garbage.write( "not json at all {" );
  garbage.close();

  // Structurally valid JSON but hollow: no version, no definition, no plans
  QFile hollow( tmpDir.filePath( "checkpoint_run-hollow.json" ) );
  REQUIRE( hollow.open( QIODevice::WriteOnly ) );
  hollow.write( R"({"runId":"run-hollow","state":"Running"})" );
  hollow.close();

  auto recovered = manager.recoverInterruptedRuns( tmpDir.path() );
  REQUIRE( recovered.size() == 1 );
  REQUIRE( recovered.front()->runId() == "run-good-1" );
}

TEST_CASE( "saveCheckpoint refuses unsafe run ids", "[workflow][v2][checkpoint]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  WorkflowCheckpointManager manager;

  WorkflowDefinition def;
  def.id = "wf_unsafe";
  def.title = "Unsafe Test";
  StepDef step;
  step.id = "s1";
  step.operatorId = "rs:test";
  def.steps.push_back( step );

  auto run = WorkflowRun::createFromDefinition( def, "run-ok" );
  REQUIRE( run != nullptr );

  // The validating setter rejects unsafe ids outright, and the id stays
  // unchanged, so every later checkpoint path stays inside the directory.
  REQUIRE_FALSE( run->setRunId( "../../escape" ) );
  REQUIRE( run->runId() == "run-ok" );
  REQUIRE_FALSE( run->setRunId( "" ) );

  REQUIRE( !manager.saveCheckpoint( *run, tmpDir.path() ).isEmpty() );
  const QStringList written = QDir( tmpDir.path() ).entryList( QStringList{ "*.json" }, QDir::Files );
  REQUIRE( written.size() == 1 );
  REQUIRE( written.front() == QStringLiteral( "checkpoint_run-ok.json" ) );
}
