// tests/test_agent_canvas_sync.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "app/workflow/pipeline_canvas_widget.h"
#include "processing/framework/json_params_converter.h"
#include "workflow/workflow_definition.h"

#include <QApplication>
#include <QJsonObject>
#include <QJsonArray>

using namespace sicnu::agent;
using namespace sicnu::workflow;
using namespace sicnu::workflow::gui;

static void ensureQtApp()
{
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_agent_canvas_sync";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
}

TEST_CASE( "Agent plan converts to WorkflowDefinition and loads on PipelineCanvasWidget", "[agent][canvas]" )
{
  ensureQtApp();

  QJsonObject planJson;
  planJson[QStringLiteral( "id" )] = QStringLiteral( "agent_plan" );
  planJson[QStringLiteral( "title" )] = QStringLiteral( "AI Agent Generated Workflow" );
  QJsonArray stepsArr;

  QJsonObject s1;
  s1[QStringLiteral( "id" )] = QStringLiteral( "step1" );
  s1[QStringLiteral( "name" )] = QStringLiteral( "rs_spectral_index" );
  QJsonObject args1;
  args1[QStringLiteral( "input" )] = QStringLiteral( "/tmp/landsat.tif" );
  args1[QStringLiteral( "index" )] = QStringLiteral( "NDVI" );
  s1[QStringLiteral( "arguments" )] = args1;
  stepsArr.append( s1 );

  QJsonObject s2;
  s2[QStringLiteral( "id" )] = QStringLiteral( "step2" );
  s2[QStringLiteral( "name" )] = QStringLiteral( "opencv_gaussian_blur" );
  QJsonObject args2;
  args2[QStringLiteral( "input" )] = QStringLiteral( "$step1.output" );
  args2[QStringLiteral( "kernel_size" )] = 5;
  s2[QStringLiteral( "arguments" )] = args2;
  stepsArr.append( s2 );

  planJson[QStringLiteral( "steps" )] = stepsArr;

  // Convert plan to WorkflowDefinition via the shared QJson→Json::Value
  // helper (ADR 0048)
  const Json::Value cppPlan = sicnu::processing::jsonValueFromQJson( planJson );

  WorkflowDefinition def;
  std::string parseErr;
  bool parsed = workflowDefinitionFromJson( cppPlan, def, parseErr );
  REQUIRE( parsed );
  REQUIRE( def.steps.size() == 2 );

  // Load onto PipelineCanvasWidget
  PipelineCanvasWidget canvas;
  canvas.loadWorkflowDefinition( def );

  REQUIRE( canvas.pipelineScene() != nullptr );
  REQUIRE( canvas.pipelineScene()->findNode( QStringLiteral( "step1" ) ) != nullptr );
  REQUIRE( canvas.pipelineScene()->findNode( QStringLiteral( "step2" ) ) != nullptr );
}
