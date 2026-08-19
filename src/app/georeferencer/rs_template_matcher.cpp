#include "rs_template_matcher.h"

#include "core/sicnu_logging.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "qgsfeedback.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <vector>

#include <gdal_priv.h>

#ifdef SICNU_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

RsTemplateMatcher::RsTemplateMatcher( QgsFeedback *fb )
  : mFb( fb )
{
}

namespace {

bool hasUsableGeoTransform( const double gt[6] )
{
  // Identity (pixel coords as "geo") is not useful for search-region prediction.
  const bool nearIdentity =
    std::abs( gt[0] ) < 1e-9 && std::abs( gt[3] ) < 1e-9
    && std::abs( gt[1] - 1.0 ) < 1e-9 && std::abs( gt[5] - 1.0 ) < 1e-9
    && std::abs( gt[2] ) < 1e-12 && std::abs( gt[4] ) < 1e-12;
  if ( nearIdentity )
    return false;
  // Degenerate pixel size
  const double det = gt[1] * gt[5] - gt[2] * gt[4];
  return std::abs( det ) > 1e-18;
}

void pixelToWorld( const double gt[6], double col, double row, double &x, double &y )
{
  x = gt[0] + gt[1] * col + gt[2] * row;
  y = gt[3] + gt[4] * col + gt[5] * row;
}

bool worldToPixel( const double gt[6], double x, double y, double &col, double &row )
{
  const double det = gt[1] * gt[5] - gt[2] * gt[4];
  if ( std::abs( det ) < 1e-18 )
    return false;
  const double dx = x - gt[0];
  const double dy = y - gt[3];
  col = ( gt[5] * dx - gt[2] * dy ) / det;
  row = ( -gt[4] * dx + gt[1] * dy ) / det;
  return true;
}

int makeOdd( int v )
{
  v = std::max( 9, v );
  if ( v % 2 == 0 )
    ++v;
  return v;
}

#ifdef SICNU_HAS_OPENCV
cv::Mat readBandWindow( GDALDataset *ds, int x0, int y0, int w, int h )
{
  if ( !ds || w <= 0 || h <= 0 )
    return {};
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  if ( x0 < 0 || y0 < 0 || x0 + w > W || y0 + h > H )
    return {};

  GDALRasterBand *band = ds->GetRasterBand( 1 );
  const GDALDataType dt = band ? band->GetRasterDataType() : GDT_Byte;
  if ( dt == GDT_Byte )
  {
    cv::Mat out( h, w, CV_8UC1 );
    const CPLErr err = band->RasterIO( GF_Read, x0, y0, w, h, out.data, w, h, GDT_Byte, 0, 0 );
    if ( err != CE_None )
      return {};
    return out;
  }
  cv::Mat f( h, w, CV_32FC1 );
  CPLErr err = band->RasterIO( GF_Read, x0, y0, w, h, f.data, w, h, GDT_Float32, 0, 0 );
  if ( err != CE_None )
    return {};
  const int N = w * h;
  float *ptr = reinterpret_cast<float *>( f.data );
  // Fast percentile stretch per window (subsample if large)
  std::vector<float> sample;
  sample.reserve( std::min( N, 20000 ) );
  const int step = std::max( 1, N / 20000 );
  for ( int i = 0; i < N; i += step )
  {
    const float v = ptr[i];
    if ( std::isfinite( v ) )
      sample.push_back( v );
  }
  if ( sample.empty() )
    return cv::Mat( h, w, CV_8UC1, cv::Scalar( 0 ) );
  std::sort( sample.begin(), sample.end() );
  double p2 = sample[sample.size() * 2 / 100];
  double p98 = sample[std::min( sample.size() * 98 / 100, sample.size() - 1 )];
  if ( p98 <= p2 )
  {
    p2 = sample.front();
    p98 = sample.back();
  }
  if ( p98 <= p2 ) p98 = p2 + 1.0;
  cv::Mat out( h, w, CV_8UC1 );
  const double scale = 255.0 / ( p98 - p2 );
  for ( int i = 0; i < N; ++i )
  {
    float v = ptr[i];
    if ( !std::isfinite( v ) ) v = static_cast<float>( p2 );
    double nv = ( v - p2 ) * scale;
    if ( nv < 0 ) nv = 0;
    if ( nv > 255 ) nv = 255;
    out.data[i] = static_cast<uchar>( std::lround( nv ) );
  }
  return out;
}
#endif

} // namespace

