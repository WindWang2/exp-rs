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
#include "app/workflow/preset_catalog_widget.h"
#include "app/workflow/pipeline_editor_dock.h"
#include "operators/framework/rs_operator_registry.h"

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

TEST_CASE( "PresetCatalogWidget provides built-in remote sensing workflow recipes", "[workflow][presets]" )
{
  ensureApp();

  auto presets = PresetCatalogWidget::builtinPresets();
  REQUIRE( !presets.empty() );
  REQUIRE( presets.size() >= 4 );

  bool foundLandsat = false;
  bool foundDEM = false;
  bool foundOBIA = false;
  bool foundNDWI = false;

  for ( const auto &p : presets )
  {
    if ( p.id == "preset_landsat_ndvi_change" )
    {
      foundLandsat = true;
      REQUIRE( p.definition.steps.size() == 4 );
    }
    else if ( p.id == "preset_dem_terrain_slope" )
    {
      foundDEM = true;
      REQUIRE( p.definition.steps.size() == 3 );
    }
    else if ( p.id == "preset_obia_seg_classify" )
    {
      foundOBIA = true;
      REQUIRE( p.definition.steps.size() == 3 );
    }
    else if ( p.id == "preset_ndwi_water_extraction" )
    {
      foundNDWI = true;
      REQUIRE( p.definition.steps.size() == 2 );
    }
  }

  REQUIRE( foundLandsat == true );
  REQUIRE( foundDEM == true );
  REQUIRE( foundOBIA == true );
  REQUIRE( foundNDWI == true );

  PresetCatalogWidget widget;
  bool signalEmitted = false;
  WorkflowDefinition selectedDef;

  QObject::connect( &widget, &PresetCatalogWidget::presetSelected, [&]( const WorkflowDefinition &def ) {
    signalEmitted = true;
    selectedDef = def;
  } );

  emit widget.presetSelected( presets[0].definition );
  REQUIRE( signalEmitted == true );
  REQUIRE( selectedDef.id == presets[0].definition.id );
}

TEST_CASE( "PipelineEditorDock integrates canvas and preset sidebar", "[workflow][dock]" )
{
  ensureApp();

  PipelineEditorDock dock;
  REQUIRE( dock.pipelineCanvas() != nullptr );
  REQUIRE( dock.presetCatalog() != nullptr );

  auto presets = PresetCatalogWidget::builtinPresets();
  REQUIRE( !presets.empty() );

  dock.pipelineCanvas()->loadWorkflowDefinition( presets[0].definition );
  REQUIRE( dock.pipelineCanvas()->pipelineScene()->findNode( QString::fromStdString( presets[0].definition.steps[0].id ) ) != nullptr );
}

TEST_CASE( "Classification postprocessing visual DAG recipe and operators", "[workflow][preset][postprocess]" )
{
  ensureApp();

  auto presets = PresetCatalogWidget::builtinPresets();
  bool foundPostprocessRecipe = false;
  WorkflowDefinition postprocessDef;

  for ( const auto &p : presets )
  {
    if ( p.id == "preset_classification_postprocess_merge" )
    {
      foundPostprocessRecipe = true;
      postprocessDef = p.definition;
      break;
    }
  }

  REQUIRE( foundPostprocessRecipe == true );
  REQUIRE( postprocessDef.steps.size() == 3 );
  REQUIRE( postprocessDef.steps[0].id == "classify_step" );
  REQUIRE( postprocessDef.steps[1].id == "majority_filter" );
  REQUIRE( postprocessDef.steps[1].operatorId == "rs:majority_filter" );
  REQUIRE( postprocessDef.steps[2].id == "recode_step" );
  REQUIRE( postprocessDef.steps[2].operatorId == "rs:recode" );
  REQUIRE( postprocessDef.steps[2].uiMeta.portAddToMap["final_class_map"] == true );

  // Verify topological order
  std::vector<std::string> orderedIds;
  std::string error;
  REQUIRE( topologicalSortSteps( postprocessDef, orderedIds, error ) == true );
  REQUIRE( orderedIds.size() == 3 );
  CHECK( orderedIds[0] == "classify_step" );
  CHECK( orderedIds[1] == "majority_filter" );
  CHECK( orderedIds[2] == "recode_step" );

  // Verify operators registered in RSOperatorRegistry
  auto &reg = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( reg.hasOperator( "rs:majority_filter" ) );
  REQUIRE( reg.hasOperator( "rs:recode" ) );

  auto majOp = reg.create( "rs:majority_filter" );
  REQUIRE( majOp != nullptr );
  CHECK( majOp->name() == "rs:majority_filter" );

  auto recodeOp = reg.create( "rs:recode" );
  REQUIRE( recodeOp != nullptr );
  CHECK( recodeOp->name() == "rs:recode" );
}

