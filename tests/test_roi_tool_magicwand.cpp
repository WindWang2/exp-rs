// test_roi_tool_magicwand.cpp - #283 / #288
//
// Verifies that the magic-wand tool's roiDrawn geometry rasterizes back to
// exactly the flooded pixel set (an L-shaped region whose bbox contains
// background pixels), and that regionClipped fires when the fill reaches the
// search-window edge away from the raster boundary.
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrectangle.h"
#include "rs_pixel_rasterizer.h"
#include "rs_roi_tool_magicwand.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPointF>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <functional>
#include <vector>

namespace
{
QApplication *ensureApp()
{
  static int argc = 1;
  static char arg0[] = "test";
  static char *argv[] = { arg0, nullptr };
  return qApp ? nullptr : new QApplication( argc, argv );
}

/// One-band Float32 GeoTIFF with GT {0,1,0,H,0,-1}; valueFn(r,c) per pixel.
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
      band[static_cast<size_t>( r * W + c )] = valueFn( r, c );
  ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H,
                                    GDT_Float32, 0, 0 );
  GDALClose( ds );
  return path;
}
} // namespace

TEST_CASE( "RoiToolMagicWand: roiDrawn geometry reproduces the flooded L-shape, not its bbox",
           "[classify][roitool][magicwand]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // 32x32 raster: value 100 inside an L-shaped region (rows 2..10 cols 2..6
  // plus rows 2..6 cols 7..14), 0 elsewhere. The bbox (rows 2..10, cols 2..14)
  // contains 63 background pixels the fill must exclude.
  auto inL = []( int r, int c )
  { return ( r >= 2 && r <= 10 && c >= 2 && c <= 6 ) || ( r >= 2 && r <= 6 && c >= 7 && c <= 14 ); };
  const int W = 32, H = 32;
  const QString raster = createRaster( tmp.path() + "/l.tif", W, H,
                                        [&]( int r, int c ) { return inL( r, c ) ? 100.0f : 0.0f; } );

  QgsMapCanvas canvas;
  canvas.resize( 500, 500 );
  canvas.setExtent( QgsRectangle( 0, 0, W, H ) );
  // A shown canvas gives toMapCoordinates() a valid viewport; without it the
  // widget->map mapping is off and the click lands outside the raster.
  canvas.show();
  QCoreApplication::processEvents();

  RsRoiToolMagicWand tool( &canvas );
  tool.setCurrentClassId( 1 );
  tool.setTolerance( 1.0 );
  tool.setSourceData( raster );
  QSignalSpy drawn( &tool, &RsRoiToolBase::roiDrawn );
  QSignalSpy exact( &tool, &RsRoiToolMagicWand::roiDrawnPixels );

  // Click the center of pixel (4, 4). With no CRS set, this canvas maps
  // widget y straight to map y (no GIS flip): pixel (4,4) center sits at
  // map (4.5, 27.5), so widget_y = 27.5/32*500.
  QMouseEvent me( QEvent::MouseButtonRelease, QPointF( 4.5 * 500.0 / W, ( 4 + 0.5 ) * 500.0 / H ),
                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  QgsMapMouseEvent mme( &canvas, &me );
  tool.canvasReleaseEvent( &mme );

  REQUIRE( drawn.count() == 1 );
  REQUIRE( exact.count() == 1 );

  // Exact indices = the L-shape.
  const QVector<quint64> idx = exact.at( 0 ).at( 2 ).value<QVector<quint64>>();
  int expected = 0;
  for ( int r = 0; r < H; ++r )
    for ( int c = 0; c < W; ++c )
      if ( inL( r, c ) )
        ++expected;
  REQUIRE( idx.size() == expected );

  // The emitted geometry must rasterize back to the same pixel set (#283).
  const QgsGeometry geom = drawn.at( 0 ).at( 0 ).value<QgsGeometry>();
  REQUIRE( !geom.isNull() );
  double gt[6] = { 0, 1, 0, double( H ), 0, -1 };
  const QSet<quint64> rasterized = RsPixelRasterizer::rasterize( geom, gt, W, H );
  REQUIRE( rasterized.size() == expected );
  for ( quint64 i : idx )
    REQUIRE( rasterized.contains( i ) );

  // Small region well inside the window: no clip warning.
  QSignalSpy clipped( &tool, &RsRoiToolMagicWand::regionClipped );
  QMouseEvent me2( QEvent::MouseButtonRelease, QPointF( 4.5 * 500.0 / W, ( 4 + 0.5 ) * 500.0 / H ),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  QgsMapMouseEvent mme2( &canvas, &me2 );
  tool.canvasReleaseEvent( &mme2 );
  REQUIRE( clipped.count() == 0 );
}

TEST_CASE( "RoiToolMagicWand: regionClipped fires when the fill hits the search window",
           "[classify][roitool][magicwand]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // 600x600 uniform raster: any fill runs to the 513x513 window edge, which is
  // inside the raster (not its boundary) on the +row/+col sides.
  const int W = 600, H = 600;
  const QString raster = createRaster( tmp.path() + "/uniform.tif", W, H,
                                        []( int, int ) { return 42.0f; } );

  QgsMapCanvas canvas;
  canvas.resize( 500, 500 );
  canvas.setExtent( QgsRectangle( 0, 0, W, H ) );
  canvas.show();
  QCoreApplication::processEvents();

  RsRoiToolMagicWand tool( &canvas );
  tool.setCurrentClassId( 1 );
  tool.setTolerance( 1.0 );
  tool.setSourceData( raster );
  QSignalSpy clipped( &tool, &RsRoiToolMagicWand::regionClipped );

  QMouseEvent me( QEvent::MouseButtonRelease, QPointF( 300.5 * 500.0 / W, ( H - 300 - 0.5 ) * 500.0 / H ),
                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  QgsMapMouseEvent mme( &canvas, &me );
  tool.canvasReleaseEvent( &mme );

  REQUIRE( clipped.count() == 1 );
}
