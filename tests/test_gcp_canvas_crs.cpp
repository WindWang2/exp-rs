#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

using Catch::Approx;

#include <QApplication>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcpcanvasitem.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsmapcanvas.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <cmath>
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

TEST_CASE( "GCP markers: REF worldPos reprojected to canvas CRS", "[georef][canvas][crs]" )
{
  ensureApp();

  // SRC canvas: pixel-space extent (no geographic CRS).
  QgsMapCanvas src;
  src.resize( 400, 400 );
  src.mapSettings().setOutputSize( QSize( 400, 400 ) );
  src.setExtent( QgsRectangle( 0, 0, 1000, 1000 ) );

  // REF canvas in Web Mercator.
  QgsMapCanvas dst;
  dst.resize( 400, 400 );
  dst.mapSettings().setOutputSize( QSize( 400, 400 ) );
  const QgsCoordinateReferenceSystem epsg3857( QStringLiteral( "EPSG:3857" ) );
  REQUIRE( epsg3857.isValid() );
  dst.setDestinationCrs( epsg3857 );
  // Rough Beijing-area mercator extent so the canvas is sensible.
  dst.setExtent( QgsRectangle( 1.0e7, 4.0e6, 1.4e7, 5.5e6 ) );

  // GCP destination stored in geographic CRS (degrees).
  const QgsCoordinateReferenceSystem epsg4326( QStringLiteral( "EPSG:4326" ) );
  REQUIRE( epsg4326.isValid() );
  QgsGcpPoint gcp( QgsPointXY( 100, 200 ), // source pixel
                   QgsPointXY( 116, 39 ),   // dest lon/lat near Beijing
                   epsg4326, true );

  QgsGeorefDataPoint dp( &src, &dst, &gcp );
  dp.setId( 1 );
  dp.updateMarkers();

  REQUIRE( dp.sourceItem() );
  REQUIRE( dp.destinationItem() );

  // SRC stays in pixel space.
  REQUIRE( dp.sourceItem()->worldPos().x() == Catch::Approx( 100.0 ) );
  REQUIRE( dp.sourceItem()->worldPos().y() == Catch::Approx( 200.0 ) );

  // REF must be mercator meters (~1e7), not raw lon 116.
  const double destX = dp.destinationItem()->worldPos().x();
  const double destY = dp.destinationItem()->worldPos().y();
  REQUIRE( std::abs( destX ) > 1.0e6 );
  REQUIRE( std::abs( destX - 116.0 ) > 1000.0 ); // explicitly not raw lon
  // 116°E ≈ 12.9e6 m; 39°N ≈ 4.7e6 m in EPSG:3857
  REQUIRE( destX == Catch::Approx( 12914872.0 ).margin( 5.0e4 ) );
  REQUIRE( destY == Catch::Approx( 4721671.0 ).margin( 5.0e4 ) );

  // Raw destination on the GCP is still geographic.
  REQUIRE( gcp.destinationPoint().x() == Catch::Approx( 116.0 ) );
  REQUIRE( gcp.destinationPoint().y() == Catch::Approx( 39.0 ) );
}
