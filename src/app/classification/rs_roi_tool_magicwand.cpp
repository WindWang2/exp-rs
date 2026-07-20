// Phase 10A Task 10.7 — RsRoiToolMagicWand implementation.
#include "rs_roi_tool_magicwand.h"

#include "rs_flood_fill.h"

#include "qgsgeometry.h"
#include "qgsmapmouseevent.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <gdal_alg.h>
#include <gdal_priv.h>

#include <opencv2/core.hpp>

void RsRoiToolMagicWand::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e || mRasterPath.isEmpty() )
    return;

  GDALAllRegister();
  auto *ds = static_cast<GDALDataset *>( GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int B = ds->GetRasterCount();
  double gt[6];
  if ( ds->GetGeoTransform( gt ) != CE_None )
  {
    GDALClose( ds );
    return;
  }

  // Convert canvas click → map coords → raster pixel via inverse GT.
  const QgsPointXY map = toMapCoordinates( e->pos() );
  double inv[6];
  if ( !GDALInvGeoTransform( gt, inv ) )
  {
    GDALClose( ds );
    return;
  }
  const int sc = static_cast<int>( std::floor( inv[0] + inv[1] * map.x() + inv[2] * map.y() ) );
  const int sr = static_cast<int>( std::floor( inv[3] + inv[4] * map.x() + inv[5] * map.y() ) );
  if ( sr < 0 || sr >= H || sc < 0 || sc >= W || B <= 0 )
  {
    GDALClose( ds );
    return;
  }

  // Restrict magic wand search space to a local window to prevent massive
  // memory usage and UI freeze.  halfWindow=256 yields a 513x513 search
  // window (~263k pixels), which keeps the peak memory for a multi-band
  // flood-fill under ~10 MB even with 8+ bands.
  constexpr int kMagicWandHalfWindow = 256;
  const int halfWindow = kMagicWandHalfWindow;
  const int colStart = std::max( 0, sc - halfWindow );
  const int rowStart = std::max( 0, sr - halfWindow );
  const int colEnd = std::min( W - 1, sc + halfWindow );
  const int rowEnd = std::min( H - 1, sr + halfWindow );
  const int winW = colEnd - colStart + 1;
  const int winH = rowEnd - rowStart + 1;

  cv::Mat img( winH, winW, CV_32FC( B ) );
  std::vector<float> band( static_cast<size_t>( winW ) * static_cast<size_t>( winH ) );
  for ( int b = 0; b < B; ++b )
  {
    if ( ds->GetRasterBand( b + 1 )->RasterIO(
           GF_Read, colStart, rowStart, winW, winH, band.data(), winW, winH, GDT_Float32, 0, 0 )
         != CE_None )
    {
      qWarning() << "RsRoiToolMagicWand: failed to read band" << (b + 1);
      GDALClose( ds );
      return;
    }
    for ( int r = 0; r < winH; ++r )
    {
      float *rowPtr = reinterpret_cast<float *>( img.ptr( r ) );
      for ( int c = 0; c < winW; ++c )
        rowPtr[c * B + b] = band[static_cast<size_t>( r ) * winW + c];
    }
  }
  GDALClose( ds );

  const int localSr = sr - rowStart;
  const int localSc = sc - colStart;
  const QSet<quint64> localPixels = RsFloodFill::run( img, localSr, localSc, mTolerance );
  if ( localPixels.isEmpty() )
    return;

  QSet<quint64> pixels;
  pixels.reserve( localPixels.size() );
  for ( quint64 i : localPixels )
  {
    const int lr = static_cast<int>( i / quint64( winW ) );
    const int lc = static_cast<int>( i % quint64( winW ) );
    const int r = lr + rowStart;
    const int c = lc + colStart;
    pixels.insert( static_cast<quint64>( r ) * static_cast<quint64>( W ) + static_cast<quint64>( c ) );
  }

  // Bbox of selected pixels — display geometry only. Training uses exact
  // pixel indices via roiDrawnPixels (do NOT re-rasterize the bbox).
  quint64 rMin = std::numeric_limits<quint64>::max();
  quint64 cMin = std::numeric_limits<quint64>::max();
  quint64 rMax = 0;
  quint64 cMax = 0;
  for ( quint64 i : pixels )
  {
    const quint64 r = i / quint64( W );
    const quint64 c = i % quint64( W );
    if ( r < rMin ) rMin = r;
    if ( r > rMax ) rMax = r;
    if ( c < cMin ) cMin = c;
    if ( c > cMax ) cMax = c;
  }

  const double x0 = gt[0] + gt[1] * double( cMin ) + gt[2] * double( rMin );
  const double y0 = gt[3] + gt[4] * double( cMin ) + gt[5] * double( rMin );
  const double x1 = gt[0] + gt[1] * double( cMax + 1 ) + gt[2] * double( rMax + 1 );
  const double y1 = gt[3] + gt[4] * double( cMax + 1 ) + gt[5] * double( rMax + 1 );
  QgsRectangle bbox( std::min( x0, x1 ), std::min( y0, y1 ),
                     std::max( x0, x1 ), std::max( y0, y1 ) );
  const QgsGeometry geom = QgsGeometry::fromRect( bbox );
  const QVector<quint64> idx( pixels.begin(), pixels.end() );
  emit roiDrawnPixels( geom, mClassId, idx );
  // Keep base signal for any generic listeners; onRoiDrawnPixels is preferred.
  emit roiDrawn( geom, mClassId );
}
