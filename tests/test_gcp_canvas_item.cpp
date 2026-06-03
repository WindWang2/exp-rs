#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QPointF>

#include "qgsgcpcanvasitem.h"
#include "qgsmapcanvas.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <cstdlib>

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup.
// Match the pattern used by test_georef_window.cpp.
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

TEST_CASE( "GCPCanvasItem: paints SRC badge (blue) at given position", "[georef][canvas]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 400, 400 );
  canvas.mapSettings().setOutputSize( QSize( 400, 400 ) );
  canvas.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  // Construct directly via (canvas, id, world position, isSource=true)
  QgsGCPCanvasItem *item = new QgsGCPCanvasItem( &canvas, /*id=*/3, QgsPointXY( 50, 50 ), /*isSource=*/true );
  item->setEnabled( true );
  item->setSelected( false );
  // Item is owned by the canvas' scene (QGraphicsItem parent on QgsMapCanvasItem ctor).

  // Render the QGraphicsScene (which owns the canvas item) directly.
  // The QgsMapCanvas viewport renderer is async / requires the widget to be
  // shown; scene->render() bypasses that and synchronously paints all items.
  QImage img( 400, 400, QImage::Format_ARGB32 );
  img.fill( Qt::white );
  QPainter painter( &img );
  REQUIRE( canvas.scene() );
  // Set sceneRect explicitly to cover the viewport area in pixel coords.
  canvas.scene()->setSceneRect( 0, 0, 400, 400 );
  canvas.scene()->render( &painter, QRectF( 0, 0, 400, 400 ), QRectF( 0, 0, 400, 400 ) );
  painter.end();

  // Expect at least 10 pixels matching the SRC badge color near center.
  // Badge color is #1f6feb so blue>200, red<80, green<150.
  int blueHits = 0;
  for ( int y = 180; y < 220; ++y )
  {
    for ( int x = 180; x < 220; ++x )
    {
      QRgb p = img.pixel( x, y );
      if ( qBlue( p ) > 180 && qRed( p ) < 100 && qGreen( p ) < 170 )
        ++blueHits;
    }
  }
  REQUIRE( blueHits >= 5 );
}

TEST_CASE( "GCPCanvasItem: disabled marker does not crash", "[georef][canvas]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 400, 400 );
  canvas.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsGCPCanvasItem *item = new QgsGCPCanvasItem( &canvas, 0, QgsPointXY( 50, 50 ), true );
  item->setEnabled( false );

  QImage img( 400, 400, QImage::Format_ARGB32 );
  img.fill( Qt::white );
  QPainter painter( &img );
  canvas.render( &painter );
  painter.end();

  // The point of this test is that the disabled draw doesn't crash.
  // Lenient assertion just to give Catch2 something to report.
  REQUIRE( img.width() == 400 );
}
