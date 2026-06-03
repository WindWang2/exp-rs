// Phase 10A Task 10.7 — RsRoiToolMagicWand implementation.
#include "rs_roi_tool_magicwand.h"

#include "rs_flood_fill.h"

#include "qgsgeometry.h"
#include "qgsmapmouseevent.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <algorithm>
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
  const int sc = int( inv[0] + inv[1] * map.x() + inv[2] * map.y() );
  const int sr = int( inv[3] + inv[4] * map.x() + inv[5] * map.y() );
  if ( sr < 0 || sr >= H || sc < 0 || sc >= W || B <= 0 )
  {
    GDALClose( ds );
    return;
  }

  // Read full image into a CV_32FC(B) cv::Mat (band-interleaved per-pixel).
  cv::Mat img( H, W, CV_32FC( B ) );
  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < B; ++b )
  {
    if ( ds->GetRasterBand( b + 1 )->RasterIO(
           GF_Read, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 )
         != CE_None )
    {
      GDALClose( ds );
      return;
    }
    for ( int r = 0; r < H; ++r )
    {
      float *rowPtr = reinterpret_cast<float *>( img.ptr( r ) );
      for ( int c = 0; c < W; ++c )
        rowPtr[c * B + b] = band[static_cast<size_t>( r ) * W + c];
    }
  }
  GDALClose( ds );

  const QSet<quint64> pixels = RsFloodFill::run( img, sr, sc, mTolerance );
  if ( pixels.isEmpty() )
    return;

  // Bbox of selected pixels (v1 approximation).
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
  emit roiDrawn( QgsGeometry::fromRect( bbox ), mClassId );
}
