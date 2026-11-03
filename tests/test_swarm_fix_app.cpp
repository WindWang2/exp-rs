// tests/test_swarm_fix_app.cpp — regression anchors for the 2026-11-04 swarm
// review APP-series findings owned by src/app/** (SWARM_REVIEW.md).
//
// One TEST_CASE per finding; each pins the POST-fix behavior (the pre-fix code
// silently rewrote state, dereferenced freed memory, dropped parameters, or
// failed open).
//
// Covered findings:
//   APP-D-2 [P2] dialogs/dialog_utils.cpp — populateRasterLayerCombo stored a
//                 raw QgsRasterLayer* in the combo item data, so a layer
//                 removed from the project left the Fusion / Terrain dialogs
//                 dereferencing freed memory. The layer id is now stored and
//                 resolved through QgsProject on every use.
//   APP-R-1 [P2] workflow/pipeline_scene.cpp — exportWorkflowDefinition
//                 rebuilt every StepDef from an empty base definition,
//                 silently dropping params / gates / resourceEstimateMb /
//                 verificationPolicy (a reopen reverted to operator
//                 defaults). The loaded definition is now the
//                 document-as-truth backfill source.
//   APP-R-2 [P2] shell/workflow_session_controller.cpp — the canvas Run
//                 button executed whatever definition the session happened
//                 to hold (deleted steps included), or silently did nothing
//                 when no session was open. The canvas definition is now
//                 synced into the runtime before the run starts.
//   APP-R-4 [P3] georeferencer/rs_georef_params_panel.cpp — the DOF label
//                 clamped a negative degrees-of-freedom to 0, hiding the
//                 "not enough points to fit" state its own tooltip documents.
//   APP-R-5 [P3] obia/rs_segment_select_tool.cpp — the map→pixel conversion
//                 ignored the geotransform rotation terms (gt[2] / gt[4]).
//   APP-R-6 [P3] classification/rs_classifier_setup_bar.cpp — an unparsable
//                 band token was dropped silently and an all-invalid list
//                 fell back to the default bands.
//   APP-S-2 [P3] shell/workflow_session_controller.cpp — cancelActiveRun
//                 cleared the tracked ids synchronously, so a Completed
//                 result that raced the cancel could no longer match
//                 onTaskUpdated: its commit / artifact registration was
//                 silently dropped (orphan temp files).
//
// CMake registration (main agent — tests/CMakeLists.txt; modeled on
// test_workflow_pipeline_ui, which compiles the same shell sources):
//
//   qt_add_executable(test_swarm_fix_app
//     test_swarm_fix_app.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_scene.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_node_item.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_port_item.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_connection_item.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_canvas_widget.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/workflow_session_controller.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/task_panel_host.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/workbench_enum_provider.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_result_summary.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_georef_params_panel.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_rms_scatter_widget.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/obia/rs_segment_select_tool.cpp
//     ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classifier_setup_bar.cpp
//   )
//   target_link_libraries(test_swarm_fix_app PRIVATE
//     Catch2::Catch2WithMain
//     Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent
//     qgis_core qgis_gui qgis_analysis
//     sicnu_processing sicnu_operators sicnu_workflow sicnu_workflow_runtime
//     sicnu_task_center sicnu_data sicnu_jobs
//     Sicnu::help
//   )
//   target_include_directories(test_swarm_fix_app PRIVATE
//     ${CMAKE_SOURCE_DIR}/src
//     ${CMAKE_SOURCE_DIR}/src/app
//     ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
//     ${CMAKE_SOURCE_DIR}/src/app/georeferencer
//     ${CMAKE_SOURCE_DIR}/src/app/obia
//     ${CMAKE_BINARY_DIR}
//   )
//   set_target_properties(test_swarm_fix_app PROPERTIES AUTOMOC ON)
//   sicnu_link_jsoncpp(test_swarm_fix_app)
//   sicnu_discover_tests(test_swarm_fix_app)

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointF>
#include <QPushButton>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsmaptopixel.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgsrasterlayer.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <json/json.h>

#include <vector>

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/framework/task_center.h"

