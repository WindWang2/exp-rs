// test_workbench_state_model.cpp — Workbench 9.0 M1 explicit state model
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTimer>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <QItemSelectionModel>
#include <qgsmapcanvas.h>
#include <qgsmaptoolpan.h>
#include <qgsmaptoolzoom.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/workbench/workbench_state.h"
#include "app/workbench/selection_context.h"

using namespace sicnu::app;

namespace
{

/// Spin the event loop until `done` turns true or the bound is hit — the
/// model coalesces refreshes onto a queued singleShot, so tests must turn
/// the loop instead of sleeping on wall-clock guesses.
void pumpUntil( const std::function<bool()> &done, int maxTurns = 200 )
{
  for ( int i = 0; i < maxTurns && !done(); ++i )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents );
    QCoreApplication::sendPostedEvents( nullptr );
  }
}

} // namespace

TEST_CASE( "WorkbenchRules: pure projections", "[m1][state][rules]" )
{
  SECTION( "canvas stack page" )
  {
    REQUIRE( WorkbenchRules::canvasStackPage( ProjectPhase::Empty ) == 0 );
    REQUIRE( WorkbenchRules::canvasStackPage( ProjectPhase::Populated ) == 1 );
  }

  SECTION( "import is the primary action only when empty and idle" )
  {
    WorkbenchFacts empty;
    REQUIRE( WorkbenchRules::importIsPrimaryAction( empty ) );

    WorkbenchFacts busy = empty;
    busy.taskInFlight = true;
    REQUIRE_FALSE( WorkbenchRules::importIsPrimaryAction( busy ) );

    WorkbenchFacts populated;
    populated.phase = ProjectPhase::Populated;
    REQUIRE_FALSE( WorkbenchRules::importIsPrimaryAction( populated ) );
  }

  SECTION( "phase summary is human-readable in both phases" )
  {
    WorkbenchFacts facts;
    REQUIRE_FALSE( WorkbenchRules::phaseSummary( facts ).isEmpty() );
    facts.phase = ProjectPhase::Populated;
    facts.layerCount = 3;
    REQUIRE( WorkbenchRules::phaseSummary( facts ).contains( QStringLiteral( "3" ) ) );
  }
}

TEST_CASE( "WorkbenchStateModel: phase follows canvas layers",
           "[m1][state][model]" )
{
  QgsMapCanvas canvas;
  WorkbenchStateModel model( &canvas );

  REQUIRE( model.phase() == ProjectPhase::Empty );
  REQUIRE( model.facts().layerCount == 0 );
  REQUIRE( model.facts().toolMode == QStringLiteral( "pan" ) );

  WorkbenchFacts lastFacts = model.facts();
  int phaseChanges = 0;
  QObject::connect( &model, &WorkbenchStateModel::factsChanged,
                    [&]( const WorkbenchFacts &f ) { lastFacts = f; } );
  QObject::connect( &model, &WorkbenchStateModel::phaseChanged,
                    [&]( ProjectPhase ) { ++phaseChanges; } );

  QgsRasterLayer layer( QStringLiteral( "/nonexistent/rs9/state.tif" ),
                        QStringLiteral( "state" ), QStringLiteral( "gdal" ) );
  canvas.setLayers( { &layer } );
  pumpUntil( [this_ = &model] { return this_->phase() == ProjectPhase::Populated; } );

  REQUIRE( model.phase() == ProjectPhase::Populated );
  REQUIRE( model.facts().layerCount == 1 );
  REQUIRE( lastFacts.phase == ProjectPhase::Populated );
  REQUIRE( phaseChanges >= 1 );

  canvas.setLayers( {} );
  pumpUntil( [this_ = &model] { return this_->phase() == ProjectPhase::Empty; } );
  REQUIRE( model.phase() == ProjectPhase::Empty );
}

