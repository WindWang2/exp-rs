#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QVector>

#include "rs_rms_scatter_widget.h"

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "RmsScatter: paints without crashing on empty + 7 points", "[georef][scatter]" )
{
  ensureApp();

  RsRmsScatterWidget w;
  w.resize( 150, 150 );

  QImage img1( 150, 150, QImage::Format_ARGB32 );
  img1.fill( Qt::transparent );
  w.render( &img1 );

  w.setResiduals( {
    { 0.1, 0.1 },
    { 0.4, -0.3 },
    { 1.2, 0.5 },
    { -0.2, 0.6 },
    { 0.8, -0.8 },
    { 0.0, 0.0 },
    { 1.5, -1.4 },
  } );

  QImage img2( 150, 150, QImage::Format_ARGB32 );
  img2.fill( Qt::transparent );
  w.render( &img2 );

  // The render must change after data is pushed in; this verifies both that
  // paintEvent ran for the points and that it didn't crash on empty input.
  REQUIRE( img1 != img2 );
}
