#ifndef RS_TEMPLATE_MATCHER_H
#define RS_TEMPLATE_MATCHER_H

#include <QString>
#include <QVector>

#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"

class QgsFeedback;

/**
 * Template (NCC) matching between SRC and REF rasters using approximate
 * SRC georeferencing to define REF search windows.
 *
 * Typical RS case: SRC has a rough GeoTransform (RPC-approx, quick-look geo,
 * previous registration). For each seed SRC pixel:
 *   1. Extract a template patch from SRC
 *   2. Project seed → world via SRC GT → predicted REF pixel via REF GT
 *   3. Search a window around that prediction with cv::matchTemplate (CCOEFF_NORMED)
 *   4. Accept peaks above minScore as refined GCPs
 *
 * Requires OpenCV at build time (same as SIFT). Without OpenCV, run() returns
 * a graceful error.
 */
class RsTemplateMatcher
{
  public:
    enum class SeedMode
    {
      Grid,           //!< Regular grid on SRC (needs usable SRC geotransform)
      ExistingSeeds   //!< Caller-supplied SRC pixel seeds (e.g. rough GCPs)
    };

    struct Params
    {
      SeedMode seedMode = SeedMode::Grid;
      int templateSize = 65;      //!< Odd preferred; clamp to odd internally
      int searchRadiusPx = 96;    //!< Extra pixels beyond template/2 on each side in REF
      double minScore = 0.75;     //!< TM_CCOEFF_NORMED threshold [0,1]
      int gridRows = 5;
      int gridCols = 5;
      int edgeMargin = 32;        //!< Keep seeds away from SRC edges
      bool requireSrcGeo = true;  //!< Fail grid mode if SRC GT is identity/missing
    };

    struct Match
    {
      QgsPointXY srcPx;
      QgsPointXY dstWorld;
      double score = 0.0;
    };

    struct Result
    {
      QVector<Match> matches;
      int attempted = 0;
      int accepted = 0;
      QString errorMessage;
      bool ok() const { return errorMessage.isEmpty(); }
    };

    explicit RsTemplateMatcher( QgsFeedback *fb = nullptr );

    /**
     * \a seedSrcPixels used only when params.seedMode == ExistingSeeds.
     * Pixel coords are original SRC resolution (not downsampled).
     */
    Result run( const QString &srcRaster,
                const QString &refRaster,
                const QgsCoordinateReferenceSystem &refCrs,
                const Params &params,
                const QVector<QgsPointXY> &seedSrcPixels = {} );

  private:
    QgsFeedback *mFb = nullptr;
};

#endif // RS_TEMPLATE_MATCHER_H