TEST_CASE( "WorkbenchStateModel: tool mode tracks the active map tool",
           "[m1][state][model]" )
{
  QgsMapCanvas canvas;
  WorkbenchStateModel model( &canvas );

  REQUIRE( model.facts().toolMode == QStringLiteral( "pan" ) );

  // Non-pan tool: the mode id becomes the tool's class name — this is the
  // anti-tautology arm (review B3): deleting the mapToolSet wiring makes
  // this test fail, because "QgsMapToolZoom" can never appear without it.
  QgsMapToolZoom zoom( &canvas, /*zoomOut=*/true );
  int toolChanges = 0;
  QObject::connect( &model, &WorkbenchStateModel::toolModeChanged,
                    [&]( const QString & ) { ++toolChanges; } );

  canvas.setMapTool( &zoom );
  pumpUntil( [this_ = &model] { return this_->facts().toolMode.contains( QStringLiteral( "Zoom" ) ); } );
  REQUIRE( model.facts().toolMode == QStringLiteral( "QgsMapToolZoom" ) );
  REQUIRE( toolChanges >= 1 );

  // Back to pan via the actual pan tool instance (class normalized).
  QgsMapToolPan pan( &canvas );
  canvas.setMapTool( &pan );
  pumpUntil( [this_ = &model] { return this_->facts().toolMode == QStringLiteral( "pan" ); } );
  REQUIRE( model.facts().toolMode == QStringLiteral( "pan" ) );

  canvas.unsetMapTool( &pan );
  pumpUntil( [this_ = &model] { return this_->facts().toolMode == QStringLiteral( "pan" ); } );
  // Unset falls back to the idle default.
  REQUIRE( model.facts().toolMode == QStringLiteral( "pan" ) );
}

TEST_CASE( "WorkbenchStateModel: in-flight predicate feeds taskInFlight",
           "[m1][state][model]" )
{
  QgsMapCanvas canvas;
  WorkbenchStateModel model( &canvas );

  bool inFlight = false;
  model.setInFlightTaskPredicate( [&] { return inFlight; } );
  pumpUntil( [&] { return model.facts().taskInFlight == false; } );
  REQUIRE_FALSE( model.facts().taskInFlight );

  WorkbenchFacts during;
  QObject::connect( &model, &WorkbenchStateModel::factsChanged,
                    [&]( const WorkbenchFacts &f ) { during = f; } );
  inFlight = true;
  model.refresh();
  pumpUntil( [&] { return model.facts().taskInFlight; } );
  REQUIRE( model.facts().taskInFlight );
  REQUIRE( during.taskInFlight );
}

TEST_CASE( "WorkbenchStateModel: broken-layer fact follows SelectionContext",
           "[m1][state][model][broken]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();
  QgsMapCanvas canvas;
  // Declaration order matters: the model must outlive the view.
  QgsLayerTreeModel treeModel( project->layerTreeRoot() );
  QgsLayerTreeView tree;
  tree.setLayerTreeModel( &treeModel );
  tree.setModel( &treeModel );

  WorkbenchStateModel model( &canvas );
  sicnu::app::SelectionContext context;
  context.attachCanvas( &canvas );
  context.attachLayerTree( &tree );
  model.attachSelectionContext( &context );

  REQUIRE_FALSE( model.facts().hasBrokenLayer );

  // A broken (missing-source) layer staged on the canvas must surface in the
  // model facts — the M1 contract that state consumers never re-derive it.
  // hasBroken is a selection fact (ContextRules contract): stage the layer in
  // the project/tree and make it current, as a user clicking it would.
  QgsRasterLayer broken( QStringLiteral( "/nonexistent/rs9/broken_state.tif" ),
                         QStringLiteral( "broken" ), QStringLiteral( "gdal" ) );
  project->addMapLayer( &broken, false, false ); // no ownership — stack object
  project->layerTreeRoot()->addLayer( &broken );
  canvas.setLayers( { &broken } );
  canvas.setCurrentLayer( &broken );
  // Selection is read from the layer tree's selection model — select the
  // broken layer's node the same way the shell's tree view does.
  if ( QgsLayerTreeLayer *node = project->layerTreeRoot()->findLayer( broken.id() ) )
  {
    const QModelIndex idx = treeModel.node2index( node );
    tree.selectionModel()->select( idx, QItemSelectionModel::Select | QItemSelectionModel::Rows );
  }
  pumpUntil( [this_ = &model] { return this_->facts().layerCount == 1; } );
  REQUIRE( model.facts().layerCount == 1 );
  pumpUntil( [&] { return model.facts().hasBrokenLayer; } );
  REQUIRE( model.facts().hasBrokenLayer );
  project->clear();
}

TEST_CASE( "WorkbenchStateModel: null canvas degrades to fact defaults",
           "[m1][state][model]" )
{
  WorkbenchStateModel model( nullptr );
  REQUIRE( model.phase() == ProjectPhase::Empty );
  REQUIRE( model.facts().layerCount == 0 );
  model.refresh();
  pumpUntil( [] { return false; } );
  REQUIRE( model.facts() == WorkbenchFacts{} );
}

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, false );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsApplication::exitQgis();
  return result;
}
