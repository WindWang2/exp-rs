#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QBrush>
#include <QColor>

#include "qgsgcplistwidget.h"
#include "qgsgcplist.h"
#include "qgsgcplistmodel.h"
#include "qgsgcppoint.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

#include <cstdlib>

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

TEST_CASE( "GCP table: shows map + pixel columns for both images", "[georef][table]" )
{
  ensureApp();
  QgsGCPListWidget w;
  QgsGCPList list;
  w.setGCPList( &list );
  REQUIRE( w.model() != nullptr );
  REQUIRE( w.model()->columnCount() == static_cast<int>( QgsGCPListModel::Column::LastColumn ) );
  REQUIRE( w.model()->columnCount() == 14 );
}

TEST_CASE( "GCP table: displays pointType in last type column", "[georef][table]" )
{
  ensureApp();
  QgsGCPListWidget w;
  QgsGCPList list;
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:4326" ) );
  QgsGcpPoint p( QgsPointXY( 0, 0 ), QgsPointXY( 0, 0 ), crs, true );
  p.setPointType( QStringLiteral( "river" ) );
  list.appendPoint( p );
  w.setGCPList( &list );
  REQUIRE( w.model()->rowCount() == 1 );
  const int typeCol = static_cast<int>( QgsGCPListModel::Column::PointType );
  const QString shown = w.model()->data( w.model()->index( 0, typeCol ), Qt::DisplayRole ).toString();
  REQUIRE( shown == QStringLiteral( "river" ) );
}

TEST_CASE( "GCP table: residual warn foreground on total residual column", "[georef][table]" )
{
  ensureApp();
  QgsGCPListWidget w;
  QgsGCPList list;
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:4326" ) );
  QgsGcpPoint p( QgsPointXY( 1, 2 ), QgsPointXY( 100, 200 ), crs, true );
  list.appendPoint( p );
  w.setGCPList( &list );
  // Pixel residual warn threshold is 2.0
  list.at( 0 )->setResidual( QPointF( 3.0, 3.0 ) );

  const int rmsCol = static_cast<int>( QgsGCPListModel::Column::TotalResidual );
  const QVariant fg = w.model()->data( w.model()->index( 0, rmsCol ), Qt::ForegroundRole );
  REQUIRE( fg.isValid() );
  REQUIRE( fg.canConvert<QBrush>() );
  const QBrush brush = fg.value<QBrush>();
  REQUIRE( brush.color() == QColor( QStringLiteral( "#bf8700" ) ) );
}
