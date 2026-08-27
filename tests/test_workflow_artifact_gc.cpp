#include <catch2/catch_test_macros.hpp>

#include "workflow/artifact_gc.h"
#include "workflow/workflow_run.h"
#include <QTemporaryDir>
#include <QFile>

using namespace sicnu::workflow;

namespace {

struct TwoStepRun {
  std::unique_ptr<WorkflowRun> run;
  StepPlan *intermediate = nullptr;
  StepPlan *finalStep = nullptr;
};

TwoStepRun makeTwoStepRun( const QString &intermediateOut, const QString &finalOut )
{
  WorkflowDefinition def;
  def.id = "two_step_pipeline";

  StepDef s1;
  s1.id = "s1";
  s1.operatorId = "rs:step1";
  def.steps.push_back( s1 );

  StepDef s2;
  s2.id = "s2";
  s2.operatorId = "rs:step2";
  s2.inputs.push_back( StepConnection{ "s1", "output", "input" } );
  def.steps.push_back( s2 );

  TwoStepRun result;
  result.run = WorkflowRun::createFromDefinition( def, "run-gc-test" );
  result.intermediate = result.run->findStepPlan( "s1" );
  result.intermediate->outputLayerPath = intermediateOut.toStdString();
  result.intermediate->status = "Completed";
  result.finalStep = result.run->findStepPlan( "s2" );
  result.finalStep->outputLayerPath = finalOut.toStdString();
  result.finalStep->status = "Completed";
  return result;
}

void transitionToCompleted( WorkflowRun &run )
{
  REQUIRE( run.transitionTo( WorkflowRunState::Planning ) );
  REQUIRE( run.transitionTo( WorkflowRunState::Ready ) );
  REQUIRE( run.transitionTo( WorkflowRunState::Running ) );
  REQUIRE( run.transitionTo( WorkflowRunState::Completed ) );
}

} // namespace

TEST_CASE( "ArtifactGC inspectReapable and sweepRun with sidecars", "[workflow][v2][gc]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  const QString step1Out = tmpDir.filePath( "step1_intermediate.tif" );
  const QString step1Tfw = tmpDir.filePath( "step1_intermediate.tfw" );
  const QString step1Aux = tmpDir.filePath( "step1_intermediate.aux.xml" );
  const QString finalOut = tmpDir.filePath( "step2_final.tif" );

  // Create mock files
  QFile f1( step1Out ); f1.open( QIODevice::WriteOnly ); f1.write( "raster1" ); f1.close();
  QFile f2( step1Tfw ); f2.open( QIODevice::WriteOnly ); f2.write( "tfw" ); f2.close();
  QFile f3( step1Aux ); f3.open( QIODevice::WriteOnly ); f3.write( "aux" ); f3.close();
  QFile f4( finalOut ); f4.open( QIODevice::WriteOnly ); f4.write( "final" ); f4.close();

  REQUIRE( QFile::exists( step1Out ) );
  REQUIRE( QFile::exists( step1Tfw ) );
  REQUIRE( QFile::exists( step1Aux ) );
  REQUIRE( QFile::exists( finalOut ) );

  TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
  transitionToCompleted( *setup.run );

  ArtifactGC gc;

  // Inspect reapable with retainFinalOutputs = true
  QStringList reapable = gc.inspectReapable( *setup.run, true );
  REQUIRE( reapable.size() == 1 );
  REQUIRE( reapable.contains( step1Out ) );
  REQUIRE_FALSE( reapable.contains( finalOut ) );

  // Sweep run
  GCSweepReport report = gc.sweepRun( *setup.run, true );
  REQUIRE( report.reapedFiles.contains( step1Out ) );
  REQUIRE( report.reapedFiles.contains( step1Tfw ) );
  REQUIRE( report.reapedFiles.contains( step1Aux ) );
  REQUIRE( report.retainedFiles.contains( finalOut ) );
  REQUIRE( report.errors.isEmpty() );
  REQUIRE( report.reapedCount == 3 );

  // Verify file system state
  REQUIRE_FALSE( QFile::exists( step1Out ) );
  REQUIRE_FALSE( QFile::exists( step1Tfw ) );
  REQUIRE_FALSE( QFile::exists( step1Aux ) );
  REQUIRE( QFile::exists( finalOut ) );
}

