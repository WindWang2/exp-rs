#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

#include "rs_georef_params_panel.h"
#include "qgscoordinatereferencesystem.h"

#include <cstdlib>



namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  // Singleton QApplication — Qt forbids two in one process.
  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication app( fake_argc, fake_argv );
    }
    // QSettings() with default ctor requires org/app names to actually
    // persist to disk — set them every call (idempotent).
    QCoreApplication::setOrganizationName( QStringLiteral( "SicnuRsTest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "GeorefTest" ) );
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "CRS picker: setCrs persists across panel lifetimes", "[georef][crs]" )
{
  ensureApp();
  // Start from a clean slate — ensure no carryover from a prior test run.
  QSettings().clear();

  {
    RsGeorefParamsPanel p1;
    p1.setDestCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    REQUIRE( p1.destCrs().authid() == QStringLiteral( "EPSG:4326" ) );
  }
  {
    RsGeorefParamsPanel p2;
    REQUIRE( p2.destCrs().authid() == QStringLiteral( "EPSG:4326" ) );
  }
}

TEST_CASE( "CRS picker: destCrsChanged signal fires on setDestCrs", "[georef][crs]" )
{
  ensureApp();
  RsGeorefParamsPanel p;
  int hits = 0;
  QObject::connect( &p, &RsGeorefParamsPanel::destCrsChanged,
                    [&hits]() { ++hits; } );
  p.setDestCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ) );
  REQUIRE( hits >= 1 );
  REQUIRE( p.destCrs().authid() == QStringLiteral( "EPSG:3857" ) );
}
