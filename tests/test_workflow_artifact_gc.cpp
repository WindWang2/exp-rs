#include <catch2/catch_test_macros.hpp>

#include "processing/framework/artifact_gc.h"
#include "workflow/workflow_run.h"
#include <QTemporaryDir>
#include <QFile>

using namespace sicnu::processing;
using namespace sicnu::workflow;

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

  auto run = WorkflowRun::createFromDefinition( def, "run-gc-1" );
  StepPlan *p1 = run->findStepPlan( "s1" );
  p1->outputLayerPath = step1Out.toStdString();
  p1->status = "Completed";

  StepPlan *p2 = run->findStepPlan( "s2" );
  p2->outputLayerPath = finalOut.toStdString();
  p2->status = "Completed";

  ArtifactGC gc;

  // Inspect reapable with retainFinalOutputs = true
  QStringList reapable = gc.inspectReapable( *run, true );
  REQUIRE( reapable.size() == 1 );
  REQUIRE( reapable.contains( step1Out ) );
  REQUIRE_FALSE( reapable.contains( finalOut ) );

  // Sweep run
  GCSweepReport report = gc.sweepRun( *run, true );
  REQUIRE( report.reapedFiles.contains( step1Out ) );
  REQUIRE( report.reapedFiles.contains( step1Tfw ) );
  REQUIRE( report.reapedFiles.contains( step1Aux ) );
  REQUIRE( report.retainedFiles.contains( finalOut ) );

  // Verify file system state
  REQUIRE_FALSE( QFile::exists( step1Out ) );
  REQUIRE_FALSE( QFile::exists( step1Tfw ) );
  REQUIRE_FALSE( QFile::exists( step1Aux ) );
  REQUIRE( QFile::exists( finalOut ) );
}