#include "app/dialogs/dialog_utils.h"
#include "app/workflow/pipeline_canvas_widget.h"
#include "app/workflow/pipeline_node_item.h"
#include "app/workflow/pipeline_scene.h"
#include "app/shell/task_panel_host.h"
#include "app/shell/workflow_session_controller.h"
#include "app/georeferencer/rs_georef_params_panel.h"
#include "app/obia/rs_segment_select_tool.h"
#include "app/classification/rs_classifier_setup_bar.h"

namespace
{
// QgsApplication is a QApplication: one lazy instance serves every case. It is
// intentionally leaked (never destroyed) so widgets outliving Catch2's
// teardown do not touch a dead QApplication — the pattern
// test_apply_mask_dialog.cpp / test_obia_main_window.cpp already use.
void ensureApp()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_swarm_fix_app";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  Q_UNUSED( application );
  QgsApplication::initQgis();
}

// The submit/cancel paths drive the TaskCenter singleton; without a clean
// shutdown on both sides of a case, in-flight worker records outlive the test
// binary's teardown and the process segfaults at exit (same guard as
// test_workflow_session_controller.cpp).
struct TaskCenterGuard
{
  TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
  ~TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
};

// Exposes the protected canvasReleaseEvent for synthetic map clicks (same
// technique as test_obia_main_window.cpp).
class TestableRsSegmentSelectTool : public RsSegmentSelectTool
{
  public:
    using RsSegmentSelectTool::RsSegmentSelectTool;
    using RsSegmentSelectTool::canvasReleaseEvent;
};

/// Small single-band GeoTIFF so a real QgsRasterLayer can join the project.
bool writeTestRaster( const QString &path, QString *error )
{
  const std::vector<std::vector<float>> bands = { { 1.f, 2.f, 3.f, 4.f } };
  const std::array<double, 6> gt = { 0.0, 1.0, 0.0, 4.0, 0.0, -1.0 };
  return writeGdalOutput( path, 2, 2, bands, gt, QStringLiteral( "EPSG:4326" ), error );
}

sicnu::workflow::StepDef makeStep( const std::string &id, const std::string &operatorId )
{
  sicnu::workflow::StepDef step;
  step.id = id;
  step.title = id;
  step.operatorId = operatorId;
  step.artifactOnSuccess = "output";
  step.uiMeta.x = 10.0;
  step.uiMeta.y = 20.0;
  return step;
}

} // namespace

// ---------------------------------------------------------------------------
// APP-D-2 — combo entries are layer ids, resolved through the project
// ---------------------------------------------------------------------------
TEST_CASE( "APP-D-2: populateRasterLayerCombo stores layer ids and resolves them through the project",
           "[swarm][app][dialog-utils]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString rasterPath = dir.filePath( QStringLiteral( "combo.tif" ) );
  QString writeError;
  REQUIRE( writeTestRaster( rasterPath, &writeError ) );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "combo_raster" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  QComboBox combo;
  populateRasterLayerCombo( &combo );

  const int idx = combo.findData( layer->id() );
  REQUIRE( idx >= 0 );
  combo.setCurrentIndex( idx );

  // The item data must be the layer id (a QString), never a raw pointer.
  CHECK( combo.itemData( idx ).canConvert<QString>() );
  CHECK( currentRasterLayerFromCombo( &combo ) == layer );

  // Removing the layer from the project must resolve to nullptr instead of
  // leaving the dialogs dereferencing freed memory (the pre-fix UAF).
  const QString staleId = layer->id();
  QgsProject::instance()->removeMapLayer( staleId );
  CHECK( rasterLayerFromComboData( combo.itemData( idx ) ) == nullptr );
  CHECK( currentRasterLayerFromCombo( &combo ) == nullptr );

  QgsProject::instance()->removeMapLayer( staleId );
}

