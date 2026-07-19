#include <catch2/catch_test_macros.hpp>
#include "rs_classify_workflow_controller.h"

TEST_CASE( "Workflow: ClassSystem complete needs >=2 classes", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
  w.setClassCount( 1 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
  w.setClassCount( 2 );
  REQUIRE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
}

TEST_CASE( "Workflow: Preview does not complete TrainClassify", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setHasSourceRaster( true );
  w.setTrainingPixelCount( 100 );
  w.setTrainingClassCountWithPixels( 2 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::TrainClassify ) );
  // Soft gate allows navigation but primary needs data
  REQUIRE( w.canTrainOrClassify() );
  w.setHasFullClassifyResult( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::TrainClassify ) );
}

TEST_CASE( "Workflow: Evaluate only via explicit review flag", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setTrainingPixelCount( 50 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::Evaluate ) );
  w.setEvaluateReviewed( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::Evaluate ) );
}

TEST_CASE( "Workflow: PostProcess skip completes step", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::PostProcess ) );
  w.setPostProcessSkipped( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::PostProcess ) );
}

TEST_CASE( "Workflow: missingRequirements lists source for train", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setTrainingPixelCount( 100 );
  const auto miss = w.missingRequirements( RsClassifyStep::TrainClassify );
  REQUIRE_FALSE( miss.isEmpty() );
  // Chinese soft-gate messages must be non-empty for every reported item.
  for ( const QString &m : miss )
    REQUIRE_FALSE( m.trimmed().isEmpty() );
  w.setHasSourceRaster( true );
  REQUIRE( w.canTrainOrClassify() );
  REQUIRE( w.missingRequirements( RsClassifyStep::TrainClassify ).isEmpty() );
}

TEST_CASE( "Workflow: Chinese missingRequirements non-empty for all gated steps",
           "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  // Leave all inputs at defaults so each gated step has a non-empty list.
  const RsClassifyStep steps[] = {
    RsClassifyStep::ClassSystem,
    RsClassifyStep::Samples,
    RsClassifyStep::Evaluate,
    RsClassifyStep::TrainClassify,
    RsClassifyStep::Accuracy,
    RsClassifyStep::PostProcess,
    RsClassifyStep::Export,
  };
  for ( RsClassifyStep s : steps )
  {
    const auto miss = w.missingRequirements( s );
    // ClassSystem needs >=2 classes → message; Samples needs source+class, etc.
    REQUIRE_FALSE( miss.isEmpty() );
    for ( const QString &m : miss )
      REQUIRE_FALSE( m.trimmed().isEmpty() );
  }
}
