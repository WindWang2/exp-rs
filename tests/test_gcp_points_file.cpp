#include <catch2/catch_test_macros.hpp>

#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

TEST_CASE( "GCP .points v2: write+read round-trip preserves type", "[georef][points]" )
{
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  QVector<QgsGcpPoint> points;
  {
    QgsGcpPoint p( QgsPointXY( 100, 200 ), QgsPointXY( 400000, 4280000 ), crs, true );
    p.setPointType( "road" );
    points.append( p );
  }
  {
    QgsGcpPoint p( QgsPointXY( 300, 400 ), QgsPointXY( 401000, 4281000 ), crs, false );
    p.setPointType( "bridge" );
    points.append( p );
  }

  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + "/round.points";
  REQUIRE( rsSaveGcpPointsFile( path, points ) );

  // Verify on-disk marker line.
  QFile f( path );
  REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
  QTextStream in( &f );
  const QString head = in.readLine();
  REQUIRE( head == QString( "# QGEOS .points v2" ) );
  f.close();

  // Round-trip.
  QVector<QgsGcpPoint> loaded;
  REQUIRE( rsLoadGcpPointsFile( path, crs, loaded ) );
  REQUIRE( loaded.size() == 2 );
  REQUIRE( loaded.at( 0 ).pointType() == QString( "road" ) );
  REQUIRE( loaded.at( 1 ).pointType() == QString( "bridge" ) );
  REQUIRE( loaded.at( 0 ).isEnabled() );
  REQUIRE_FALSE( loaded.at( 1 ).isEnabled() );
}

TEST_CASE( "GCP .points v2: destination CRS survives save→load round-trip", "[georef][points][crs]" )
{
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  QVector<QgsGcpPoint> points;
  points.append( QgsGcpPoint( QgsPointXY( 100, 200 ), QgsPointXY( 400000, 4280000 ), crs, true ) );
  points.append( QgsGcpPoint( QgsPointXY( 300, 400 ), QgsPointXY( 401000, 4281000 ), crs, false ) );

  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + "/crs.points";
  REQUIRE( rsSaveGcpPointsFile( path, points ) );

  // Load with a DIFFERENT fallback CRS: the CRS stored in the file must win,
  // otherwise saved GCPs silently lose their destination CRS.
  QgsCoordinateReferenceSystem otherCrs( QStringLiteral( "EPSG:4326" ) );
  QVector<QgsGcpPoint> loaded;
  REQUIRE( rsLoadGcpPointsFile( path, otherCrs, loaded ) );
  REQUIRE( loaded.size() == 2 );
  REQUIRE( loaded.at( 0 ).destinationPointCrs().authid() == QString( "EPSG:32650" ) );
  REQUIRE( loaded.at( 1 ).destinationPointCrs().authid() == QString( "EPSG:32650" ) );
}

TEST_CASE( "GCP .points v1: legacy file without header reads with empty type", "[georef][points]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + "/legacy.points";

  QFile f( path );
  REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Text ) );
  {
    QTextStream out( &f );
    out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual\n";
    out << "400000,4280000,100,-200,1,0,0,0\n";
  }
  f.close();

  QVector<QgsGcpPoint> loaded;
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  REQUIRE( rsLoadGcpPointsFile( path, crs, loaded ) );
  REQUIRE( loaded.size() == 1 );
  REQUIRE( loaded.at( 0 ).pointType().isEmpty() );
  REQUIRE( loaded.at( 0 ).isEnabled() );
}