// ---------------------------------------------------------------------------
// APP-R-1 — export backfills the invisible step fields
// ---------------------------------------------------------------------------
TEST_CASE( "APP-R-1: PipelineScene export backfills params/gates/resource/verification from the loaded definition",
           "[swarm][app][workflow]" )
{
  ensureApp();

  sicnu::workflow::gui::PipelineScene scene;

  sicnu::workflow::WorkflowDefinition def;
  def.id = "backfill_wf";
  def.title = "Backfill Workflow";

  sicnu::workflow::StepDef keep = makeStep( "keep", "rs:contrast_stretch" );
  keep.params = Json::Value( Json::objectValue );
  keep.params["threshold"] = 42;
  sicnu::workflow::GateDef gate;
  gate.require = "hasArtifact:output";
  gate.hint = "run the previous step first";
  keep.gates.push_back( gate );
  keep.resourceEstimateMb = 1234;
  keep.verificationPolicy = "raster";

  sicnu::workflow::StepDef drop = makeStep( "drop", "rs:contrast_stretch" );

  def.steps = { keep, drop };
  scene.loadWorkflowDefinition( def );

  // The canvas projection still owns the visible fields: moving a node must
  // win over the loaded position.
  auto *node = scene.findNode( QStringLiteral( "keep" ) );
  REQUIRE( node != nullptr );
  node->setPos( 350.0, 480.0 );

  sicnu::workflow::WorkflowDefinition base;
  base.id = "exported_pipeline";
  base.title = "Exported Pipeline";

  sicnu::workflow::WorkflowDefinition exported = scene.exportWorkflowDefinition( base );
  REQUIRE( exported.steps.size() == 2 );

  const sicnu::workflow::StepDef *kept = nullptr;
  for ( const auto &step : exported.steps )
    if ( step.id == "keep" )
      kept = &step;
  REQUIRE( kept != nullptr );

  // The invisible fields the node items cannot hold must survive the export.
  CHECK( kept->params.isObject() );
  CHECK( kept->params["threshold"].asInt() == 42 );
  REQUIRE( kept->gates.size() == 1 );
  CHECK( kept->gates.front().require == "hasArtifact:output" );
  CHECK( kept->resourceEstimateMb == 1234u );
  CHECK( kept->verificationPolicy == "raster" );
  CHECK( kept->uiMeta.x == 350.0 );
  CHECK( kept->uiMeta.y == 480.0 );

  // A step deleted from the canvas must not reappear with its old params.
  scene.removeNode( QStringLiteral( "drop" ) );
  exported = scene.exportWorkflowDefinition( base );
  REQUIRE( exported.steps.size() == 1 );
  CHECK( exported.steps.front().id == "keep" );
  CHECK( exported.steps.front().params["threshold"].asInt() == 42 );

  // Clearing the canvas drops the backfill source: a rebuilt node must not
  // inherit the cleared definition's params (stale-state leak).
  scene.clearWorkflow();
  CHECK( scene.lastLoadedDefinition().steps.empty() );
  scene.addNode( keep );
  exported = scene.exportWorkflowDefinition( base );
  REQUIRE( exported.steps.size() == 1 );
  CHECK( exported.steps.front().params.isNull() );
}

// ---------------------------------------------------------------------------
// APP-R-2 — canvas Run syncs the canvas definition into the runtime
// ---------------------------------------------------------------------------
TEST_CASE( "APP-R-2: canvas Run syncs the canvas definition into the runtime session",
           "[swarm][app][workflow][shell]" )
{
  ensureApp();
  TaskCenterGuard guard;

  // Heap + explicit delete: a stack QWidget outliving Catch2's teardown of
  // the QApplication this binary lazily constructs segfaults at exit.
  auto *panel = new TaskPanelHost();
  WorkflowSessionController controller;
  controller.bindPanel( panel );

  auto *canvas = new sicnu::workflow::gui::PipelineCanvasWidget();
  controller.bindCanvas( canvas );

  sicnu::workflow::WorkflowDefinition wf;
  wf.id = "canvas_demo";
  wf.title = "Canvas Demo";
  wf.steps = { makeStep( "step_a", "rs:contrast_stretch" ),
               makeStep( "step_b", "rs:contrast_stretch" ) };
  canvas->loadWorkflowDefinition( wf );

  QStringList idleSteps;
  QObject::connect( &controller, &WorkflowSessionController::stepStatusChanged,
                    [&idleSteps]( const QString &stepId, const QString &status ) {
                      if ( status == QLatin1String( "idle" ) )
                        idleSteps << stepId;
                    } );

  // Pre-fix: no session was open, so Run returned silently and nothing ran.
  controller.runFullWorkflow();
  CHECK( idleSteps.contains( QStringLiteral( "step_a" ) ) );
  CHECK( idleSteps.contains( QStringLiteral( "step_b" ) ) );
  CHECK_FALSE( controller.activeSessionId().isEmpty() );

  // A step deleted on the canvas must not be executed any more: the run is
  // driven by the canvas definition, not by a stale session snapshot.
  canvas->pipelineScene()->removeNode( QStringLiteral( "step_b" ) );
  controller.cancelActiveRun();
  idleSteps.clear();
  controller.runFullWorkflow();
  CHECK( idleSteps.contains( QStringLiteral( "step_a" ) ) );
  CHECK_FALSE( idleSteps.contains( QStringLiteral( "step_b" ) ) );

  controller.cancelActiveRun();
  delete canvas;
  delete panel;
}

