#include "rs_sift_matcher.h"

#include "core/sicnu_logging.h"
#include <QFileInfo>

#include "qgsfeedback.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef SICNU_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#endif

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal_priv.h>

RsSiftMatcher::RsSiftMatcher( QgsFeedback *fb )
  : mFb( fb )
{
}

#ifdef SICNU_HAS_OPENCV
namespace
{
//! Read band 1 from a raster, optionally downsample so max(W,H) <= maxSide.
//! Returns scale factor (dstSize / srcSize) and writes the 6-tuple geotransform.
cv::Mat readGdalGray( const QString &path, int maxSide, double &scaleOut, double gt[6] )
{
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return {};

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  // Default identity geotransform if absent (still safe to use).
  gt[0] = 0.0; gt[1] = 1.0; gt[2] = 0.0;
  gt[3] = 0.0; gt[4] = 0.0; gt[5] = 1.0;
  ds->GetGeoTransform( gt );

  const int target = std::max( W, H );
  scaleOut = ( target > maxSide ) ? double( maxSide ) / double( target ) : 1.0;
  const int dstW = std::max( 1, int( std::round( W * scaleOut ) ) );
  const int dstH = std::max( 1, int( std::round( H * scaleOut ) ) );

  GDALRasterBand *band = ds->GetRasterBand( 1 );
  const GDALDataType dt = band ? band->GetRasterDataType() : GDT_Byte;
  cv::Mat fullGray;
  // Decimated read (#631): request the WORKING resolution directly from
  // GDAL instead of decoding the full-resolution band and resizing after -
  // a 50k x 50k scene previously materialized 2.5-10 GB transiently on the
  // SIFT worker before the downscale threw it away.
  if ( dt == GDT_Byte )
  {
    fullGray.create( dstH, dstW, CV_8UC1 );
    CPLErr err = band->RasterIO( GF_Read, 0, 0, W, H,
                                 fullGray.data, dstW, dstH, GDT_Byte, 0, 0 );
    GDALClose( ds );
    if ( err != CE_None )
      return {};
  }
  else
  {
    cv::Mat fullFloat( dstH, dstW, CV_32FC1 );
    CPLErr err = band->RasterIO( GF_Read, 0, 0, W, H,
                                 fullFloat.data, dstW, dstH, GDT_Float32, 0, 0 );
    GDALClose( ds );
    if ( err != CE_None )
      return {};
    // Percentile stretch [p2, p98] -> [0,255] to handle UInt16 1000..8500 without clamping.
    const int N = static_cast<int>( fullFloat.total() );
    const float *ptr = reinterpret_cast<float *>( fullFloat.data );
    std::vector<float> sample;
    sample.reserve( std::min( N, 100000 ) );
    // Subsample for speed if large
    const int step = std::max( 1, N / 100000 );
    for ( int i = 0; i < N; i += step )
    {
      const float v = ptr[i];
      if ( std::isfinite( v ) )
        sample.push_back( v );
    }
    if ( sample.empty() )
      return cv::Mat( dstH, dstW, CV_8UC1, cv::Scalar( 0 ) );
    std::sort( sample.begin(), sample.end() );
    const size_t p2Idx = sample.size() * 2 / 100;
    const size_t p98Idx = sample.size() * 98 / 100;
    double p2 = sample[p2Idx];
    double p98 = sample[std::min( p98Idx, sample.size() - 1 )];
    if ( p98 <= p2 )
    {
      p2 = sample.front();
      p98 = sample.back();
    }
    if ( p98 <= p2 )
    {
      p98 = p2 + 1.0;
    }
    fullGray.create( fullFloat.rows, fullFloat.cols, CV_8UC1 );
    const double scale = 255.0 / ( p98 - p2 );
    for ( int i = 0; i < N; ++i )
    {
      float v = ptr[i];
      if ( !std::isfinite( v ) ) v = static_cast<float>( p2 );
      double nv = ( v - p2 ) * scale;
      if ( nv < 0 ) nv = 0;
      if ( nv > 255 ) nv = 255;
      fullGray.data[i] = static_cast<uchar>( std::lround( nv ) );
    }
  }

  if ( dstW == W && dstH == H )
    return fullGray;

  cv::Mat scaled;
  cv::resize( fullGray, scaled, cv::Size( dstW, dstH ), 0, 0, cv::INTER_AREA );
  return scaled;
}
} // namespace
#endif // SICNU_HAS_OPENCV

