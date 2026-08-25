#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_definition.h"
#include "workflow/workflow_runtime.h"
#include "workflow/builtin_definitions.h"

#include <json/json.h>
#include <string>
#include <vector>

int main( int argc, char *argv[] )
{
  const int result = Catch::Session().run( argc, argv );
#ifdef _WIN32
  _exit( result );
#else
  return result;
#endif
}

TEST_CASE( "WorkflowDefinition spatial UI metadata serialization", "[workflow][dag]" )
{
  sicnu::workflow::WorkflowDefinition def;
  def.id = "test_pipeline";
  def.title = "Test Pipeline";

  sicnu::workflow::StepDef step1;
  step1.id = "step1";
  step1.title = "Step 1: Input";
  step1.operatorId = "opencv:sobel";
  step1.uiMeta.x = 150.0;
  step1.uiMeta.y = 200.0;

  sicnu::workflow::StepDef step2;
  step2.id = "step2";
  step2.title = "Step 2: Blur";
  step2.operatorId = "opencv:gaussian_blur";
  step2.uiMeta.x = 400.0;
  step2.uiMeta.y = 200.0;
  step2.inputs.push_back( { "step1", "output", "input" } );

  def.steps.push_back( step1 );
  def.steps.push_back( step2 );

  // Serialize to JSON
  Json::Value json = sicnu::workflow::workflowDefinitionToJson( def );
  CHECK( json["id"].asString() == "test_pipeline" );
  CHECK( json["steps"].size() == 2 );
  CHECK( json["steps"][0]["meta"]["ui"]["x"].asDouble() == 150.0 );
  CHECK( json["steps"][0]["meta"]["ui"]["y"].asDouble() == 200.0 );
  CHECK( json["steps"][1]["inputs"].size() == 1 );
  CHECK( json["steps"][1]["inputs"][0]["fromStepId"].asString() == "step1" );

  // Deserialize back from JSON
  sicnu::workflow::WorkflowDefinition restored;
  std::string error;
  bool ok = sicnu::workflow::workflowDefinitionFromJson( json, restored, error );
  REQUIRE( ok );
  CHECK( restored.id == "test_pipeline" );
  CHECK( restored.steps.size() == 2 );
  CHECK( restored.steps[0].uiMeta.x == 150.0 );
  CHECK( restored.steps[0].uiMeta.y == 200.0 );
  CHECK( restored.steps[1].inputs.size() == 1 );
  CHECK( restored.steps[1].inputs[0].fromStepId == "step1" );
}

TEST_CASE( "Topological sorting and DAG cycle detection", "[workflow][dag]" )
{
  sicnu::workflow::WorkflowDefinition def;
  def.id = "dag_pipeline";

  sicnu::workflow::StepDef A{ .id = "A" };
  sicnu::workflow::StepDef B{ .id = "B" };
  sicnu::workflow::StepDef C{ .id = "C" };

  // B depends on A, C depends on B (A -> B -> C)
  B.inputs.push_back( { "A", "output", "input" } );
  C.inputs.push_back( { "B", "output", "input" } );

  def.steps = { C, A, B }; // Shuffle order

  std::vector<std::string> orderedIds;
  std::string error;
  bool ok = sicnu::workflow::topologicalSortSteps( def, orderedIds, error );
  REQUIRE( ok );
  REQUIRE( orderedIds.size() == 3 );
  CHECK( orderedIds[0] == "A" );
  CHECK( orderedIds[1] == "B" );
  CHECK( orderedIds[2] == "C" );

  // Introduce a cycle: A depends on C (A -> B -> C -> A)
  A.inputs.push_back( { "C", "output", "input" } );
  def.steps = { A, B, C };

  std::vector<std::string> cycleOrderedIds;
  bool cycleOk = sicnu::workflow::topologicalSortSteps( def, cycleOrderedIds, error );
  CHECK_FALSE( cycleOk );
  CHECK( error.find( "cycle" ) != std::string::npos );
}

TEST_CASE( "Port parameter type validation", "[workflow][dag]" )
{
  CHECK( sicnu::workflow::validatePortConnection( "RasterLayer", "RasterLayer" ) );
  CHECK( sicnu::workflow::validatePortConnection( "RasterLayer", "Raster" ) );
  CHECK( sicnu::workflow::validatePortConnection( "VectorLayer", "VectorLayer" ) );
  CHECK( sicnu::workflow::validatePortConnection( "Number", "Double" ) );
  CHECK( sicnu::workflow::validatePortConnection( "Number", "Integer" ) );

  // Incompatible types
  CHECK_FALSE( sicnu::workflow::validatePortConnection( "RasterLayer", "VectorLayer" ) );
  CHECK_FALSE( sicnu::workflow::validatePortConnection( "VectorLayer", "RasterLayer" ) );
}