TEST_CASE( "ArtifactGC never sweeps non-completed runs", "[workflow][v2][gc][gating]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  const QString step1Out = tmpDir.filePath( "intermediate.tif" );
  const QString finalOut = tmpDir.filePath( "final.tif" );
  QFile( step1Out ).open( QIODevice::WriteOnly );
  QFile( finalOut ).open( QIODevice::WriteOnly );

  ArtifactGC gc;

  SECTION( "Running run keeps everything" )
  {
    TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
    REQUIRE( setup.run->transitionTo( WorkflowRunState::Planning ) );
    REQUIRE( setup.run->transitionTo( WorkflowRunState::Ready ) );
    REQUIRE( setup.run->transitionTo( WorkflowRunState::Running ) );
    REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
    GCSweepReport report = gc.sweepRun( *setup.run );
    REQUIRE( report.reapedCount == 0 );
    REQUIRE( QFile::exists( step1Out ) );
  }

  SECTION( "Interrupted (resumable) run keeps everything" )
  {
    TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
    setup.run->forceSetState( WorkflowRunState::Interrupted );
    REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
    gc.sweepRun( *setup.run );
    REQUIRE( QFile::exists( step1Out ) );
  }

  SECTION( "Failed run keeps everything for retry" )
  {
    TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
    setup.run->forceSetState( WorkflowRunState::Failed );
    REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
    gc.sweepRun( *setup.run );
    REQUIRE( QFile::exists( step1Out ) );
  }
}

TEST_CASE( "ArtifactGC retains cache-hit intermediates shared with the result cache", "[workflow][v2][gc][gating]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  const QString step1Out = tmpDir.filePath( "cached_intermediate.tif" );
  const QString finalOut = tmpDir.filePath( "final.tif" );
  QFile( step1Out ).open( QIODevice::WriteOnly );
  QFile( finalOut ).open( QIODevice::WriteOnly );

  TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
  setup.intermediate->cacheHit = true;
  setup.intermediate->cachedOutputAssetId = "asset-shared-1";
  transitionToCompleted( *setup.run );

  ArtifactGC gc;
  REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
  GCSweepReport report = gc.sweepRun( *setup.run );
  REQUIRE( report.reapedCount == 0 );
  REQUIRE( QFile::exists( step1Out ) );
}

TEST_CASE( "ArtifactGC refuses paths outside the run workspace", "[workflow][v2][gc][gating]" )
{
  QTemporaryDir workspaceDir;
  QTemporaryDir outsideDir;
  REQUIRE( workspaceDir.isValid() );
  REQUIRE( outsideDir.isValid() );

  // A tampered/corrupt run whose "intermediate" points outside the workspace
  // (where the final output lives) must not have that file deleted.
  const QString outsideFile = outsideDir.filePath( "innocent.tif" );
  const QString finalOut = workspaceDir.filePath( "final.tif" );
  QFile( outsideFile ).open( QIODevice::WriteOnly );
  QFile( finalOut ).open( QIODevice::WriteOnly );

  TwoStepRun setup = makeTwoStepRun( outsideFile, finalOut );
  transitionToCompleted( *setup.run );

  ArtifactGC gc;
  REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
  GCSweepReport report = gc.sweepRun( *setup.run );
  REQUIRE( report.reapedCount == 0 );
  REQUIRE( QFile::exists( outsideFile ) );
  REQUIRE( QFile::exists( finalOut ) );
}

TEST_CASE( "ArtifactGC retains outputs of steps that did not complete", "[workflow][v2][gc][gating]" )
{
  QTemporaryDir tmpDir;
  REQUIRE( tmpDir.isValid() );

  const QString step1Out = tmpDir.filePath( "partial_intermediate.tif" );
  const QString finalOut = tmpDir.filePath( "final.tif" );
  QFile( step1Out ).open( QIODevice::WriteOnly );
  QFile( finalOut ).open( QIODevice::WriteOnly );

  TwoStepRun setup = makeTwoStepRun( step1Out, finalOut );
  setup.intermediate->status = "Failed"; // step did not finish; retry needs its file
  transitionToCompleted( *setup.run );

  ArtifactGC gc;
  REQUIRE( gc.inspectReapable( *setup.run ).isEmpty() );
  gc.sweepRun( *setup.run );
  REQUIRE( QFile::exists( step1Out ) );
}