RsSiftMatcher::Result RsSiftMatcher::run( const QString &srcRaster,
                                          const QString &refRaster,
                                          const QgsCoordinateReferenceSystem &/*refCrs*/,
                                          const Params &params )
{
  Result r;
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "SIFT matching started: src=%1 ref=%2" )
    .arg( QFileInfo( srcRaster ).fileName(), QFileInfo( refRaster ).fileName() ) );

  // Fast-fail: missing files. This works without OpenCV so the error path is testable.
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
  Q_UNUSED( params );
  r.errorMessage = QStringLiteral( "OpenCV not available at build time" );
  return r;
#else
  double srcScale = 1.0;
  double refScale = 1.0;
  double srcGt[6] = { 0, 1, 0, 0, 0, 1 };
  double refGt[6] = { 0, 1, 0, 0, 0, 1 };

  cv::Mat src = readGdalGray( srcRaster, params.maxImageSide, srcScale, srcGt );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }
  cv::Mat ref = readGdalGray( refRaster, params.maxImageSide, refScale, refGt );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }
  if ( src.empty() || ref.empty() )
  {
    r.errorMessage = QStringLiteral( "Failed to read one of the rasters" );
    return r;
  }
  if ( mFb ) mFb->setProgress( 25.0 );

  cv::Ptr<cv::SIFT> sift = cv::SIFT::create( 0, 3, params.contrastThreshold );
  std::vector<cv::KeyPoint> kpSrc, kpRef;
  cv::Mat descSrc, descRef;
  sift->detectAndCompute( src, cv::noArray(), kpSrc, descSrc );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }

  sift->detectAndCompute( ref, cv::noArray(), kpRef, descRef );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }
  if ( mFb ) mFb->setProgress( 50.0 );

  if ( descSrc.empty() || descRef.empty() )
  {
    r.errorMessage = QStringLiteral( "No descriptors found" );
    return r;
  }

  cv::BFMatcher matcher( cv::NORM_L2, /*crossCheck=*/true );
  std::vector<cv::DMatch> matches;
  matcher.match( descSrc, descRef, matches );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }
  if ( mFb ) mFb->setProgress( 75.0 );

  r.totalMatches = static_cast<int>( matches.size() );
  if ( matches.size() < 4 )
  {
    r.errorMessage = QStringLiteral( "Too few matches for RANSAC" );
    return r;
  }

  // Sort by ascending distance so we keep the best maxMatches.
  std::sort( matches.begin(), matches.end(),
             []( const cv::DMatch &a, const cv::DMatch &b ) { return a.distance < b.distance; } );
  if ( static_cast<int>( matches.size() ) > params.maxMatches )
    matches.resize( params.maxMatches );

  std::vector<cv::Point2f> srcPts, refPts;
  srcPts.reserve( matches.size() );
  refPts.reserve( matches.size() );
  for ( const auto &m : matches )
  {
    srcPts.push_back( kpSrc[m.queryIdx].pt );
    refPts.push_back( kpRef[m.trainIdx].pt );
  }
  std::vector<uchar> mask;
  cv::findHomography( srcPts, refPts, cv::RANSAC, params.ransacThreshold, mask );
  if ( mFb && mFb->isCanceled() ) { r.errorMessage = QStringLiteral( "cancelled" ); return r; }
  if ( mFb ) mFb->setProgress( 100.0 );

  int inlierCount = 0;
  for ( size_t i = 0; i < mask.size(); ++i )
  {
    if ( !mask[i] )
      continue;
    Match mm;
    // Back to original SRC pixel coords (undo downsample).
    mm.srcPx = QgsPointXY( srcPts[i].x / srcScale, srcPts[i].y / srcScale );
    // REF pixel -> REF world via GeoTransform.
    const double refPxX = refPts[i].x / refScale;
    const double refPxY = refPts[i].y / refScale;
    const double worldX = refGt[0] + refGt[1] * refPxX + refGt[2] * refPxY;
    const double worldY = refGt[3] + refGt[4] * refPxX + refGt[5] * refPxY;
    mm.dstWorld = QgsPointXY( worldX, worldY );
    mm.distance = matches[i].distance;
    r.inliers.append( mm );
    ++inlierCount;
  }
  r.inlierRatio = matches.empty()
                    ? 0.0
                    : double( inlierCount ) / double( matches.size() );
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "SIFT matching completed: %1 matches, %2 inliers (%3%)" )
    .arg( r.totalMatches ).arg( inlierCount ).arg( static_cast<int>( r.inlierRatio * 100 ) ) );
  return r;
#endif
}
