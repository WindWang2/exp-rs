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
#include <opencv2/imgproc.hpp>

namespace
{
/// Build the true region polygon from the local-window flood-fill pixels.
///
/// The mask is upscaled 2x and contoured so the ring passes within a quarter
/// pixel of the exact pixel-square boundary; center-convention rasterization
/// (RsPixelRasterizer / GDALRasterizeGeometries default) therefore recovers
/// exactly the flooded set - unlike the previous bbox fallback, which
/// swallowed background pixels into training samples (#283).
QgsGeometry regionGeometry( const QSet<quint64> &localPixels, int winW, int winH,
                            int rowStart, int colStart, const double gt[6] )
{
  cv::Mat mask = cv::Mat::zeros( winH, winW, CV_8U );
  for ( quint64 i : localPixels )
    mask.at<uchar>( static_cast<int>( i / quint64( winW ) ),
                    static_cast<int>( i % quint64( winW ) ) ) = 1;

  cv::Mat up;
  cv::resize( mask, up, cv::Size( winW * 2, winH * 2 ), 0, 0, cv::INTER_NEAREST );

  std::vector<std::vector<cv::Point>> contours;
  std::vector<cv::Vec4i> hierarchy;
  cv::findContours( up, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE );

  auto ringFromContour = [&]( const std::vector<cv::Point> &contour ) -> QVector<QgsPointXY>
  {
    QVector<QgsPointXY> ring;
    ring.reserve( static_cast<int>( contour.size() ) + 1 );
    for ( const cv::Point &pt : contour )
    {
      // Upsampled pixel center -> original pixel-corner space.
      const double px = colStart + ( pt.x + 0.5 ) / 2.0;
      const double py = rowStart + ( pt.y + 0.5 ) / 2.0;
      ring.append( QgsPointXY( gt[0] + gt[1] * px + gt[2] * py,
                               gt[3] + gt[4] * px + gt[5] * py ) );
    }
    if ( !ring.isEmpty() )
      ring.append( ring.first() );
    return ring;
  };

  // RETR_CCOMP: top-level contours are outer boundaries, their children
  // (hierarchy[j][3] == i) are holes. A 4-connected fill yields one outer.
  QgsPolygonXY polygon;
  for ( size_t i = 0; i < contours.size(); ++i )
  {
    if ( !contours[i].empty() && hierarchy[i][3] == -1 )
    {
      const QVector<QgsPointXY> outer = ringFromContour( contours[i] );
      if ( outer.size() >= 4 )
      {
        polygon.append( outer );
        for ( size_t j = 0; j < contours.size(); ++j )
        {
          if ( hierarchy[j][3] == static_cast<int>( i ) )
          {
            const QVector<QgsPointXY> hole = ringFromContour( contours[j] );
            if ( hole.size() >= 4 )
              polygon.append( hole );
          }
        }
      }
    }
  }
  if ( polygon.isEmpty() )
    return QgsGeometry();
  return QgsGeometry::fromPolygonXY( polygon );
}
} // namespace

void RsRoiToolMagicWand::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e || mRasterPath.isEmpty() )
  {
    return;
  }

  GDALAllRegister();
  auto *ds = static_cast<GDALDataset *>( GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    return;
  }

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

  // O3: cap band reads to avoid 421 MB transient at 400 bands — read single band when hyperspectral
  const int readB = ( B > 8 ? 1 : B );
  cv::Mat img( winH, winW, CV_32FC( readB ) );
  std::vector<float> band( static_cast<size_t>( winW ) * static_cast<size_t>( winH ) );
  for ( int b = 0; b < readB; ++b )
  {
    // When hyperspectral, flood-fill uses band 1 only (intensity); otherwise all bands spectral.
    const int gdalBand = ( readB == 1 ? 1 : b + 1 );
    if ( ds->GetRasterBand( gdalBand )->RasterIO(
           GF_Read, colStart, rowStart, winW, winH, band.data(), winW, winH, GDT_Float32, 0, 0 )
         != CE_None )
    {
      qWarning() << "RsRoiToolMagicWand: failed to read band" << gdalBand;
      GDALClose( ds );
      return;
    }
    for ( int r = 0; r < winH; ++r )
    {
      float *rowPtr = reinterpret_cast<float *>( img.ptr( r ) );
      for ( int c = 0; c < winW; ++c )
        rowPtr[c * readB + b] = band[static_cast<size_t>( r ) * winW + c];
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

  // True region polygon; bbox kept only as a fallback for degenerate fills.
  QgsGeometry geom = regionGeometry( localPixels, winW, winH, rowStart, colStart, gt );
  if ( geom.isNull() || geom.isEmpty() )
  {
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
    const double cx[4] = {
      gt[0] + gt[1] * double( cMin ) + gt[2] * double( rMin ),
      gt[0] + gt[1] * double( cMax + 1 ) + gt[2] * double( rMin ),
      gt[0] + gt[1] * double( cMin ) + gt[2] * double( rMax + 1 ),
      gt[0] + gt[1] * double( cMax + 1 ) + gt[2] * double( rMax + 1 )
    };
    const double cy[4] = {
      gt[3] + gt[4] * double( cMin ) + gt[5] * double( rMin ),
      gt[3] + gt[4] * double( cMax + 1 ) + gt[5] * double( rMin ),
      gt[3] + gt[4] * double( cMin ) + gt[5] * double( rMax + 1 ),
      gt[3] + gt[4] * double( cMax + 1 ) + gt[5] * double( rMax + 1 )
    };
    geom = QgsGeometry::fromRect( QgsRectangle( *std::min_element( cx, cx + 4 ),
                                                *std::min_element( cy, cy + 4 ),
                                                *std::max_element( cx, cx + 4 ),
                                                *std::max_element( cy, cy + 4 ) ) );
  }

  // #288 - surface a silent truncation: a fill that reaches the search-window
  // edge (where that edge is not the raster boundary) was clipped.
  for ( quint64 i : localPixels )
  {
    const int lr = static_cast<int>( i / quint64( winW ) );
    const int lc = static_cast<int>( i % quint64( winW ) );
    if ( ( lr == 0 && rowStart > 0 ) || ( lr == winH - 1 && rowStart + winH < H ) ||
         ( lc == 0 && colStart > 0 ) || ( lc == winW - 1 && colStart + winW < W ) )
    {
      emit regionClipped();
      break;
    }
  }

  const QVector<quint64> idx( pixels.begin(), pixels.end() );
  emit roiDrawnPixels( geom, mClassId, idx );
  emit roiDrawn( geom, mClassId );
}
