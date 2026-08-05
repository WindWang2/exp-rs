// tests/test_active_view_host_viewport.cpp — Catch2 unit tests for ActiveViewHost viewport methods (ADR 0036)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/active_view_host.h"

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsrectangle.h>
#include <qgspointxy.h>
#include <qgsproject.h>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "ActiveViewHost viewport methods degrade gracefully headlessly", "[app][active_view_host][viewport][headless]" )
{
  ActiveViewHost host( nullptr, nullptr, nullptr, nullptr, nullptr, sicnu::display::DisplayViewId(), nullptr );

  SECTION( "Extent and scale return default fallbacks" )
  {
    CHECK( host.mapCanvasExtent().isEmpty() );
    CHECK( host.mapCanvasScale() == 1.0 );
    CHECK( host.mapCanvasCrsAuthId().isEmpty() );
  }

  SECTION( "Viewport mutations execute as safe no-ops headlessly" )
  {
    const QgsRectangle extent( 10.0, 20.0, 30.0, 40.0 );
    const QgsPointXY center( 20.0, 30.0 );

    REQUIRE_NOTHROW( host.setExtent( extent ) );
    REQUIRE_NOTHROW( host.setCenter( center ) );
    REQUIRE_NOTHROW( host.zoomToFullExtent() );
    REQUIRE_NOTHROW( host.refreshCanvas() );
  }
}

TEST_CASE( "ActiveViewHost viewport methods manipulate QgsMapCanvas", "[app][active_view_host][viewport]" )
{
  QgsMapCanvas canvas;
  ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr, sicnu::display::DisplayViewId(), nullptr );

  SECTION( "setExtent updates map canvas extent" )
  {
    const QgsRectangle rect( 100.0, 200.0, 300.0, 400.0 );
    host.setExtent( rect );
    CHECK_FALSE( host.mapCanvasExtent().isEmpty() );
  }

  SECTION( "setCenter updates map canvas center" )
  {
    host.setExtent( QgsRectangle( 0.0, 0.0, 100.0, 100.0 ) );
    const QgsPointXY center( 150.0, 250.0 );
    host.setCenter( center );
    CHECK( canvas.center().x() == 150.0 );
    CHECK( canvas.center().y() == 250.0 );
  }

  SECTION( "viewportSnapshot captures extent and scale" )
  {
    const QgsRectangle rect( 100.0, 200.0, 300.0, 400.0 );
    host.setExtent( rect );
    const auto snap = host.viewportSnapshot();
    CHECK_FALSE( snap.extent.isEmpty() );
    CHECK( snap.scale == host.mapCanvasScale() );
  }
}
