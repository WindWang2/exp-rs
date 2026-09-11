// test_workbench_state_model.cpp — Workbench 9.0 M1 explicit state model
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTimer>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsmaptoolpan.h>

#include "app/workbench/workbench_state.h"

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
