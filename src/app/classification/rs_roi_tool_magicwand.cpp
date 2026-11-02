// Phase 10A Task 10.7 — RsRoiToolMagicWand implementation.
#include "rs_roi_tool_magicwand.h"

#include "rs_flood_fill.h"
#include "widgets/rs_scan_pool.h"

#include "qgsgeometry.h"
#include "qgsmapmouseevent.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <QMetaObject>
#include <QPointer>
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
/// Build the true region polygon RINGS from the local-window flood-fill
/// pixels. Plain value types only — this now runs on the worker thread
/// (#1791), and no QGIS geometry object may be touched off the GUI thread.
///
/// The mask is upscaled 2x and contoured so the ring passes within a quarter
/// pixel of the exact pixel-square boundary; center-convention rasterization
/// (RsPixelRasterizer / GDALRasterizeGeometries default) therefore recovers
/// exactly the flooded set - unlike the previous bbox fallback, which
/// swallowed background pixels into training samples (#283).
QVector<QVector<QgsPointXY>> regionRings( const QSet<quint64> &localPixels, int winW, int winH,
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
  QVector<QVector<QgsPointXY>> rings;
  for ( size_t i = 0; i < contours.size(); ++i )
  {
    if ( !contours[i].empty() && hierarchy[i][3] == -1 )
    {
      const QVector<QgsPointXY> outer = ringFromContour( contours[i] );
      if ( outer.size() >= 4 )
      {
        rings.append( outer );
        for ( size_t j = 0; j < contours.size(); ++j )
        {
          if ( hierarchy[j][3] == static_cast<int>( i ) )
          {
            const QVector<QgsPointXY> hole = ringFromContour( contours[j] );
            if ( hole.size() >= 4 )
              rings.append( hole );
          }
        }
      }
    }
  }
  return rings;
}
} // namespace

RsRoiToolMagicWand::~RsRoiToolMagicWand()
{
  // #1791: stop the in-flight fill/read when the tool dies; the landing is
  // dropped by the QPointer anyway, this ends the work itself (#797).
  if ( m_scanGeneration )
    sicnu::app::RsScanPool::instance().cancel( m_scanGeneration, this );
}

void RsRoiToolMagicWand::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  // #1791: snapshot everything the fill needs as PLAIN VALUES on the GUI
  // thread (click in map coords, raster path, tolerance, class id), then
  // run open + windowed read + flood fill + contouring on the bounded scan
  // pool. The old path did all of it inside this event and froze the canvas
  // for hundreds of ms - seconds on large local or any remote source.
  if ( !e || mRasterPath.isEmpty() )
    return;

  const QgsPointXY map = toMapCoordinates( e->pos() );
  const QString rasterPath = mRasterPath;
  const double tolerance = mTolerance;
  const int classId = mClassId;
  const quint64 requestId = ++m_requestEpoch;
  const quint64 scanGeneration =
    sicnu::app::RsScanPool::instance().nextGeneration( this );
  m_scanGeneration = scanGeneration;
  QPointer<RsRoiToolMagicWand> self( this );

  sicnu::app::RsScanPool::instance().pool().start(
    [self, rasterPath, map, tolerance, classId, requestId, scanGeneration]() {
      WandResult result;
      result.classId = classId;

      // Cooperative cancellation: a newer click or tool death ends the read
      // at this seam instead of finishing stale work.
      if ( sicnu::app::RsScanPool::instance().isStale( scanGeneration, self.data() ) )
        return;

      GDALAllRegister();
      auto *ds = static_cast<GDALDataset *>( GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly ) );
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

      // Convert map coords -> raster pixel via inverse GT.
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
      const QSet<quint64> localPixels = RsFloodFill::run( img, localSr, localSc, tolerance );
      if ( localPixels.isEmpty() )
        return;

      for ( quint64 i : localPixels )
      {
        const int lr = static_cast<int>( i / quint64( winW ) );
        const int lc = static_cast<int>( i % quint64( winW ) );
        const int r = lr + rowStart;
        const int c = lc + colStart;
        result.pixels.insert( static_cast<quint64>( r ) * static_cast<quint64>( W ) + static_cast<quint64>( c ) );
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
          result.clipped = true;
          break;
        }
      }

      // True region rings; bbox kept only as a fallback for degenerate fills.
      result.rings = regionRings( localPixels, winW, winH, rowStart, colStart, gt );
      if ( result.rings.isEmpty() )
      {
        quint64 rMin = std::numeric_limits<quint64>::max();
        quint64 cMin = std::numeric_limits<quint64>::max();
        quint64 rMax = 0;
        quint64 cMax = 0;
        for ( quint64 i : result.pixels )
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
        result.useBboxFallback = true;
        result.bboxMinX = *std::min_element( cx, cx + 4 );
        result.bboxMinY = *std::min_element( cy, cy + 4 );
        result.bboxMaxX = *std::max_element( cx, cx + 4 );
        result.bboxMaxY = *std::max_element( cy, cy + 4 );
      }

      result.ok = true;
      result.pixelsList.reserve( result.pixels.size() );
      for ( quint64 i : result.pixels )
        result.pixelsList.append( i );

      // One marshalled hop onto the GUI thread; dropped when the tool (or
      // its request) is already gone.
      QMetaObject::invokeMethod( qApp, [self, requestId, result]() {
        if ( self )
          self->landWandResult( requestId, result );
      } );
    } );
}

void RsRoiToolMagicWand::landWandResult( quint64 requestId, const WandResult &result )
{
  if ( requestId != m_requestEpoch )
    return; // superseded by a newer click

  if ( !result.ok )
    return;

  QgsGeometry geom;
  if ( result.useBboxFallback )
  {
    geom = QgsGeometry::fromRect(
      QgsRectangle( result.bboxMinX, result.bboxMinY, result.bboxMaxX, result.bboxMaxY ) );
  }
  else if ( !result.rings.isEmpty() )
  {
    QgsPolygonXY polygon;
    for ( const auto &ring : result.rings )
      polygon.append( ring );
    geom = QgsGeometry::fromPolygonXY( polygon );
  }
  if ( geom.isNull() || geom.isEmpty() )
    return;

  if ( result.clipped )
    emit regionClipped();

  emit roiDrawnPixels( geom, result.classId, result.pixelsList );
  emit roiDrawn( geom, result.classId );
}
