// tests/test_r27_t04_async_bench.cpp — R27-T04 (#1790, #1791, #1794)
//
// Measured responsiveness oracle: the GUI-thread slot cost of the three
// async-ified sampling paths must stay in the milliseconds (the scheduling
// + snapshot work only), while the full computation completes off-thread.
// Reported per path: p50/p95 slot time (50 calls) and the p95 end-to-end
// completion latency, over a raster sized to make the HEAD synchronous
// cost plainly observable (same work, same thread count).
//
// Assertions use a generous 50 ms slot ceiling — the point is the O()
// (HEAD blocked for the whole read; the assert catches a regression back
// to synchronous behavior), not micro-benchmark precision.
#include <catch2/catch_test_macros.hpp>

#include "classification/qgsclassificationmainwindow.h"
#include "classification/rs_roi_tool_magicwand.h"
#include "classification/rs_spectral_curve_widget.h"
#include "widgets/cross_section_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <gdal.h>
#include <gdal_priv.h>

#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgspointxy.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_async_bench";
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

  // 1200x1200 3-band Float32 raster: enough rows/bands that the HEAD
  // synchronous recompute / transect / fill took a human-noticeable pause.
  QString makeRaster( const QString &path, int W, int H, int bands )
  {
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, bands, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    ds->SetGeoTransform( gt );
    std::vector<float> line( W );
    for ( int b = 1; b <= bands; ++b )
      for ( int r = 0; r < H; ++r )
      {
        for ( int c = 0; c < W; ++c )
          line[static_cast<size_t>( c )] = static_cast<float>( b * 10 + ( r % 97 ) );
        ds->GetRasterBand( b )->RasterIO( GF_Write, 0, r, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
      }
    GDALClose( ds );
    return path;
  }

  void addSample( QgsVectorLayer *layer, int classId, double x0, double y0, double size = 15.0 )
  {
    if ( !layer->isEditable() )
      layer->startEditing();
    QgsFeature f( layer->fields() );
    QgsPolygonXY poly;
    QgsPolylineXY ring = { QgsPointXY( x0, y0 ), QgsPointXY( x0 + size, y0 ),
                           QgsPointXY( x0 + size, y0 + size ),
                           QgsPointXY( x0, y0 + size ), QgsPointXY( x0, y0 ) };
    poly << ring;
    f.setGeometry( QgsGeometry::fromPolygonXY( poly ) );
    f.setAttribute( QStringLiteral( "cls_id" ), classId );
    layer->addFeature( f );
    layer->commitChanges();
  }

  // Same raster but with gt[3]=H: map y in [0,H] (canvas-facing).
  QString makeRasterCanvasY( const QString &path, int W, int H, int bands )
  {
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, bands, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, double( H ), 0, -1 };
    ds->SetGeoTransform( gt );
    std::vector<float> line( W );
    for ( int b = 1; b <= bands; ++b )
      for ( int r = 0; r < H; ++r )
      {
        for ( int c = 0; c < W; ++c )
          line[static_cast<size_t>( c )] = 100.f * b;
        ds->GetRasterBand( b )->RasterIO( GF_Write, 0, r, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
      }
    GDALClose( ds );
    return path;
  }

  QgsVectorLayer *sampleLayerOf( QgsClassificationMainWindow &w )
  {
    return w.findChild<QgsVectorLayer *>( QStringLiteral( "rsClassifySampleLayer" ) );
  }

  double percentile( std::vector<qint64> v, double p )
  {
    std::sort( v.begin(), v.end() );
    const size_t idx = std::min( v.size() - 1,
                                 static_cast<size_t>( p * ( v.size() - 1 ) ) );
    return static_cast<double>( v[idx] ) / 1000.0; // ms
  }

  void pumpUntil( const std::function<bool()> &done, int timeoutMs )
  {
    QElapsedTimer t;
    t.start();
    while ( !done() )
    {
      REQUIRE( t.elapsed() < timeoutMs );
      QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
      QThread::msleep( 2 );
    }
  }
} // namespace

TEST_CASE( "Recompute slot cost stays millisecond-scale (#1794 bench)",
           "[bench][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString raster = makeRaster( tmp.filePath( QStringLiteral( "bench.tif" ) ), 1200, 1200, 3 );

  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.openSourceRaster( raster ) );
  auto *layer = sampleLayerOf( w );
  REQUIRE( layer != nullptr );
  for ( int cls = 1; cls <= 4; ++cls )
    for ( int i = 0; i < 10; ++i )
      addSample( layer, cls, 50.0 + i * 20.0, -( 60.0 + cls * 200.0 + i * 40.0 ) );
  auto *curves = w.findChild<RsSpectralCurveWidget *>();
  REQUIRE( curves != nullptr );

  // One dispatch → measured slot cost + end-to-end completion; then five
  // more slot timings (each schedules the same full read on the pool).
  QElapsedTimer total;
  total.start();
  QElapsedTimer t;
  t.start();
  w.recomputeSpectralCurves();
  const double slot0Ms = t.elapsed();
  pumpUntil( [&] { return curves->curves().size() == 4; }, 30000 );
  const double e2eMs = total.elapsed();

  std::vector<qint64> slotUs{ qint64( slot0Ms * 1000 ) };
  for ( int i = 0; i < 5; ++i )
  {
    t.start();
    w.recomputeSpectralCurves();
    slotUs.push_back( t.nsecsElapsed() / 1000 );
    QThread::msleep( 40 ); // pace dispatches; landings replace curves
  }
  QThread::msleep( 400 );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

  fprintf(stderr, "[BENCH] recompute(1200x1200x3, 40 ROIs): slot p50=%.3fms p95=%.3fms; first landing e2e=%.1fms\n",
          percentile( slotUs, 0.5 ), percentile( slotUs, 0.95 ), e2eMs );
  // HEAD ran the whole scanline read inside the slot; the async seam
  // schedules + snapshots only.
  CHECK( percentile( slotUs, 0.95 ) < 50.0 );
  CHECK( curves->curves().size() == 4 );
}

