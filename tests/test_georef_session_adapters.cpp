// test_georef_session_adapters.cpp — #33 I2I / I2M adapters over Georeferencing Session
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "qgsgeoreferencermainwindow.h"
#include "qgsgeoref_image_to_map_window.h"
#include "rs_georeferencing_session.h"

#include <QApplication>

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
} // namespace
CATCH_REGISTER_LISTENER( FastExitListener )

static int argc = 1;
static char arg0[] = "test_georef_session_adapters";
static char *argv[] = { arg0, nullptr };
static QApplication *app = []() {
  static QApplication a( argc, argv );
  return &a;
}();

TEST_CASE( "I2M keeps map-coordinate destination picking; I2I does not",
           "[georef][session][adapter]" )
{
  QgsGeoreferencerMainWindow i2i( nullptr, nullptr );
  QgsGeorefImageToMapWindow i2m( nullptr, nullptr );

  REQUIRE_FALSE( i2i.usesMapCoordsDialogForGcp() );
  REQUIRE( i2m.usesMapCoordsDialogForGcp() );
}

TEST_CASE( "I2I and I2M each own an independent Georeferencing Session",
           "[georef][session][adapter]" )
{
  QgsGeoreferencerMainWindow i2i( nullptr, nullptr );
  QgsGeorefImageToMapWindow i2m( nullptr, nullptr );

  auto *sI2i = i2i.georefSessionForTest();
  auto *sI2m = i2m.georefSessionForTest();
  REQUIRE( sI2i != nullptr );
  REQUIRE( sI2m != nullptr );
  REQUIRE( sI2i != sI2m );

  // Mutating one session must not change the other.
  sI2i->setSourceRasterPath( QStringLiteral( "/tmp/i2i_src.tif" ) );
  sI2m->setSourceRasterPath( QStringLiteral( "/tmp/i2m_src.tif" ) );
  REQUIRE( sI2i->sourceRasterPath() == QStringLiteral( "/tmp/i2i_src.tif" ) );
  REQUIRE( sI2m->sourceRasterPath() == QStringLiteral( "/tmp/i2m_src.tif" ) );

  QVector<QgsGcpPoint> gcps;
  gcps.append( QgsGcpPoint( QgsPointXY( 0, 0 ), QgsPointXY( 10, 20 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 1, 0 ), QgsPointXY( 11, 20 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 0, 1 ), QgsPointXY( 10, 21 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 1, 1 ), QgsPointXY( 11, 21 ), QgsCoordinateReferenceSystem(), true ) );

  sI2i->setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  sI2i->setGcps( gcps );
  REQUIRE( sI2i->refit().ready );
  REQUIRE( sI2i->gcps().size() == 4 );

  // I2M session still empty / not fit.
  REQUIRE( sI2m->gcps().isEmpty() );
  REQUIRE_FALSE( sI2m->isFitReady() );

  // Clearing I2I does not touch I2M after I2M gets its own GCPs.
  sI2m->setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  sI2m->setGcps( gcps );
  REQUIRE( sI2m->refit().ready );

  sI2i->clearGcps();
  REQUIRE( sI2i->gcps().isEmpty() );
  REQUIRE_FALSE( sI2i->isFitReady() );
  REQUIRE( sI2m->gcps().size() == 4 );
  REQUIRE( sI2m->isFitReady() );
}

TEST_CASE( "I2M and I2I both expose Task Center warp via the shared session type",
           "[georef][session][adapter]" )
{
  QgsGeoreferencerMainWindow i2i( nullptr, nullptr );
  QgsGeorefImageToMapWindow i2m( nullptr, nullptr );

  // Both adapters share the same session API (snapshot + startWarpTask on shell).
  auto *sI2i = i2i.georefSessionForTest();
  auto *sI2m = i2m.georefSessionForTest();
  REQUIRE( sI2i != nullptr );
  REQUIRE( sI2m != nullptr );

  QVector<QgsGcpPoint> gcps;
  gcps.append( QgsGcpPoint( QgsPointXY( 0, 0 ), QgsPointXY( 100, 200 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 10, 0 ), QgsPointXY( 110, 200 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 0, 10 ), QgsPointXY( 100, 210 ), QgsCoordinateReferenceSystem(), true ) );
  gcps.append( QgsGcpPoint( QgsPointXY( 10, 10 ), QgsPointXY( 110, 210 ), QgsCoordinateReferenceSystem(), true ) );

  sI2i->setSourceRasterPath( QStringLiteral( "/tmp/i2i.tif" ) );
  sI2i->setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  sI2i->setGcps( gcps );
  REQUIRE( sI2i->refit().ready );
  const auto snapI2i = sI2i->createWarpSnapshot(
    QStringLiteral( "/tmp/i2i_out.tif" ),
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem(), 0.0 );
  REQUIRE( snapI2i.has_value() );

  sI2m->setSourceRasterPath( QStringLiteral( "/tmp/i2m.tif" ) );
  sI2m->setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  sI2m->setGcps( gcps );
  REQUIRE( sI2m->refit().ready );
  const auto snapI2m = sI2m->createWarpSnapshot(
    QStringLiteral( "/tmp/i2m_out.tif" ),
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem(), 0.0 );
  REQUIRE( snapI2m.has_value() );

  // Snapshots are independent (different source/output paths).
  REQUIRE( snapI2i->sourcePath != snapI2m->sourcePath );
  REQUIRE( snapI2i->outputPath != snapI2m->outputPath );
}
