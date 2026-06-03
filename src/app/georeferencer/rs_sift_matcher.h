#ifndef RS_SIFT_MATCHER_H
#define RS_SIFT_MATCHER_H

#include <QString>
#include <QVector>

#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

class QgsFeedback;

/**
 * OpenCV-based SIFT keypoint matcher between two rasters.
 *
 * Pipeline (Task 11.5.7):
 *   1. GDAL read band 1 of SRC + REF (grayscale, downsampled to maxImageSide).
 *   2. cv::SIFT::create(...) detect-and-compute on each.
 *   3. cv::BFMatcher(NORM_L2, crossCheck=true) match descriptors.
 *   4. cv::findHomography(RANSAC) inlier filter.
 *   5. Back-project surviving keypoints to original SRC pixel coords + REF world coords
 *      (via REF GeoTransform).
 *
 * If built without OpenCV (SICNU_HAS_OPENCV undefined), run() returns an empty
 * Result with errorMessage="OpenCV not available at build time".
 */
class RsSiftMatcher
{
  public:
    struct Params
    {
        double contrastThreshold = 0.04;   //!< SIFT contrast threshold (lower = more keypoints)
        int    maxMatches        = 100;    //!< Cap on matches kept before RANSAC
        double minInlierRatio    = 0.5;    //!< Informational (caller may filter on this)
        double ransacThreshold   = 3.0;    //!< Pixel reprojection tolerance for findHomography
        int    maxImageSide      = 2048;   //!< Downscale so max(W,H) <= this value
    };

    struct Match
    {
        QgsPointXY srcPx;        //!< Pixel coords in source raster (original resolution)
        QgsPointXY dstWorld;     //!< World coords in REF CRS (via REF GeoTransform)
        double     distance = 0.0;
    };

    struct Result
    {
        QVector<Match> inliers;
        int            totalMatches = 0;
        double         inlierRatio  = 0.0;
        QString        errorMessage;
        bool ok() const { return errorMessage.isEmpty(); }
    };

    explicit RsSiftMatcher( QgsFeedback *fb = nullptr );

    Result run( const QString &srcRaster,
                const QString &refRaster,
                const QgsCoordinateReferenceSystem &refCrs,
                const Params &params );

  private:
    QgsFeedback *mFb = nullptr;
};

#endif // RS_SIFT_MATCHER_H
