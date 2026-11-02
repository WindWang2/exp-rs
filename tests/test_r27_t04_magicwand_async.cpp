// tests/test_r27_t04_magicwand_async.cpp — R27-T04 (#1791)
//
// Contract under test: the magic-wand fill (windowed multi-band RasterIO +
// flood fill) runs OFF the GUI thread — canvasReleaseEvent returns before
// any result exists, the landing arrives through the event loop, and a
// rapid second click supersedes the first (exactly one landing, the newest).
// On HEAD the fill ran synchronously inside the release event.
//
// Geometry/clip correctness of the fill itself is pinned by
// tests/test_roi_tool_magicwand.cpp (adapted to the async landing).
#include <catch2/catch_test_macros.hpp>

#include "classification/rs_roi_tool_magicwand.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrectangle.h"

#include <gdal_priv.h>

#include <functional>
#include <vector>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_magicwand_async";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      return new QApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  QString createRaster( const QString &path, int W, int H,
                        const std::function<float( int, int )> &valueFn )
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, double( H ), 0, -1 };
    ds->SetGeoTransform( gt );
    std::vector<float> band( static_cast<size_t>( W ) * H );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        band[static_cast<size_t>( r ) * W + c ] = valueFn( r, c );
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return {};
  }

  QgsMapMouseEvent clickAt( QgsMapCanvas &canvas, double mapX, double mapY,
                            int canvasW, int canvasH, const QgsRectangle &extent )
  {
    const double wx = ( mapX - extent.xMinimum() ) / extent.width() * canvasW;
    const double wy = ( extent.yMaximum() - mapY ) / extent.height() * canvasH;
    QMouseEvent me( QEvent::MouseButtonRelease, QPointF( wx, wy ),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    return QgsMapMouseEvent( &canvas, &me );
  }
} // namespace

TEST_CASE( "Magic wand fills off the GUI thread; rapid re-click supersedes (#1791)",
           "[classify][magicwand][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const int W = 64, H = 64;
  // Two distinct uniform squares so class ids differ by click position.
  const QString raster = createRaster( tmp.path() + "/two.tif", W, H,
    []( int r, int c ) { return ( r < 32 && c < 32 ) ? 100.0f : 200.0f; } );

  QgsMapCanvas canvas;
  canvas.resize( 500, 500 );
  const QgsRectangle extent( 0, 0, W, H );
  canvas.setExtent( extent );
  canvas.show();
  QCoreApplication::processEvents();

  RsRoiToolMagicWand tool( &canvas );
  tool.setTolerance( 1.0 );
  tool.setSourceData( raster );
  QSignalSpy drawn( &tool, &RsRoiToolBase::roiDrawn );

  // Click inside the left-top square (map coords; gt maps y -> H - y).
  QgsMapMouseEvent click1 = clickAt( canvas, 10.5, double( H ) - 10.5, 500, 500, extent );
  tool.canvasReleaseEvent( &click1 );

  // RED on HEAD: the fill landed synchronously inside the release event.
  REQUIRE( drawn.count() == 0 );

  // Pump until the landing (bounded wait).
  QElapsedTimer t;
  t.start();
  while ( drawn.count() == 0 )
  {
    REQUIRE( t.elapsed() < 10000 );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  REQUIRE( drawn.count() == 1 );
  REQUIRE( drawn.at( 0 ).at( 1 ).toInt() == tool.currentClassId() );

  // Rapid double click: two dispatches, one landing (the newest wins).
  QgsMapMouseEvent click2 = clickAt( canvas, 10.5, double( H ) - 10.5, 500, 500, extent );
  QgsMapMouseEvent click3 = clickAt( canvas, 50.5, double( H ) - 50.5, 500, 500, extent );
  tool.canvasReleaseEvent( &click2 );
  tool.canvasReleaseEvent( &click3 );
  // HEAD would have landed BOTH synchronously (count 3 total); the async
  // tool drops the superseded first dispatch.
  REQUIRE( drawn.count() == 1 );

  t.restart();
  while ( drawn.count() < 2 )
  {
    REQUIRE( t.elapsed() < 10000 );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  REQUIRE( drawn.count() == 2 ); // exactly one more landing — the latest click
}