RsTemplateMatcher::Result RsTemplateMatcher::run( const QString &srcRaster,
                                                    const QString &refRaster,
                                                    const QgsCoordinateReferenceSystem &/*refCrs*/,
                                                    const Params &paramsIn,
                                                    const QVector<QgsPointXY> &seedSrcPixels )
{
  Result r;
  Params params = paramsIn;
  params.templateSize = makeOdd( params.templateSize );
  params.searchRadiusPx = std::max( params.templateSize, params.searchRadiusPx );
  params.gridRows = std::max( 1, params.gridRows );
  params.gridCols = std::max( 1, params.gridCols );
  params.edgeMargin = std::max( params.templateSize / 2 + 1, params.edgeMargin );

  SICNU_LOG_INFO( SicnuLogTags::Georeferencing,
                  QStringLiteral( "Template matching started: src=%1 ref=%2 mode=%3" )
                    .arg( QFileInfo( srcRaster ).fileName(),
                          QFileInfo( refRaster ).fileName(),
                          params.seedMode == SeedMode::Grid ? QStringLiteral( "grid" )
                                                            : QStringLiteral( "seeds" ) ) );

  if ( !QFileInfo::exists( srcRaster ) )
  {
    r.errorMessage = QStringLiteral( "Source raster not found: %1" ).arg( srcRaster );
    return r;
  }
  if ( !QFileInfo::exists( refRaster ) )
  {
    r.errorMessage = QStringLiteral( "Reference raster not found: %1" ).arg( refRaster );
    return r;
  }

#ifndef SICNU_HAS_OPENCV
  Q_UNUSED( seedSrcPixels );
  r.errorMessage = QStringLiteral( "OpenCV not available at build time" );
  return r;
#else
  ensureGdalInit();
  GDALDataset *srcDs = static_cast<GDALDataset *>(
    GDALOpen( srcRaster.toUtf8().constData(), GA_ReadOnly ) );
  GDALDataset *refDs = static_cast<GDALDataset *>(
    GDALOpen( refRaster.toUtf8().constData(), GA_ReadOnly ) );
  if ( !srcDs || !refDs )
  {
    if ( srcDs )
      GDALClose( srcDs );
    if ( refDs )
      GDALClose( refDs );
    r.errorMessage = QStringLiteral( "Failed to open SRC or REF raster" );
    return r;
  }

  double srcGt[6] = { 0, 1, 0, 0, 0, 1 };
  double refGt[6] = { 0, 1, 0, 0, 0, 1 };
  srcDs->GetGeoTransform( srcGt );
  refDs->GetGeoTransform( refGt );

  const int srcW = srcDs->GetRasterXSize();
  const int srcH = srcDs->GetRasterYSize();
  const int refW = refDs->GetRasterXSize();
  const int refH = refDs->GetRasterYSize();

  if ( params.requireSrcGeo && !hasUsableGeoTransform( srcGt ) )
  {
    GDALClose( srcDs );
    GDALClose( refDs );
    r.errorMessage = QStringLiteral(
      "SRC 缺少可用的初始地理变换（GeoTransform）。"
      "模板匹配依赖初始坐标预测搜索区；请先为源影像指定近似 CRS/地理参考，"
      "或先手工打若干粗 GCP 后使用「现有种子点」模式。" );
    return r;
  }
  if ( !hasUsableGeoTransform( refGt ) )
  {
    GDALClose( srcDs );
    GDALClose( refDs );
    r.errorMessage = QStringLiteral( "REF 缺少可用的地理变换，无法将匹配点转为地面坐标。" );
    return r;
  }

  QVector<QgsPointXY> seeds = seedSrcPixels;
  if ( params.seedMode == SeedMode::Grid || seeds.isEmpty() )
  {
    seeds.clear();
    const int margin = params.edgeMargin;
    if ( srcW <= 2 * margin || srcH <= 2 * margin )
    {
      GDALClose( srcDs );
      GDALClose( refDs );
      r.errorMessage = QStringLiteral( "SRC 影像过小，无法生成网格种子点" );
      return r;
    }
    for ( int gy = 0; gy < params.gridRows; ++gy )
    {
      for ( int gx = 0; gx < params.gridCols; ++gx )
      {
        const double fx = ( params.gridCols == 1 )
                            ? 0.5
                            : double( gx ) / double( params.gridCols - 1 );
        const double fy = ( params.gridRows == 1 )
                            ? 0.5
                            : double( gy ) / double( params.gridRows - 1 );
        const double col = margin + fx * ( srcW - 1 - 2 * margin );
        const double row = margin + fy * ( srcH - 1 - 2 * margin );
        seeds.push_back( QgsPointXY( col, row ) );
      }
    }
  }

  const int halfT = params.templateSize / 2;
  const int searchHalf = params.searchRadiusPx;
  r.attempted = int( seeds.size() );

  for ( int i = 0; i < seeds.size(); ++i )
  {
    if ( mFb && mFb->isCanceled() )
    {
      r.errorMessage = QStringLiteral( "cancelled" );
      break;
    }
    if ( mFb )
      mFb->setProgress( 100.0 * double( i ) / double( std::max( 1, int( seeds.size() ) ) ) );

    const double sx = seeds[i].x();
    const double sy = seeds[i].y();
    const int sc = int( std::lround( sx ) );
    const int sr = int( std::lround( sy ) );
    if ( sc < halfT || sr < halfT || sc + halfT >= srcW || sr + halfT >= srcH )
      continue;

    cv::Mat templ = readBandWindow( srcDs, sc - halfT, sr - halfT,
                                    params.templateSize, params.templateSize );
    if ( templ.empty() )
      continue;

    double wx = 0, wy = 0;
    pixelToWorld( srcGt, sx, sy, wx, wy );
    double predCol = 0, predRow = 0;
    if ( !worldToPixel( refGt, wx, wy, predCol, predRow ) )
      continue;

    const int pc = int( std::lround( predCol ) );
    const int pr = int( std::lround( predRow ) );
    const int x0 = pc - searchHalf;
    const int y0 = pr - searchHalf;
    const int sw = 2 * searchHalf + 1;
    const int sh = 2 * searchHalf + 1;

    // Clip search window to REF bounds
    const int rx0 = std::max( 0, x0 );
    const int ry0 = std::max( 0, y0 );
    const int rx1 = std::min( refW, x0 + sw );
    const int ry1 = std::min( refH, y0 + sh );
    const int rw = rx1 - rx0;
    const int rh = ry1 - ry0;
    if ( rw < params.templateSize || rh < params.templateSize )
      continue;

    cv::Mat search = readBandWindow( refDs, rx0, ry0, rw, rh );
    if ( search.empty() )
      continue;

    cv::Mat response;
    cv::matchTemplate( search, templ, response, cv::TM_CCOEFF_NORMED );
    double minV = 0, maxV = 0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc( response, &minV, &maxV, &minLoc, &maxLoc );
    if ( maxV < params.minScore )
      continue;

    // Peak is top-left of template in the (clipped) search image
    const double matchCol = rx0 + maxLoc.x + halfT;
    const double matchRow = ry0 + maxLoc.y + halfT;

    double mx = 0, my = 0;
    pixelToWorld( refGt, matchCol, matchRow, mx, my );

    Match m;
    m.srcPx = QgsPointXY( sx, sy );
    m.dstWorld = QgsPointXY( mx, my );
    m.score = maxV;
    r.matches.push_back( m );
    ++r.accepted;
  }

  GDALClose( srcDs );
  GDALClose( refDs );

  if ( !r.errorMessage.isEmpty() )
    return r;

  if ( r.matches.isEmpty() )
  {
    r.errorMessage = QStringLiteral(
      "未找到满足阈值的匹配点。可增大搜索半径、降低最小相关分数，"
      "或检查 SRC 初始坐标是否大致正确。" );
    return r;
  }

  if ( mFb )
    mFb->setProgress( 100.0 );
  return r;
#endif
}
