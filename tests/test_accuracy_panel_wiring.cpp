// tests/test_accuracy_panel_wiring.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/classification/rs_accuracy_panel.h"
#include "app/classification/rs_classify_workflow_controller.h"
#include "analysis/classification/rs_accuracy_assessment.h"

#include <QApplication>

using Catch::Approx;

int main( int argc, char *argv[] )
{
  QApplication app( argc, argv );
  const int result = Catch::Session().run( argc, argv );
#ifdef _WIN32
  _exit( result );
#else
  return result;
#endif
}

TEST_CASE( "RsAccuracyPanel setResult and state transitions (#326)", "[classify][accuracy][panel]" )
{
  RsAccuracyPanel panel;
  CHECK_FALSE( panel.hasResult() );

  QVector<int> yt = { 1, 1, 2, 2, 3, 3 };
  QVector<int> yp = { 1, 2, 2, 2, 3, 3 };
  const auto r = RsAccuracyAssessment::compute( yt, yp );

  QHash<int, QString> names{
    { 1, "Water" },
    { 2, "Forest" },
    { 3, "Urban" }
  };

  panel.setResult( r, names );

  CHECK( panel.hasResult() );
  CHECK( panel.result().overallAccuracy == Approx( 5.0 / 6.0 ).margin( 1e-5 ) );
  CHECK( panel.classNames().value( 1 ) == "Water" );
  CHECK( panel.classNames().value( 2 ) == "Forest" );

  panel.clear();
  CHECK_FALSE( panel.hasResult() );
}

TEST_CASE( "RsClassifyWorkflowController accuracy metrics completion (#326)", "[classify][workflow][controller]" )
{
  RsClassifyWorkflowController controller;
  controller.setHasSourceRaster( true );
  controller.setClassCount( 3 );
  controller.setTrainingClassCountWithPixels( 3 );
  controller.setTrainingPixelCount( 100 );
  controller.setHasFullClassifyResult( true );

  CHECK_FALSE( controller.isStepComplete( RsClassifyStep::Accuracy ) );

  controller.setHasAccuracyMetrics( true );
  CHECK( controller.isStepComplete( RsClassifyStep::Accuracy ) );
}
