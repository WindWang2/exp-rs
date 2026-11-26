#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

#include "rs_georef_params_panel.h"
#include "qgscoordinatereferencesystem.h"
#include "support/qsettings_redirect.h"

#include <cstdlib>



namespace
{
  exprs_test::QSettingsUserRootRedirect settingsRedirect;
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  // Singleton QApplication — Qt forbids two in one process.
  // Heap-allocate and deliberately leak it: a static-lifetime QApplication
  // object is destroyed during exit-time static destruction, after QGIS/Qt
  // global resources it depends on are already gone (segfault at shutdown,
  // same idiom as sicnu_geo_rs main.cpp).
  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication *app = new QApplication( fake_argc, fake_argv );
      Q_UNUSED( app );
    }
    // CTest runs these cases in parallel processes. Both panel instances in a
    // case must see the same settings without sharing a file with other cases.
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QCoreApplication::setOrganizationName( QStringLiteral( "SicnuRsTest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "CrsPickerPersistsTest" ) );
    QSettings().clear();
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "CRS picker: setCrs persists across panel lifetimes", "[georef][crs]" )
{
  ensureApp();
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
