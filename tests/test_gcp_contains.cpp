#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsmapcanvas.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <cstdlib>

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup.
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

TEST_CASE( "GCP contains: hit inside search radius on SRC", "[georef][contains]" )
{
  ensureApp();
  QgsMapCanvas src;
  src.resize( 400, 400 );
  src.mapSettings().setOutputSize( QSize( 400, 400 ) );
  src.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsMapCanvas dst;
  dst.resize( 400, 400 );
  dst.mapSettings().setOutputSize( QSize( 400, 400 ) );
  dst.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsGcpPoint gcp( QgsPointXY( 50, 50 ), QgsPointXY( 10, 10 ),
                   QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), true );
  QgsGeorefDataPoint dp( &src, &dst, &gcp );
  dp.setId( 1 );
  dp.updateMarkers();

  REQUIRE( dp.sourceItem() );

  double dist = -1;
  REQUIRE( dp.contains( QgsPointXY( 50, 50 ), QgsGcpPoint::PointType::Source, dist ) );
  REQUIRE( dist >= 0.0 );
  REQUIRE( dist < 1.0 );

  REQUIRE_FALSE( dp.contains( QgsPointXY( 0, 0 ), QgsGcpPoint::PointType::Source, dist ) );
}

TEST_CASE( "GCP contains: hit inside search radius on DEST", "[georef][contains]" )
{
  ensureApp();
  QgsMapCanvas src;
  src.resize( 400, 400 );
  src.mapSettings().setOutputSize( QSize( 400, 400 ) );
  src.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsMapCanvas dst;
  dst.resize( 400, 400 );
  dst.mapSettings().setOutputSize( QSize( 400, 400 ) );
  dst.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsGcpPoint gcp( QgsPointXY( 50, 50 ), QgsPointXY( 40, 40 ),
                   QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), true );
  QgsGeorefDataPoint dp( &src, &dst, &gcp );
  dp.setId( 2 );
  dp.updateMarkers();

  REQUIRE( dp.destinationItem() );

  double dist = -1;
  REQUIRE( dp.contains( QgsPointXY( 40, 40 ), QgsGcpPoint::PointType::Destination, dist ) );
  REQUIRE( dist >= 0.0 );
  REQUIRE( dist < 1.0 );

  REQUIRE_FALSE( dp.contains( QgsPointXY( 0, 0 ), QgsGcpPoint::PointType::Destination, dist ) );
}
