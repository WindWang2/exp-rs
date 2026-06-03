#include "rs_sift_matcher.h"

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
  GDALAllRegister();
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

  cv::Mat fullGray( H, W, CV_8UC1 );
  CPLErr err = ds->GetRasterBand( 1 )->RasterIO(
    GF_Read, 0, 0, W, H,
    fullGray.data, W, H, GDT_Byte, 0, 0 );
  GDALClose( ds );
  if ( err != CE_None )
    return {};

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
  return r;
#endif
}
