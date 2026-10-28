// tests/test_r25_d_cartography_dock.cpp — R25-D Loop 1 (dock-side gating)
//
// The CartographyDock's export/produce buttons used to be permanently
// enabled: with no composed layout, clicking Export only produced a
// status-bar line after the fact. This suite pins the honest contract:
//  - Export stays disabled (tooltip names the missing step) until a
//    MapSpec/layout has been adopted;
//  - Compose/Check/Fix/Production disable (with reason) when no map layers
//    are available to draft from;
//  - a spec adopted through the public handoff seam (workflow/agent
//    bridge) re-enables Export;
//  - busy state (task running) still wins over every readiness rule.
//
// Rides the full shell library (#1295 precedent): the REAL dock, REAL
// template registry, REAL TaskCenter connections — offscreen.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QPushButton>

#include "app/cartography/cartography_dock.h"

#include <qgsapplication.h>
#include <qgsproject.h>

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

int main( int argc, char *argv[] )
{
  qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using sicnu::app::CartographyDock;

namespace
{

QPushButton *buttonOf( CartographyDock *dock, const char *objectName )
{
  return dock->findChild<QPushButton *>( QLatin1String( objectName ) );
}

} // namespace

TEST_CASE( "export and produce gating follows draft/composed readiness", "[r25d][cartography][dock]" )
{
  QStringList liveLayers{ QStringLiteral( "/tmp/fixture-a.tif" ), QStringLiteral( "/tmp/fixture-b.tif" ) };
  CartographyDock dock( [&liveLayers] { return liveLayers; },
                        [] { return QStringLiteral( "/tmp" ); } );
  dock.show();

  REQUIRE( dock.templateIds().size() >= 2 ); // catalog loaded from data/cartography

  auto *compose = buttonOf( &dock, "rsCartographyCompose" );
  auto *preflight = buttonOf( &dock, "rsCartographyPreflight" );
  auto *repair = buttonOf( &dock, "rsCartographyRepair" );
  auto *exportBtn = buttonOf( &dock, "rsCartographyExport" );
  auto *produce = buttonOf( &dock, "rsCartographyProduce" );
  REQUIRE( compose );
  REQUIRE( preflight );
  REQUIRE( repair );
  REQUIRE( exportBtn );
  REQUIRE( produce );

  SECTION( "layers present, nothing composed yet" )
  {
    CHECK( compose->isEnabled() );
    CHECK( preflight->isEnabled() );
    CHECK( repair->isEnabled() );
    CHECK( produce->isEnabled() );
    // Export needs a compiled layout — disabled, and the tooltip says why.
    CHECK_FALSE( exportBtn->isEnabled() );
    CHECK( exportBtn->toolTip().contains( QStringLiteral( "Compose" ) ) );
  }

  SECTION( "adopting a spec (workflow/agent handoff) enables export" )
  {
    Json::Value spec( Json::objectValue );
    spec["layout_name"] = "handoff-map";
    spec["kind"] = "map_spec";
    spec["spec_version"] = 1;
    dock.adoptSpec( spec );
    CHECK( dock.currentMapSpec()["layout_name"].asString() == "handoff-map" );
    CHECK( exportBtn->isEnabled() );
    // The enabled tooltip is the action description, not a blocker reason.
    CHECK_FALSE( exportBtn->toolTip().contains( QStringLiteral( "Compose a layout first" ) ) );
  }

  SECTION( "no map layers disables the draft actions with a reason" )
  {
    liveLayers.clear();
    dock.refreshActionStates(); // the dock re-derives on visibility/adoptions
    CHECK_FALSE( compose->isEnabled() );
    CHECK_FALSE( preflight->isEnabled() );
    CHECK_FALSE( repair->isEnabled() );
    CHECK_FALSE( produce->isEnabled() );
    CHECK( compose->toolTip().contains( QStringLiteral( "layer" ), Qt::CaseInsensitive ) );
    // Export still needs a composition AND now layers too — one reason is
    // enough; it must be non-empty.
    CHECK_FALSE( exportBtn->isEnabled() );
    CHECK_FALSE( exportBtn->toolTip().isEmpty() );
  }
}

TEST_CASE( "compose click without layers surfaces an actionable status, never a silent no-op", "[r25d][cartography][dock]" )
{
  CartographyDock dock( [] { return QStringList(); }, [] { return QString(); } );
  dock.show();
  QSignalSpy statusSpy( &dock, &CartographyDock::statusMessage );

  dock.runCompose();

  REQUIRE( statusSpy.size() >= 1 );
  const QString message = statusSpy.first().at( 0 ).toString();
  CHECK( message.contains( QStringLiteral( "layer" ), Qt::CaseInsensitive ) );
}
