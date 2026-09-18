// tests/test_workflow_pipeline_ui.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPointer>
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
  auto it5 = std::find_if( exported.steps.begin(), exported.steps.end(), []( const StepDef &s ) {
    return s.id == "step_5";
  } );
  REQUIRE( it5 != exported.steps.end() );
  REQUIRE( it5->inputs.size() == 1 );
  REQUIRE( it5->inputs[0].fromStepId == "step_4" );
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
  node.addOutputPort( "out_raster", "Raster" );

  // Verify node bounding rect contains margin for 2.0px stroke
  QRectF bRect = node.boundingRect();
  REQUIRE( bRect.left() <= -1.5 );
  REQUIRE( bRect.top() <= -1.5 );

  // Port item shape should be precise (circular pin region, not entire node width)
  QPainterPath inShape = inPort->shape();
  REQUIRE( !inShape.isEmpty() );
  REQUIRE( inShape.boundingRect().width() < 30.0 );
}

namespace
{

/// Exposes the protected scene event handlers so drag interactions can be
/// driven without a real QGraphicsView / user input.
class TestablePipelineScene : public PipelineScene
{
public:
  using PipelineScene::mouseMoveEvent;
  using PipelineScene::mouseReleaseEvent;
};

int connectionItemCount( const QGraphicsScene &scene )
{
  int count = 0;
  for ( QGraphicsItem *item : scene.items() )
  {
    if ( dynamic_cast<PipelineConnectionItem *>( item ) )
      ++count;
  }
  return count;
}

PipelineConnectionItem *findTempConnection( const QGraphicsScene &scene )
{
  for ( QGraphicsItem *item : scene.items() )
  {
    auto *conn = dynamic_cast<PipelineConnectionItem *>( item );
    if ( conn && conn->targetPort() == nullptr )
      return conn;
  }
  return nullptr;
}

PipelineNodeItem *addDragNode( PipelineScene &scene, const QString &id )
{
  StepDef step;
  step.id = id.toStdString();
  step.title = id.toStdString();
  step.artifactOnSuccess = "out";
  return scene.addNode( step );
}

} // namespace

// Issue #1049: the temp wire lives only on its source port, so deleting the
// owning node used to leave both the wire and the scene's drag source dangling.
TEST_CASE( "PipelineScene drops an in-flight temp connection when its source node is deleted", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;

  auto *srcNode = addDragNode( scene, "drag_source" );
  StepDef targetStep;
  targetStep.id = "drag_target";
  targetStep.title = "Drag Target";
  REQUIRE( scene.addNode( targetStep ) != nullptr );

  auto *outPort = srcNode->findOutputPort( "out" );
  REQUIRE( outPort != nullptr );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 140.0, 90.0 ) );
  REQUIRE( findTempConnection( scene ) != nullptr );
  REQUIRE( scene.connections().empty() );

  REQUIRE( scene.removeNode( "drag_source" ) == true );
  REQUIRE( sourceGuard.isNull() ); // port really was destroyed
  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( connectionItemCount( scene ) == 0 );

  // The next mouse move / repaint must be a no-op, not a dereference of the
  // freed drag source.
  QGraphicsSceneMouseEvent moveEvent( QEvent::GraphicsSceneMouseMove );
  moveEvent.setScenePos( QPointF( 300.0, 200.0 ) );
  scene.mouseMoveEvent( &moveEvent );
  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( connectionItemCount( scene ) == 0 );
}

TEST_CASE( "PipelineScene drops an in-flight temp connection on clearWorkflow", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;
  auto *srcNode = addDragNode( scene, "clear_source" );
  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 80.0, 40.0 ) );
  REQUIRE( findTempConnection( scene ) != nullptr );

  scene.clearWorkflow();
  REQUIRE( sourceGuard.isNull() );
  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( scene.nodes().empty() );
  REQUIRE( scene.connections().empty() );

  QGraphicsSceneMouseEvent moveEvent( QEvent::GraphicsSceneMouseMove );
  moveEvent.setScenePos( QPointF( 50.0, 50.0 ) );
  scene.mouseMoveEvent( &moveEvent );
  REQUIRE( connectionItemCount( scene ) == 0 );
}

TEST_CASE( "PipelineScene drops an in-flight temp connection on loadWorkflowDefinition", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;
  auto *srcNode = addDragNode( scene, "reload_source" );
  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 10.0, 10.0 ) );
  REQUIRE( findTempConnection( scene ) != nullptr );

  WorkflowDefinition reloaded;
  reloaded.id = "reloaded";
  StepDef stepA;
  stepA.id = "r_a";
  stepA.artifactOnSuccess = "out";
  StepDef stepB;
  stepB.id = "r_b";
  StepConnection link;
  link.fromStepId = "r_a";
  link.fromPort = "out";
  link.toPort = "input";
  stepB.inputs.push_back( link );
  reloaded.steps.push_back( stepA );
  reloaded.steps.push_back( stepB );

  scene.loadWorkflowDefinition( reloaded );
  REQUIRE( sourceGuard.isNull() );
  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( scene.nodes().size() == 2 );
  REQUIRE( scene.connections().size() == 1 );
}

// Delete during an active drag: removeNode is handed the temp conn through no
// port list, so removeConnection itself must treat it as a cancel.
TEST_CASE( "PipelineScene removeConnection cancels a live temp wire", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;
  auto *srcNode = addDragNode( scene, "remove_conn_source" );
  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 30.0, 30.0 ) );
  auto *temp = findTempConnection( scene );
  REQUIRE( temp != nullptr );

  REQUIRE( scene.removeConnection( temp ) == true );
  REQUIRE( sourceGuard == outPort ); // source port untouched
  REQUIRE( outPort->connections().empty() );
  REQUIRE( findTempConnection( scene ) == nullptr );
}