// ---------------------------------------------------------------------------
// APP-S-2 — cancel keeps the tracked ids armed for the terminal callback
// ---------------------------------------------------------------------------
TEST_CASE( "APP-S-2: cancelActiveRun keeps the tracked ids armed for the terminal callback",
           "[swarm][app][workflow][shell]" )
{
  ensureApp();
  TaskCenterGuard guard;

  auto *panel = new TaskPanelHost();
  WorkflowSessionController controller;
  controller.bindPanel( panel );
  controller.registerBuiltins();

  const QString sessionId = controller.openTool( QStringLiteral( "lab.preprocess.optical" ) );
  REQUIRE( !sessionId.isEmpty() );

  controller.runFullWorkflow();
  REQUIRE( controller.isRunInFlight() );
  const long pipelineId = controller.activePipelineId();
  const long taskId = controller.pendingTaskId();
  REQUIRE( ( pipelineId >= 0 || taskId >= 0 ) );

  QStringList messages;
  QObject::connect( &controller, &WorkflowSessionController::statusMessage,
                    [&messages]( const QString &m ) { messages << m; } );

  controller.cancelActiveRun();
  CHECK_FALSE( controller.isRunInFlight() );
  // Pre-fix the ids were cleared here, so a terminal record that raced the
  // cancel (Canceled, or Completed when the work finished first) could no
  // longer match onTaskUpdated and its commit / artifact registration was
  // silently dropped.
  CHECK( controller.activePipelineId() == pipelineId );
  CHECK( controller.pendingTaskId() == taskId );

  bool sawStopping = false;
  for ( const QString &m : messages )
    if ( m.contains( QLatin1String( "stopping" ) ) )
      sawStopping = true;
  CHECK( sawStopping );

  // Idempotent second cancel while the terminal record is still pending.
  controller.cancelActiveRun();
  CHECK( controller.activePipelineId() == pipelineId );
  CHECK( controller.pendingTaskId() == taskId );

  delete panel;
}

// ---------------------------------------------------------------------------
// APP-R-4 — negative DOF is shown, not clamped to 0
// ---------------------------------------------------------------------------
TEST_CASE( "APP-R-4: georeferencer DOF label shows the negative deficit with a warn tone",
           "[swarm][app][georef]" )
{
  ensureApp();

  RsGeorefParamsPanel panel;
  auto *dofLabel = panel.findChild<QLabel *>( QStringLiteral( "rsDofLabel" ) );
  REQUIRE( dofLabel != nullptr );

  panel.setMinimumGcpCount( 10 );

  // Not enough points: DOF must read as the real deficit (the label's own
  // tooltip documents "DOF < 0: not enough points to fit") and be marked.
  panel.setActualGcpCount( 4 );
  CHECK( dofLabel->text() == QString::number( -6 ) );
  CHECK( dofLabel->property( "rsTone" ).toString() == QStringLiteral( "warn" ) );

  // Exactly determined: no warning tone.
  panel.setActualGcpCount( 10 );
  CHECK( dofLabel->text() == QString::number( 0 ) );
  CHECK( ( !dofLabel->property( "rsTone" ).isValid()
           || dofLabel->property( "rsTone" ).toString().isEmpty() ) );

  // Over-determined: still no warning tone.
  panel.setActualGcpCount( 12 );
  CHECK( dofLabel->text() == QString::number( 2 ) );
  CHECK( ( !dofLabel->property( "rsTone" ).isValid()
           || dofLabel->property( "rsTone" ).toString().isEmpty() ) );
}

