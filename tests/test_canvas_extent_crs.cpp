// test_canvas_extent_crs.cpp — fail-closed canvas extent reprojection
// (#1005 / #1037). Locks the two QGIS behaviours that make a plain try/catch
// insufficient:
//   - transformBoundingBox THROWS for geocentric CRS pairs (EPSG:4978);
//   - a valid-but-unbuildable pair (horizontal -> vertical, EPSG:5703) has
//     ct.isValid() == false and would silently return the input.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/shell/canvas_extent_crs.h"

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsproject.h>
#include <qgsrectangle.h>

using Catch::Approx;

namespace
{
// CRS registry / PROJ need one QgsApplication per process (same pattern as the
// other georef CRS tests).
struct QgisEnv
{
  QgisEnv()
  {
    static int argc = 1;
    static char arg0[] = "test_canvas_extent_crs";
    static char *argv[] = { arg0 };
    app = new QgsApplication( argc, argv, false );
    QgsApplication::initQgis();
  }
  QgsApplication *app;
};
} // namespace

static QgisEnv &qgisEnv()
{
  static QgisEnv env;
  return env;
}

TEST_CASE( "Canvas extent CRS: same CRS is the exact identity", "[app][crs][failclosed][1005]" )
{
  qgisEnv();
  const auto crs = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32633" ) );
  const QgsRectangle in( 100, 200, 300, 400 );
  const auto out = sicnu::app::rsTransformExtentForCanvas(
    in, crs, crs, QgsCoordinateTransformContext() );
  REQUIRE( out.has_value() );
  CHECK( out->xMinimum() == Approx( in.xMinimum() ) );
  CHECK( out->xMaximum() == Approx( in.xMaximum() ) );
  CHECK( out->yMinimum() == Approx( in.yMinimum() ) );
  CHECK( out->yMaximum() == Approx( in.yMaximum() ) );
}

TEST_CASE( "Canvas extent CRS: invalid CRS passes through (unreferenced workflow)",
           "[app][crs][failclosed][1005]" )
{
  qgisEnv();
  const auto valid = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) );
  const QgsCoordinateReferenceSystem invalid;
  const QgsRectangle in( 0, 0, 1, 1 );

  const auto invalidLayer = sicnu::app::rsTransformExtentForCanvas(
    in, invalid, valid, QgsCoordinateTransformContext() );
  REQUIRE( invalidLayer.has_value() );
  CHECK( invalidLayer->xMaximum() == Approx( 1.0 ) );

  const auto invalidCanvas = sicnu::app::rsTransformExtentForCanvas(
    in, valid, invalid, QgsCoordinateTransformContext() );
  REQUIRE( invalidCanvas.has_value() );
  CHECK( invalidCanvas->xMaximum() == Approx( 1.0 ) );
}

TEST_CASE( "Canvas extent CRS: WGS84 -> UTM33N transforms", "[app][crs][failclosed][1005]" )
{
  qgisEnv();
  const auto wgs = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) );
  const auto utm = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32633" ) );
  const auto out = sicnu::app::rsTransformExtentForCanvas(
    QgsRectangle( 16.0, 48.0, 17.0, 49.0 ), wgs, utm, QgsCoordinateTransformContext() );
  REQUIRE( out.has_value() );
  CHECK( out->xMinimum() > 300000.0 );
  CHECK( out->xMaximum() < 800000.0 );
  CHECK( out->yMinimum() > 5000000.0 );
  CHECK( out->yMaximum() < 5700000.0 );
}

TEST_CASE( "Canvas extent CRS: geocentric target throws -> std::nullopt (fail closed)",
           "[app][crs][failclosed][1005]" )
{
  qgisEnv();
  const auto wgs = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) );
  const auto geocentric = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4978" ) );
  REQUIRE( wgs.isValid() );
  REQUIRE( geocentric.isValid() );

  // Precondition: the bbox transform itself throws — lock it so a PROJ/QGIS
  // change cannot turn this regression into a silent pass.
  {
    const QgsCoordinateTransform probe( wgs, geocentric, QgsProject::instance() );
    REQUIRE_THROWS( probe.transformBoundingBox( QgsRectangle( 0, 0, 1, 1 ) ) );
  }

  const auto out = sicnu::app::rsTransformExtentForCanvas(
    QgsRectangle( 0, 0, 1, 1 ), wgs, geocentric, QgsCoordinateTransformContext() );
  CHECK_FALSE( out.has_value() );
}

TEST_CASE( "Canvas extent CRS: unbuildable operation -> std::nullopt (fail closed)",
           "[app][crs][failclosed][1037]" )
{
  qgisEnv();
  const auto wgs = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) );
  const auto vertical = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:5703" ) );
  REQUIRE( wgs.isValid() );
  REQUIRE( vertical.isValid() );

  // Precondition: the transform is VALID as a pair but cannot be built, so
  // transformBoundingBox would return the input instead of throwing.
  {
    const QgsCoordinateTransform probe( wgs, vertical, QgsProject::instance() );
    REQUIRE_FALSE( probe.isValid() );
  }

  const QgsRectangle in( 10, 10, 20, 20 );
  const auto out = sicnu::app::rsTransformExtentForCanvas(
    in, wgs, vertical, QgsCoordinateTransformContext() );
  CHECK_FALSE( out.has_value() );
}