TEST_CASE( "PipelineScene commits a temp connection dropped on an input port", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;
  auto *srcNode = addDragNode( scene, "commit_source" );
  StepDef targetStep;
  targetStep.id = "commit_target";
  targetStep.title = "Commit Target";
  auto *dstNode = scene.addNode( targetStep );

  auto *outPort = srcNode->findOutputPort( "out" );
  auto *inPort = dstNode->findInputPort( "input" );
  REQUIRE( outPort != nullptr );
  REQUIRE( inPort != nullptr );

  emit outPort->connectionDragStarted( outPort, outPort->sceneAnchorPos() );

  QGraphicsSceneMouseEvent moveEvent( QEvent::GraphicsSceneMouseMove );
  moveEvent.setScenePos( inPort->sceneAnchorPos() );
  scene.mouseMoveEvent( &moveEvent );
  REQUIRE( findTempConnection( scene ) != nullptr );

  QGraphicsSceneMouseEvent releaseEvent( QEvent::GraphicsSceneMouseRelease );
  releaseEvent.setScenePos( inPort->sceneAnchorPos() );
  releaseEvent.setButton( Qt::LeftButton );
  releaseEvent.setButtons( Qt::LeftButton );
  scene.mouseReleaseEvent( &releaseEvent );

  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( scene.connections().size() == 1 );
  auto *conn = *scene.connections().begin();
  REQUIRE( conn->sourcePort() == outPort );
  REQUIRE( conn->targetPort() == inPort );
  REQUIRE( inPort->connections().size() == 1 );
}

TEST_CASE( "PipelineCanvasWidget select-all delete cancels an in-flight connection drag", "[workflow][canvas][drag]" )
{
  ensureApp();

  PipelineCanvasWidget canvas;
  auto *scene = canvas.pipelineScene();

  auto *srcNode = addDragNode( *scene, "select_all_source" );
  StepDef targetStep;
  targetStep.id = "select_all_target";
  scene->addNode( targetStep );

  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );
  emit outPort->connectionDragStarted( outPort, QPointF( 20.0, 20.0 ) );
  REQUIRE( findTempConnection( *scene ) != nullptr );

  QKeyEvent selectAllEvent( QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier );
  QCoreApplication::sendEvent( &canvas, &selectAllEvent );
  REQUIRE( scene->selectedItems().size() >= 2 );

  QKeyEvent deleteEvent( QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier );
  QCoreApplication::sendEvent( &canvas, &deleteEvent );

  REQUIRE( sourceGuard.isNull() );
  REQUIRE( findTempConnection( *scene ) == nullptr );
  REQUIRE( connectionItemCount( *scene ) == 0 );
  REQUIRE( scene->nodes().empty() );
}

TEST_CASE( "PipelineCanvasWidget Esc cancels an in-flight connection drag", "[workflow][canvas][drag]" )
{
  ensureApp();

  PipelineCanvasWidget canvas;
  auto *scene = canvas.pipelineScene();

  auto *srcNode = addDragNode( *scene, "esc_source" );
  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 5.0, 5.0 ) );
  REQUIRE( findTempConnection( *scene ) != nullptr );

  QKeyEvent escapeEvent( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
  QCoreApplication::sendEvent( &canvas, &escapeEvent );

  REQUIRE( sourceGuard == outPort ); // source survives an Esc cancel
  REQUIRE( findTempConnection( *scene ) == nullptr );
  REQUIRE( connectionItemCount( *scene ) == 0 );
}

TEST_CASE( "PipelineCanvasWidget Delete on the drag source node cancels the drag", "[workflow][canvas][drag]" )
{
  ensureApp();

  PipelineCanvasWidget canvas;
  auto *scene = canvas.pipelineScene();

  auto *srcNode = addDragNode( *scene, "delete_key_source" );
  StepDef targetStep;
  targetStep.id = "delete_key_target";
  scene->addNode( targetStep );

  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );
  emit outPort->connectionDragStarted( outPort, QPointF( 15.0, 15.0 ) );
  REQUIRE( findTempConnection( *scene ) != nullptr );

  srcNode->setSelected( true );
  QKeyEvent deleteEvent( QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier );
  QCoreApplication::sendEvent( &canvas, &deleteEvent );

  REQUIRE( sourceGuard.isNull() );
  REQUIRE( findTempConnection( *scene ) == nullptr );
  REQUIRE( connectionItemCount( *scene ) == 0 );
  REQUIRE( scene->findNode( "delete_key_source" ) == nullptr );
}

TEST_CASE( "PipelineScene cancels the temp connection dropped in empty space", "[workflow][drag][lifetime]" )
{
  ensureApp();

  TestablePipelineScene scene;
  auto *srcNode = addDragNode( scene, "empty_drop_source" );
  auto *outPort = srcNode->findOutputPort( "out" );
  QPointer<PipelinePortItem> sourceGuard( outPort );

  emit outPort->connectionDragStarted( outPort, QPointF( 40.0, 40.0 ) );
  REQUIRE( findTempConnection( scene ) != nullptr );

  QGraphicsSceneMouseEvent releaseEvent( QEvent::GraphicsSceneMouseRelease );
  releaseEvent.setScenePos( QPointF( 1200.0, 900.0 ) );
  releaseEvent.setButton( Qt::LeftButton );
  releaseEvent.setButtons( Qt::LeftButton );
  scene.mouseReleaseEvent( &releaseEvent );

  REQUIRE( sourceGuard == outPort );
  REQUIRE( findTempConnection( scene ) == nullptr );
  REQUIRE( connectionItemCount( scene ) == 0 );
}