TEST_CASE( "Cross-section slot cost stays millisecond-scale (#1790 bench)",
           "[bench][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString raster = makeRaster( tmp.filePath( QStringLiteral( "cs.tif" ) ), 1200, 1200, 3 );

  QgsRasterLayer layer( raster, QStringLiteral( "bench" ) );
  REQUIRE( layer.isValid() );
  CrossSectionWidget w;
  w.setRasterLayer( &layer );

  // Sequential dispatch→landing pairs (hasData toggles per extract), so a
  // slot time is never taken while the pool still owes a landing.
  std::vector<qint64> slotUs;
  std::vector<qint64> e2eUs;
  for ( int i = 0; i < 8; ++i )
  {
    QElapsedTimer total;
    total.start();
    QElapsedTimer t;
    t.start();
    w.extractProfile( QgsPointXY( 0.5, -( 100.0 + i ) ),
                      QgsPointXY( 1100.5, -( 100.0 + i ) ) );
    slotUs.push_back( t.nsecsElapsed() / 1000 );
    pumpUntil( [&] { return w.hasData(); }, 30000 );
    e2eUs.push_back( total.nsecsElapsed() / 1000 );
    w.clear();
    pumpUntil( [&] { return !w.hasData(); }, 5000 );
  }

  fprintf(stderr, "[BENCH] cross-section(1100-sample transect): slot p50=%.3fms p95=%.3fms; e2e p95=%.1fms\n",
          percentile( slotUs, 0.5 ), percentile( slotUs, 0.95 ), percentile( e2eUs, 0.95 ) );
  // HEAD did 1100 per-sample RasterIO calls inside the slot.
  CHECK( percentile( slotUs, 0.95 ) < 50.0 );
}

TEST_CASE( "Magic-wand slot cost stays millisecond-scale (#1791 bench)",
           "[bench][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  constexpr int W = 1200, H = 1200;
  const QString raster = makeRaster( tmp.filePath( QStringLiteral( "mw.tif" ) ), W, H, 3 );

  // Raster for the canvas case uses gt[3]=H (map y in [0,H], the same
  // convention as the functional magic-wand suites) so the click's map
  // point falls INSIDE the raster.
  const QString rasterCanvas = makeRasterCanvasY( tmp.filePath( QStringLiteral( "mwc.tif" ) ), W, H, 3 );

  QgsMapCanvas canvas;
  canvas.resize( 400, 400 );
  const QgsRectangle extent( 0, 0, W, H );
  canvas.setExtent( extent );
  canvas.show();
  QCoreApplication::processEvents();

  RsRoiToolMagicWand tool( &canvas );
  tool.setTolerance( 1.0 );
  tool.setSourceData( rasterCanvas );
  QSignalSpy drawn( &tool, &RsRoiToolBase::roiDrawn );

  std::vector<qint64> slotUs;
  std::vector<qint64> e2eUs;
  for ( int i = 0; i < 8; ++i )
  {
    const double wx = ( 600.5 ) / W * 400;
    const double wy = ( 600.5 ) / H * 400;
    QMouseEvent me( QEvent::MouseButtonRelease, QPointF( wx, wy ),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QgsMapMouseEvent mme( &canvas, &me );
    QElapsedTimer total;
    total.start();
    QElapsedTimer t;
    t.start();
    tool.canvasReleaseEvent( &mme );
    slotUs.push_back( t.nsecsElapsed() / 1000 );
    pumpUntil( [&] { return drawn.count() > i; }, 30000 );
    e2eUs.push_back( total.nsecsElapsed() / 1000 );
  }

  fprintf(stderr, "[BENCH] magic-wand(513x513x3 read+fill): slot p50=%.3fms p95=%.3fms; e2e p95=%.1fms\n",
          percentile( slotUs, 0.5 ), percentile( slotUs, 0.95 ), percentile( e2eUs, 0.95 ) );
  // HEAD did the open+read+flood-fill+contour inside the release event.
  CHECK( percentile( slotUs, 0.95 ) < 50.0 );
  CHECK( drawn.count() == 8 );
}
