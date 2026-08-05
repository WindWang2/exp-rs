#include <catch2/catch_test_macros.hpp>

#include "rs_georeferencing_session.h"
#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsgeoreftransform.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>

TEST_CASE( "Session GCP: addGcp emits gcpsChanged", "[georef][gcplist]" )
{
  RsGeoreferencingSession session;
  QSignalSpy spy( &session, &RsGeoreferencingSession::gcpsChanged );
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  const QgsGcpPoint pair( QgsPointXY( 10, 20 ), QgsPointXY( 100, 200 ), crs, true );
  session.addGcp( pair );
  REQUIRE( spy.count() == 1 );
}

TEST_CASE( "Session GCP: removeGcpAt emits gcpsChanged", "[georef][gcplist]" )
{
  RsGeoreferencingSession session;
  QSignalSpy spy( &session, &RsGeoreferencingSession::gcpsChanged );

  const QgsGcpPoint pair( QgsPointXY( 10, 20 ), QgsPointXY( 100, 200 ),
                          QgsCoordinateReferenceSystem(), true );
  session.addGcp( pair );
  REQUIRE( spy.count() == 1 );

  session.removeGcpAt( 0 );
  REQUIRE( spy.count() == 2 );
}

TEST_CASE( "Session GCP: clearGcps emits gcpsChanged", "[georef][gcplist]" )
{
  RsGeoreferencingSession session;
  QSignalSpy spy( &session, &RsGeoreferencingSession::gcpsChanged );

  for ( int i = 0; i < 5; ++i )
  {
    const QgsGcpPoint pair( QgsPointXY( i * 10, i * 20 ), QgsPointXY( i * 100, i * 200 ),
                            QgsCoordinateReferenceSystem(), true );
    session.addGcp( pair );
  }
  REQUIRE( spy.count() == 5 );

  session.clearGcps();
  REQUIRE( spy.count() == 6 );
}

TEST_CASE( "Session GCP: clearGcps on empty does not emit", "[georef][gcplist]" )
{
  RsGeoreferencingSession session;
  QSignalSpy spy( &session, &RsGeoreferencingSession::gcpsChanged );

  session.clearGcps();
  REQUIRE( spy.count() == 0 );
}

TEST_CASE( "Session GCP: .points codec round-trip preserves type", "[georef][gcplist]" )
{
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  QVector<QgsGcpPoint> points;
  {
    QgsGcpPoint p( QgsPointXY( 100, 200 ), QgsPointXY( 400000, 4280000 ), crs, true );
    p.setPointType( QStringLiteral( "road" ) );
    points.append( p );
  }
  {
    QgsGcpPoint p( QgsPointXY( 300, 400 ), QgsPointXY( 401000, 4281000 ), crs, false );
    p.setPointType( QStringLiteral( "bridge" ) );
    points.append( p );
  }

  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + "/round.points";
  REQUIRE( rsSaveGcpPointsFile( path, points ) );

  QVector<QgsGcpPoint> loaded;
  REQUIRE( rsLoadGcpPointsFile( path, crs, loaded ) );
  REQUIRE( loaded.size() == 2 );
  REQUIRE( loaded.at( 0 ).pointType() == QString( "road" ) );
  REQUIRE( loaded.at( 1 ).pointType() == QString( "bridge" ) );
  REQUIRE( loaded.at( 0 ).isEnabled() );
  REQUIRE_FALSE( loaded.at( 1 ).isEnabled() );
}