TEST_CASE( "PipelineScene prevents duplicate edges and self-loops", "[workflow][graph]" )
{
  ensureApp();

  PipelineScene scene;

  StepDef stepA;
  stepA.id = "step_1";
  stepA.title = "Source";
  stepA.artifactOnSuccess = "out";

  StepDef stepB;
  stepB.id = "step_2";
  stepB.title = "Target";

  scene.addNode( stepA );
  scene.addNode( stepB );

  // Self loop should return nullptr
  auto *selfConn = scene.addConnection( "step_1", "out", "step_1", "input" );
  REQUIRE( selfConn == nullptr );

  // First valid connection
  auto *conn1 = scene.addConnection( "step_1", "out", "step_2", "input" );
  REQUIRE( conn1 != nullptr );
  REQUIRE( scene.connections().size() == 1 );

  // Duplicate connection should return existing without creating second item
  auto *conn2 = scene.addConnection( "step_1", "out", "step_2", "input" );
  REQUIRE( conn2 == conn1 );
  REQUIRE( scene.connections().size() == 1 );
}

TEST_CASE( "PipelineScene batches signals during loadWorkflowDefinition", "[workflow][signals]" )
{
  ensureApp();

  PipelineScene scene;

  WorkflowDefinition wf;
  wf.id = "batch_test";
  wf.title = "Batch Signal Test";

  for ( int i = 1; i <= 5; ++i )
  {
    StepDef s;
    s.id = "step_" + std::to_string( i );
    s.title = "Step " + std::to_string( i );
    s.artifactOnSuccess = "out";
    if ( i > 1 )
    {
      StepConnection inConn;
      inConn.fromStepId = "step_" + std::to_string( i - 1 );
      inConn.fromPort = "out";
      inConn.toPort = "input";
      s.inputs.push_back( inConn );
    }
    wf.steps.push_back( s );
  }

  int workflowChangedCount = 0;
  QObject::connect( &scene, &PipelineScene::workflowChanged, [&]() {
    workflowChangedCount++;
  } );

  // Loading a 5-step DAG should emit exactly 1 workflowChanged signal (not 1 + 5 + 4 = 10)
  scene.loadWorkflowDefinition( wf );
  REQUIRE( workflowChangedCount == 1 );
  REQUIRE( scene.nodes().size() == 5 );
  REQUIRE( scene.connections().size() == 4 );

  // Export roundtrip check (O(V+E) traversal)
  WorkflowDefinition exported = scene.exportWorkflowDefinition( wf );
  REQUIRE( exported.steps.size() == 5 );
  REQUIRE( exported.steps[4].inputs.size() == 1 );
  REQUIRE( exported.steps[4].inputs[0].fromStepId == "step_4" );
}

TEST_CASE( "PipelineCanvasWidget deleteSelected removes selected items", "[workflow][canvas][delete]" )
{
  ensureApp();

  PipelineCanvasWidget canvas;
  auto *scene = canvas.pipelineScene();

  StepDef s1;
  s1.id = "n1";
  s1.artifactOnSuccess = "out";
  StepDef s2;
  s2.id = "n2";

  scene->addNode( s1 );
  scene->addNode( s2 );
  auto *conn = scene->addConnection( "n1", "out", "n2", "input" );

  REQUIRE( scene->nodes().size() == 2 );
  REQUIRE( scene->connections().size() == 1 );

  // Select connection only and delete
  conn->setSelected( true );
  canvas.deleteSelected();
  REQUIRE( scene->connections().empty() );
  REQUIRE( scene->nodes().size() == 2 );

  // Re-add connection, select node n1 and delete
  scene->addConnection( "n1", "out", "n2", "input" );
  auto *node1 = scene->findNode( "n1" );
  REQUIRE( node1 != nullptr );
  node1->setSelected( true );
  canvas.deleteSelected();

  // Removing n1 should also clean up its incident connections in O(d)
  REQUIRE( scene->findNode( "n1" ) == nullptr );
  REQUIRE( scene->connections().empty() );
  REQUIRE( scene->nodes().size() == 1 );
}

TEST_CASE( "PresetCatalogWidget search filters presets by keyword", "[workflow][presets][filter]" )
{
  ensureApp();

  PresetCatalogWidget catalog;
  REQUIRE( catalog.visiblePresetCount() >= 4 );

  // Filter by "NDVI"
  catalog.findChild<QLineEdit *>()->setText( "NDVI" );
  REQUIRE( catalog.visiblePresetCount() == 1 );

  // Filter by non-existent query
  catalog.findChild<QLineEdit *>()->setText( "NonExistentPreset12345" );
  REQUIRE( catalog.visiblePresetCount() == 0 );

  // Clear query resets full list
  catalog.findChild<QLineEdit *>()->clear();
  REQUIRE( catalog.visiblePresetCount() >= 4 );
}

TEST_CASE( "PipelineNodeItem bounding rect padding and port shape precision", "[workflow][geometry]" )
{
  ensureApp();

  StepDef s;
  s.id = "geom_step";
  s.title = "Geometry Step";

  PipelineNodeItem node( s );
  auto *inPort = node.addInputPort( "in_raster", "Raster" );
  auto *outPort = node.addOutputPort( "out_raster", "Raster" );

  // Verify node bounding rect contains margin for 2.0px stroke
  QRectF bRect = node.boundingRect();
  REQUIRE( bRect.left() <= -1.5 );
  REQUIRE( bRect.top() <= -1.5 );

  // Port item shape should be precise (circular pin region, not entire node width)
  QPainterPath inShape = inPort->shape();
  REQUIRE( !inShape.isEmpty() );
  REQUIRE( inShape.boundingRect().width() < 30.0 );
}
