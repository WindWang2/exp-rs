#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QWidget>

#include "rs_georeferencing_session.h"

namespace {
  int argc = 1;
  char arg0[] = "test";
  char *argv[] = { arg0, nullptr };
  // QWidget requires QApplication (not QCoreApplication alone).
  void ensureCore()
  {
    if ( !QCoreApplication::instance() )
      static QApplication app( argc, argv );
    QCoreApplication::setOrganizationName( QStringLiteral( "SicnuRsTest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "GeorefTest" ) );
  }
}

TEST_CASE( "RsGeoreferencingSession: dirty mark and clear", "[georef][session]" )
{
  ensureCore();
  RsGeoreferencingSession s;
  REQUIRE_FALSE( s.isDirty() );
  s.markDirty();
  REQUIRE( s.isDirty() );
  s.clearDirty();
  REQUIRE_FALSE( s.isDirty() );
}

TEST_CASE( "RsGeoreferencingSession: workflow snapshot round-trip", "[georef][session]" )
{
  ensureCore();
  QSettings().clear();

  RsGeoreferencingSession::WorkflowSnapshot in;
  in.mode = 1;
  in.transformMethod = 2;
  in.resamplingMethod = 1;
  in.lastSourcePath = QStringLiteral( "/tmp/src.tif" );
  in.lastRefPath = QStringLiteral( "/tmp/ref.tif" );
  in.lastOutputPath = QStringLiteral( "/tmp/out.tif" );
  in.lastDemPath = QStringLiteral( "/tmp/dem.tif" );
  in.lastPointsPath = QStringLiteral( "/tmp/a.points" );
  in.lastDestCrsAuthId = QStringLiteral( "EPSG:32650" );
  in.demZOffset = 12.5;
  in.syncZoom = false;

  RsGeoreferencingSession s;
  s.saveWorkflow( in );
  s.setLastPointsPath( in.lastPointsPath );

  const auto out = s.restoreWorkflow();
  REQUIRE( out.mode == 1 );
  REQUIRE( out.transformMethod == 2 );
  REQUIRE( out.resamplingMethod == 1 );
  REQUIRE( out.lastSourcePath == QStringLiteral( "/tmp/src.tif" ) );
  REQUIRE( out.lastRefPath == QStringLiteral( "/tmp/ref.tif" ) );
  REQUIRE( out.lastOutputPath == QStringLiteral( "/tmp/out.tif" ) );
  REQUIRE( out.lastDemPath == QStringLiteral( "/tmp/dem.tif" ) );
  REQUIRE( out.lastPointsPath == QStringLiteral( "/tmp/a.points" ) );
  REQUIRE( out.lastDestCrsAuthId == QStringLiteral( "EPSG:32650" ) );
  REQUIRE( out.demZOffset == 12.5 );
  REQUIRE( out.syncZoom == false );
  REQUIRE( s.lastPointsPath() == QStringLiteral( "/tmp/a.points" ) );
}

TEST_CASE( "RsGeoreferencingSession: window geometry save/restore", "[georef][session]" )
{
  ensureCore();
  QSettings().remove( QStringLiteral( "Georeferencer/geometry" ) );

  QWidget w;
  w.resize( 640, 480 );
  w.move( 12, 34 );
  RsGeoreferencingSession s;
  s.saveWindow( &w );

  QWidget w2;
  s.restoreWindow( &w2 );
  QSettings st;
  REQUIRE( st.contains( QStringLiteral( "Georeferencer/geometry" ) ) );
}
