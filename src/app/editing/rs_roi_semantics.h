// rs_roi_semantics.h — F11 Package D: ROI ↔ raster sample semantics.
//
// The editing-side contract that turns a drawn ROI into raster truth:
//   * CRS: geometry arrives in \a geomCrs and is transformed to the raster
//     CRS first. TRANSFORMS FAIL CLOSED — an unusable transform returns an
//     error, never raw untransformed coordinates (in-scope application of
//     the fail-closed lesson from out-of-scope issue #1005).
//   * Footprint: center-of-pixel membership via RsPixelRasterizer — the
//     existing analysis-layer oracle (no second rasterizer here). The
//     rasterization runs on a bounded WINDOW around the ROI (bbox ∩ raster
//     extent), never on the full raster.
//   * NoData/edge: pixels outside the raster extent are not covered; the
//     reported pixelCount counts covered pixels inside the extent and
//     validPixelCount counts those that also carry data.
//   * Stats preview: per-band min/max/mean/std over valid pixels, computed
//     from windowed block reads; cancelable; refuses ROIs above maxPixels
//     (fail-closed: no partial statistics are ever returned).
//   * Class write-back: explicit edit-command-wrapped attribute write.
#pragma once

#include <QVector>
#include <functional>

#include <qgsfeatureid.h>
#include <qgsgeometry.h>

class QgsCoordinateReferenceSystem;
class QgsRasterLayer;
class QgsVectorLayer;
class RsEditSession;

struct RsRoiBandStats
{
    int band = 1;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double stddev = 0.0; // population stddev over valid pixels
};

struct RsRoiStatsResult
{
    bool ok = false;
    QString error;                 // fail-closed reason when ok == false
    qlonglong pixelCount = 0;      // covered pixels inside the raster extent
    qlonglong validPixelCount = 0; // covered pixels that carry data (band 1 mask)
    QVector<RsRoiBandStats> bands;
};

class RsRoiSemantics
{
  public:
    /// Default ROI pixel budget for previews (PERFORMANCE: larger previews
    /// must opt in explicitly).
    static constexpr qlonglong kDefaultMaxPixels = 4'000'000;

    /// Transform \a geometry from \a geomCrs to the raster's CRS.
    /// Returns an empty error string on success and writes \a out.
    static QString transformToRasterCrs( const QgsGeometry &geometry,
                                         const QgsCoordinateReferenceSystem &geomCrs,
                                         const QgsRasterLayer *raster,
                                         QgsGeometry *out );

    /// Per-band statistics preview over the ROI. \a isCanceled is polled
    /// during pixel iteration; cancellation returns ok=false with
    /// error "canceled". ROIs covering more than \a maxPixels are refused.
    static RsRoiStatsResult bandStatsPreview(
      QgsRasterLayer *raster,
      const QgsGeometry &roiInGeomCrs,
      const QgsCoordinateReferenceSystem &geomCrs,
      const std::function<bool()> &isCanceled = {},
      qlonglong maxPixels = kDefaultMaxPixels,
      const QVector<int> &bandNumbers = { 1 } );

    /// Set the class label attribute of one sample feature inside a single
    /// edit command (one undo step). \a classField must exist; fail-closed
    /// otherwise. \a session is optional but, when set, refuses locked or
    /// unattached layers.
    static bool writeClassLabel( QgsVectorLayer *samples, QgsFeatureId fid,
                                 int classId, const QString &classField,
                                 RsEditSession *session = nullptr,
                                 QString *error = nullptr );
};
