// tests/test_measure_tool.cpp
#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include "app/map_tools/measure_tool.h"

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <memory>

struct TestAppFixture {
  TestAppFixture() {
    if ( !QCoreApplication::instance() ) {
      static int argc = 1;
      static char appName[] = "test_measure_tool";
      static char *argv[] = { appName, nullptr };
      s_app = new QApplication( argc, argv );
      QgsApplication::initQgis();
    }
  }

  static QApplication *s_app;
};
QApplication *TestAppFixture::s_app = nullptr;

TEST_CASE( "MeasureTool dynamically synchronizes CRS and ellipsoid (#318)", "[app][map_tools][measure]" )
{
  TestAppFixture fixture;

  QgsProject *project = QgsProject::instance();
  project->setCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  project->setEllipsoid( "WGS84" );

  auto canvas = std::make_unique<QgsMapCanvas>();
  canvas->setDestinationCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );

  MeasureTool tool( canvas.get(), MeasureTool::Distance );

  SECTION( "initial distance area reflects startup CRS" )
  {
    REQUIRE( tool.distanceArea().sourceCrs().authid() == "EPSG:4326" );
    REQUIRE( tool.distanceArea().ellipsoid() == "WGS84" );
  }

  SECTION( "canvas destination CRS change updates distance area" )
  {
    canvas->setDestinationCrs( QgsCoordinateReferenceSystem( "EPSG:32650" ) );
    REQUIRE( tool.distanceArea().sourceCrs().authid() == "EPSG:32650" );
  }

  SECTION( "project CRS and ellipsoid change updates distance area" )
  {
    project->setCrs( QgsCoordinateReferenceSystem( "EPSG:3857" ) );
    project->setEllipsoid( "GRS80" );
    canvas->setDestinationCrs( QgsCoordinateReferenceSystem( "EPSG:3857" ) );

    REQUIRE( tool.distanceArea().sourceCrs().authid() == "EPSG:3857" );
    REQUIRE( tool.distanceArea().ellipsoid() == "GRS80" );
  }

  SECTION( "activate() refreshes distance area" )
  {
    canvas->setDestinationCrs( QgsCoordinateReferenceSystem( "EPSG:32649" ) );
    tool.activate();
    REQUIRE( tool.distanceArea().sourceCrs().authid() == "EPSG:32649" );
  }
}