// ---------------------------------------------------------------------------
// APP-R-5 — inverse affine honours the rotation terms
// ---------------------------------------------------------------------------
TEST_CASE( "APP-R-5: segment select tool maps clicks through the full inverse affine",
           "[swarm][app][obia][maptool]" )
{
  ensureApp();

  auto canvas = std::make_unique<QgsMapCanvas>();
  auto *tool = new TestableRsSegmentSelectTool( canvas.get() );

  // 4x4 label grid; segment 7 lives only at (row 1, col 2).
  QVector<quint32> labels( 16, 0 );
  labels[1 * 4 + 2] = 7;
  RsSegmentMap segMap( labels, 4, 4 );
  tool->setSegmentMap( segMap );

  // Rotated geotransform: gt[2] (row → x) and gt[4] (col → y) are non-zero.
  const double gt[6] = { 100.0, 2.0, 1.0, 500.0, 1.0, -3.0 };
  tool->setGeoTransform( gt );

  // Map coordinate of the CENTRE of pixel (row 1, col 2).
  const double mapX = gt[0] + 2.5 * gt[1] + 1.5 * gt[2];
  const double mapY = gt[3] + 2.5 * gt[4] + 1.5 * gt[5];

  canvas->resize( 400, 400 );
  canvas->setExtent( QgsRectangle( 0.0, 0.0, 400.0, 400.0 ) );
  canvas->refresh();
  const QgsPointXY widgetPos =
      canvas->mapSettings().mapToPixel().transform( QgsPointXY( mapX, mapY ) );

  QMouseEvent release( QEvent::MouseButtonRelease,
                       QPointF( widgetPos.x(), widgetPos.y() ),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
  QgsMapMouseEvent mapEvent( canvas.get(), &release );
  tool->canvasReleaseEvent( &mapEvent );

  // Pre-fix the rotation terms were dropped: (mapX - 100) / 2 → column 3 and
  // (mapY - 500) / 1 → row -2, i.e. background — the click missed the segment.
  CHECK( tool->selectedSegmentId() == 7u );

  // The canvas dies first with a live highlight: no double free of the band.
  canvas.reset();
}

// ---------------------------------------------------------------------------
// APP-R-6 — an unparsable band list is refused, not silently substituted
// ---------------------------------------------------------------------------
TEST_CASE( "APP-R-6: classifier band editor refuses an unparsable band list",
           "[swarm][app][classification]" )
{
  ensureApp();

  RsClassifierSetupBar bar;
  auto *bandsEdit = bar.findChild<QLineEdit *>( QStringLiteral( "rsClassifierBands" ) );
  auto *applyButton = bar.findChild<QPushButton *>( QStringLiteral( "rsClassifierBtnApply" ) );
  REQUIRE( bandsEdit != nullptr );
  REQUIRE( applyButton != nullptr );

  QSignalSpy applySpy( &bar, &RsClassifierSetupBar::applyRequested );

  // Empty means "use the default bands" and stays valid.
  bandsEdit->setText( QString() );
  CHECK( bar.bandSelectionValid() );
  CHECK( bar.selectedBands().isEmpty() );

  bandsEdit->setText( QStringLiteral( "1,2,3" ) );
  CHECK( bar.bandSelectionValid() );
  CHECK( bar.selectedBands() == QVector<int>( { 1, 2, 3 } ) );

  // A token that does not parse used to be dropped silently, and an
  // all-invalid list fell back to the default bands. Now the run is refused.
  bandsEdit->setText( QStringLiteral( "1,x,2" ) );
  CHECK_FALSE( bar.bandSelectionValid() );
  CHECK( bar.selectedBands() == QVector<int>( { 1, 2 } ) );

  applySpy.clear();
  applyButton->click();
  CHECK( applySpy.count() == 0 );

  // Fixing the text re-arms the button and clears the invalid marker.
  bandsEdit->setText( QStringLiteral( "2,3" ) );
  CHECK( bar.bandSelectionValid() );
  CHECK( bandsEdit->styleSheet().isEmpty() );
  applyButton->click();
  CHECK( applySpy.count() == 1 );
}
