#include <catch2/catch_test_macros.hpp>

#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsgeoreftransform.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

#include <QSignalSpy>

#include <cmath>

TEST_CASE( "GCPList: appendPoint emits changed", "[georef][gcplist]" )
{
  QgsGCPList list;
  QSignalSpy spy( &list, &QgsGCPList::changed );
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  QgsGcpPoint p( QgsPointXY( 10, 20 ), QgsPointXY( 100, 200 ), crs, true );
  list.appendPoint( p );
  REQUIRE( spy.count() == 1 );
}

TEST_CASE( "GCPList: updateResiduals skips disabled points", "[georef][gcplist]" )
{
  QgsGCPList list;
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );

  // Identity-mapped enabled points.
  list.appendPoint( QgsGcpPoint( QgsPointXY( 0, 0 ),   QgsPointXY( 0, 0 ),   crs, true ) );
  list.appendPoint( QgsGcpPoint( QgsPointXY( 10, 0 ),  QgsPointXY( 10, 0 ),  crs, true ) );
  list.appendPoint( QgsGcpPoint( QgsPointXY( 0, 10 ),  QgsPointXY( 0, 10 ),  crs, true ) );
  // Disabled outlier — its residual must NOT be touched.
  list.appendPoint( QgsGcpPoint( QgsPointXY( 99, 99 ), QgsPointXY( 0, 0 ),   crs, false ) );

  // Seed the disabled point with a sentinel residual; verify updateResiduals leaves it intact.
  list.at( 3 )->setResidual( QPointF( 42.0, 42.0 ) );

  auto transform = std::make_unique<QgsGeorefTransform>(
      QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1 );
  list.updateResiduals( transform.get(), crs, crs );

  // Enabled points have ~zero residual under identity transform.
  REQUIRE( std::abs( list.at( 0 )->residual().x() ) < 1e-6 );
  REQUIRE( std::abs( list.at( 0 )->residual().y() ) < 1e-6 );

  // Disabled point: residual remained the sentinel (function did not write to it).
  REQUIRE( list.at( 3 )->residual().x() == 42.0 );
  REQUIRE( list.at( 3 )->residual().y() == 42.0 );
}
