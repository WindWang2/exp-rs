#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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

TEST_CASE( "GCP .points v2: georeferenced raster converts source map to pixel coords", "[georef][points][geotransform]" )
{
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  // Raster geotransform: origin (1000, 2000), 10m pixels north-up: gt = [1000, 10, 0, 2000, 0, -10]
  const double gt[6] = { 1000.0, 10.0, 0.0, 2000.0, 0.0, -10.0 };

  // A point at pixel (5, 8) in map space is (1000 + 5*10, 2000 - 8*10) = (1050, 1920)
  QVector<QgsGcpPoint> points;
  points.append( QgsGcpPoint( QgsPointXY( 1050.0, 1920.0 ), QgsPointXY( 400000, 4280000 ), crs, true ) );

  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + "/georef.points";
  REQUIRE( rsSaveGcpPointsFile( path, points, gt ) );

  // Check file content: pixelX and pixelY columns should store (5, 8)
  QFile f( path );
  REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
  QTextStream in( &f );
  in.readLine(); // # QGEOS .points v2
  in.readLine(); // header
  const QString dataLine = in.readLine();
  f.close();

  const QStringList parts = dataLine.split( ',' );
  REQUIRE( parts.size() >= 4 );
  REQUIRE( parts[2].toDouble() == Catch::Approx( 5.0 ).margin( 1e-4 ) );
  REQUIRE( parts[3].toDouble() == Catch::Approx( -8.0 ).margin( 1e-4 ) );

  // Round-trip load with the same gt restores the map coordinate (1050, 1920)
  QVector<QgsGcpPoint> loaded;
  REQUIRE( rsLoadGcpPointsFile( path, crs, loaded, gt ) );
  REQUIRE( loaded.size() == 1 );
  REQUIRE( loaded.at( 0 ).sourcePoint().x() == Catch::Approx( 1050.0 ).margin( 1e-4 ) );
  REQUIRE( loaded.at( 0 ).sourcePoint().y() == Catch::Approx( 1920.0 ).margin( 1e-4 ) );
}
