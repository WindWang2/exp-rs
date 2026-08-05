// tests/test_classify_workflow_bridge.cpp — Catch2 unit tests for RsClassifyWorkflowBridge (ADR 0038)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "app/classification/rs_classify_workflow_bridge.h"
#include "app/classification/rs_classify_workflow_controller.h"

TEST_CASE( "RsClassifyWorkflowBridge opens and closes lab.classify.supervised session cleanly", "[classify][workflow][bridge]" )
{
  RsClassifyWorkflowBridge bridge;

  CHECK_FALSE( bridge.isOpen() );
  CHECK( bridge.sessionId().empty() );

  REQUIRE( bridge.open() );
  CHECK( bridge.isOpen() );
  CHECK_FALSE( bridge.sessionId().empty() );

  bridge.close();
  CHECK_FALSE( bridge.isOpen() );
  CHECK( bridge.sessionId().empty() );
}

TEST_CASE( "RsClassifyWorkflowBridge bindController automatically syncs completions and artifacts", "[classify][workflow][bridge]" )
{
  RsClassifyWorkflowBridge bridge;
  RsClassifyWorkflowController controller;

  controller.setHasSourceRaster( true );
  controller.setClassCount( 5 );

  bridge.bindController( &controller );
  CHECK( bridge.isOpen() );

  // Step 'classes' should be marked complete since classCount > 0
  const std::string sid = bridge.sessionId();
  const auto snapshot = bridge.runtime().state( sid );
  const bool hasClassesCompleted = std::find( snapshot.completedStepIds.begin(), snapshot.completedStepIds.end(), "classes" ) != snapshot.completedStepIds.end();
  CHECK( hasClassesCompleted );

  // Manual artifact setters record soft hints
  bridge.setSourceRasterArtifact( "/data/test_input.tif" );
  bridge.setClassifiedOutputArtifact( "/data/test_output.tif" );

  const auto updatedSnapshot = bridge.runtime().state( sid );
  CHECK( updatedSnapshot.artifacts.count( "source_raster" ) == 1 );
  CHECK( updatedSnapshot.artifacts.at( "source_raster" ) == "/data/test_input.tif" );

  CHECK( updatedSnapshot.artifacts.count( "classified_output" ) == 1 );
  CHECK( updatedSnapshot.artifacts.at( "classified_output" ) == "/data/test_output.tif" );
}
