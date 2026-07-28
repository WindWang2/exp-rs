// tests/test_workflow_pipeline_ui.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include "app/workflow/pipeline_canvas_widget.h"
#include "app/workflow/pipeline_scene.h"
#include "app/workflow/pipeline_node_item.h"
#include "app/workflow/pipeline_port_item.h"
#include "app/workflow/pipeline_connection_item.h"
#include "shell/workflow_session_controller.h"
#include "workflow_definition.h"
#include "data/data_manager.h"

using namespace sicnu::workflow;
using namespace sicnu::workflow::gui;

static int fake_argc = 1;
static char fake_argv0[] = "test_workflow_pipeline_ui";
static char *fake_argv[] = { fake_argv0, nullptr };

static QApplication *ensureApp()
{
  if ( !qApp )
  {
    new QApplication( fake_argc, fake_argv );
  }
  return qApp;
}

TEST_CASE( "PipelineScene handles nodes and spatial positions", "[workflow][ui]" )
{
  ensureApp();

  PipelineScene scene;

  StepDef stepA;
  stepA.id = "node_a";
  stepA.title = "DEM Input";
  stepA.operatorId = "gdal:import";
  stepA.uiMeta.x = 120.0;
  stepA.uiMeta.y = 250.0;
  stepA.artifactOnSuccess = "dem_output";

  auto *nodeA = scene.addNode( stepA );
  REQUIRE( nodeA != nullptr );
  REQUIRE( nodeA->stepId() == "node_a" );
  REQUIRE( nodeA->pos().x() == 120.0 );
  REQUIRE( nodeA->pos().y() == 250.0 );

  // Move node position in scene
  nodeA->setPos( 350.0, 480.0 );
  REQUIRE( nodeA->pos().x() == 350.0 );
  REQUIRE( nodeA->pos().y() == 480.0 );

  // Export workflow definition and verify spatial (X, Y) roundtrip
  WorkflowDefinition baseDef;
  baseDef.id = "test_wf";
  WorkflowDefinition exportedDef = scene.exportWorkflowDefinition( baseDef );

  REQUIRE( exportedDef.steps.size() == 1 );
  REQUIRE( exportedDef.steps[0].id == "node_a" );
  REQUIRE( exportedDef.steps[0].uiMeta.x == 350.0 );
  REQUIRE( exportedDef.steps[0].uiMeta.y == 480.0 );
}

TEST_CASE( "PipelineScene handles port connections and validation", "[workflow][ui]" )
{
  ensureApp();

  PipelineScene scene;

  StepDef stepA;
  stepA.id = "step_a";
  stepA.title = "Raster Reader";
  stepA.artifactOnSuccess = "raster_out";
  stepA.uiMeta = { 100.0, 100.0 };

  StepDef stepB;
  stepB.id = "step_b";
  stepB.title = "Slope Calculator";
  stepB.artifactOnSuccess = "slope_out";
  stepB.uiMeta = { 400.0, 100.0 };
  StepConnection connIn;
  connIn.fromStepId = "step_a";
  connIn.fromPort = "raster_out";
  connIn.toPort = "input";
  stepB.inputs.push_back( connIn );

  auto *nodeA = scene.addNode( stepA );
  auto *nodeB = scene.addNode( stepB );

  REQUIRE( nodeA != nullptr );
  REQUIRE( nodeB != nullptr );

  auto *connItem = scene.addConnection( "step_a", "raster_out", "step_b", "input" );
  REQUIRE( connItem != nullptr );
  REQUIRE( connItem->sourcePort() != nullptr );
  REQUIRE( connItem->targetPort() != nullptr );
  REQUIRE( connItem->sourcePort()->nodeItem() == nodeA );
  REQUIRE( connItem->targetPort()->nodeItem() == nodeB );

  // Port type compatibility check
  REQUIRE( validatePortConnection( "Raster", "Raster" ) == true );
  REQUIRE( validatePortConnection( "Raster", "Vector" ) == false );
}

TEST_CASE( "PipelineNodeItem status badge updates", "[workflow][ui]" )
{
  ensureApp();

  StepDef step;
  step.id = "status_node";
  step.title = "Status Test";

  PipelineNodeItem nodeItem( step );
  REQUIRE( nodeItem.status() == NodeStatus::Idle );

  nodeItem.setStatusFromString( "running" );
  REQUIRE( nodeItem.status() == NodeStatus::Running );

  nodeItem.setStatusFromString( "success" );
  REQUIRE( nodeItem.status() == NodeStatus::Success );

  nodeItem.setStatusFromString( "failed" );
  REQUIRE( nodeItem.status() == NodeStatus::Failure );
}

TEST_CASE( "PipelinePortItem Add to Map toggle", "[workflow][ui]" )
{
  ensureApp();

  StepDef step;
  step.id = "port_node";
  step.title = "Port Test";

  PipelineNodeItem nodeItem( step );
  auto *outPort = nodeItem.addOutputPort( "result", "Raster" );
  REQUIRE( outPort != nullptr );
  REQUIRE( outPort->addToMap() == false );

  bool signalFired = false;
  QObject::connect( outPort, &PipelinePortItem::addToMapToggled, [&]( PipelinePortItem *, bool enabled ) {
    signalFired = enabled;
  } );

  outPort->setAddToMap( true );
  REQUIRE( outPort->addToMap() == true );
  REQUIRE( signalFired == true );
}

