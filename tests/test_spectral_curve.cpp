#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QVector>

#include "rs_spectral_curve_widget.h"

#include <cstdlib>

// Fast-exit shim — same pattern as test_rms_scatter.cpp. Bypasses Qt/glibc
// teardown crashes that have nothing to do with the assertions under test.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication app( fake_argc, fake_argv );
      return &app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "SpectralCurveWidget: empty paint doesn't crash", "[classify][spectra]" )
{
  ensureApp();

  RsSpectralCurveWidget w;
  w.resize( 400, 180 );

  QImage img( 400, 180, QImage::Format_ARGB32 );
  img.fill( Qt::white );
  w.render( &img );
  SUCCEED();
}

TEST_CASE( "SpectralCurveWidget: setClassCurves redraws", "[classify][spectra]" )
{
  ensureApp();

  RsSpectralCurveWidget w;
  w.resize( 400, 180 );

  QImage img1( 400, 180, QImage::Format_ARGB32 );
  img1.fill( Qt::white );
  w.render( &img1 );

  QVector<RsSpectralCurveWidget::Curve> curves;
  RsSpectralCurveWidget::Curve c;
  c.classId = 1;
  c.color = QColor( "#2da44e" );
  c.name = QStringLiteral( "Forest" );
  c.bandMeans = { 120.0, 95.0, 180.0, 60.0 };
  c.bandStds = { 12.0, 8.0, 15.0, 5.0 };
  curves.append( c );
  w.setClassCurves( curves );

  QImage img2( 400, 180, QImage::Format_ARGB32 );
  img2.fill( Qt::white );
  w.render( &img2 );

  // The render must change after curves are pushed in; this verifies both
  // that paintEvent ran for the curve and that empty input didn't crash.
  REQUIRE( img1 != img2 );
}
