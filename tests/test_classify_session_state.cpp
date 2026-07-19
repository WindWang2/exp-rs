#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QWidget>

#include "rs_classify_session_state.h"

using Catch::Approx;

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
    QCoreApplication::setApplicationName( QStringLiteral( "ClassifyTest" ) );
  }
}

TEST_CASE( "SessionState: dirty mark and clear", "[classify][session]" )
{
  ensureCore();
  RsClassifySessionState s;
  REQUIRE_FALSE( s.isDirty() );
  s.markDirty();
  REQUIRE( s.isDirty() );
  s.clearDirty();
  REQUIRE_FALSE( s.isDirty() );
}

TEST_CASE( "SessionState: workflow snapshot round-trip", "[classify][session]" )
{
  ensureCore();
  QSettings().clear();

  RsClassifySessionState::WorkflowSnapshot in;
  in.lastSourcePath = QStringLiteral( "/tmp/src.tif" );
  in.lastOutputPath = QStringLiteral( "/tmp/out.tif" );
  in.lastRoisPath = QStringLiteral( "/tmp/rois.shp" );
  in.lastModelPath = QStringLiteral( "/tmp/model.yml" );
  in.classifierKind = 1;
  in.trainRatio = 0.8;
  in.wandTolerance = 15.5;

  RsClassifySessionState s;
  s.saveWorkflow( in );
  s.setLastRoisPath( in.lastRoisPath );

  const auto out = s.restoreWorkflow();
  REQUIRE( out.lastSourcePath == QStringLiteral( "/tmp/src.tif" ) );
  REQUIRE( out.lastOutputPath == QStringLiteral( "/tmp/out.tif" ) );
  REQUIRE( out.lastRoisPath == QStringLiteral( "/tmp/rois.shp" ) );
  REQUIRE( out.lastModelPath == QStringLiteral( "/tmp/model.yml" ) );
  REQUIRE( out.classifierKind == 1 );
  REQUIRE( out.trainRatio == Approx( 0.8 ) );
  REQUIRE( out.wandTolerance == Approx( 15.5 ) );
  REQUIRE( s.lastRoisPath() == QStringLiteral( "/tmp/rois.shp" ) );
}

TEST_CASE( "SessionState: window geometry save/restore", "[classify][session]" )
{
  ensureCore();
  QSettings().remove( QStringLiteral( "Classification/geometry" ) );

  QWidget w;
  w.resize( 640, 480 );
  w.move( 12, 34 );
  RsClassifySessionState s;
  s.saveWindow( &w );

  QWidget w2;
  s.restoreWindow( &w2 );
  QSettings st;
  REQUIRE( st.contains( QStringLiteral( "Classification/geometry" ) ) );
}