TEST_CASE( "PipelineCanvasWidget loads and exports workflow definitions", "[workflow][ui]" )
{
  ensureApp();

  PipelineCanvasWidget canvas;

  WorkflowDefinition wf;
  wf.id = "demo_pipeline";
  wf.title = "Demo Pipeline";

  StepDef s1;
  s1.id = "s1";
  s1.title = "Step 1";
  s1.uiMeta = { 50.0, 50.0 };
  s1.artifactOnSuccess = "out1";

  StepDef s2;
  s2.id = "s2";
  s2.title = "Step 2";
  s2.uiMeta = { 300.0, 50.0 };
  StepConnection inConn;
  inConn.fromStepId = "s1";
  inConn.fromPort = "out1";
  inConn.toPort = "input";
  s2.inputs.push_back( inConn );

  wf.steps.push_back( s1 );
  wf.steps.push_back( s2 );

  canvas.loadWorkflowDefinition( wf );

  auto *scene = canvas.pipelineScene();
  REQUIRE( scene->findNode( "s1" ) != nullptr );
  REQUIRE( scene->findNode( "s2" ) != nullptr );

  WorkflowDefinition exported = canvas.exportWorkflowDefinition( wf );
  REQUIRE( exported.steps.size() == 2 );
}

TEST_CASE( "WorkflowSessionController drives canvas step status updates", "[workflow][controller]" )
{
  ensureApp();

  WorkflowSessionController controller;
  PipelineCanvasWidget canvas;
  controller.bindCanvas( &canvas );

  WorkflowDefinition wf;
  wf.id = "status_pipeline";
  wf.title = "Status Test Pipeline";

  StepDef s1;
  s1.id = "step_1";
  s1.title = "Step 1";
  s1.uiMeta = { 10.0, 10.0 };

  wf.steps.push_back( s1 );
  canvas.loadWorkflowDefinition( wf );

  auto *nodeItem = canvas.pipelineScene()->findNode( "step_1" );
  REQUIRE( nodeItem != nullptr );
  REQUIRE( nodeItem->status() == NodeStatus::Idle );

  // Emit status change signal from controller
  emit controller.stepStatusChanged( "step_1", "running" );
  REQUIRE( nodeItem->status() == NodeStatus::Running );

  emit controller.stepStatusChanged( "step_1", "success" );
  REQUIRE( nodeItem->status() == NodeStatus::Success );
}

TEST_CASE( "PipelineScene preserves port addToMap toggle in WorkflowDefinition JSON", "[workflow][ui]" )
{
  ensureApp();

  WorkflowDefinition wf;
  wf.id = "add_to_map_pipeline";
  wf.title = "Add To Map Test";

  StepDef s1;
  s1.id = "step_1";
  s1.title = "Step 1";
  s1.uiMeta.x = 100.0;
  s1.uiMeta.y = 150.0;
  s1.uiMeta.portAddToMap["output"] = true;

  wf.steps.push_back( s1 );

  PipelineCanvasWidget canvas;
  canvas.loadWorkflowDefinition( wf );

  auto *scene = canvas.pipelineScene();
  auto *node = scene->findNode( "step_1" );
  REQUIRE( node != nullptr );

  auto *outPort = node->findOutputPort( "output" );
  REQUIRE( outPort != nullptr );
  REQUIRE( outPort->addToMap() == true );

  WorkflowDefinition exported = canvas.exportWorkflowDefinition( wf );
  REQUIRE( exported.steps.size() == 1 );
  REQUIRE( exported.steps[0].uiMeta.portAddToMap["output"] == true );

  // JSON roundtrip verification
  Json::Value json = workflowDefinitionToJson( exported );
  REQUIRE( json["steps"][0]["meta"]["ui"]["portAddToMap"]["output"].asBool() == true );

  WorkflowDefinition restoredDef;
  std::string error;
  REQUIRE( workflowDefinitionFromJson( json, restoredDef, error ) == true );
  REQUIRE( restoredDef.steps[0].uiMeta.portAddToMap["output"] == true );
}

TEST_CASE( "WorkflowSessionController integrates DataManager TaskTemporary assets and port display", "[workflow][catalog]" )
{
  ensureApp();

  sicnu::data::DataManager dataManager;
  WorkflowSessionController controller;
  controller.setDataManager( &dataManager );
  REQUIRE( controller.dataManager() == &dataManager );

  PipelineCanvasWidget canvas;
  controller.bindCanvas( &canvas );

  WorkflowDefinition wf;
  wf.id = "catalog_test";
  wf.title = "Catalog Test";

  StepDef s1;
  s1.id = "step_1";
  s1.title = "Step 1";
  s1.uiMeta.portAddToMap["output"] = true;
  wf.steps.push_back( s1 );

  canvas.loadWorkflowDefinition( wf );

  bool loadRequested = false;
  QString loadedPath;
  QObject::connect( &controller, &WorkflowSessionController::requestLoadRaster, [&]( const QString &path ) {
    loadRequested = true;
    loadedPath = path;
  } );

  // Sweep task temporaries (initially empty)
  auto reapResult = controller.reapTaskTemporaries();
  REQUIRE( reapResult.reapedCount == 0 );
}


