// rs_training_data_extraction.h — ADR 0019 slice S1: unified training-sample
// extraction ("ROIs/geometries + raster → X/y") for the classification
// pipeline.
//
// Deep, GUI-free module (no QWidget / QApplication assumptions, synchronous)
// consumed by two thin adapters:
//   * QgsClassificationMainWindow::buildTrainingData — in-memory ROIs.
//   * RsSupervisedClassificationOperator — OGR vector + class field.
//
// Semantics (superset of the two former call sites):
//   * Overlapping geometries dedup by pixel index, last class wins.
//   * Geometries are rasterized with the windowed RsPixelRasterizer (never a
//     full W×H mask); pre-computed pixel-index caches are honored as-is.
//   * Band values are read with scanline-grouped RasterIO (one row read per
//     unique sample row per band, not 1×1 reads).
//   * RsPixelIgnoreOptions drop NoData / user-ignore samples.
//   * Optional per-class cap with deterministic std::mt19937(42) subsampling.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_pixel_ignore_options.h"
#include "qgsgeometry.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <ogr_api.h>

#include <opencv2/core.hpp>

#include <cstdint>
#include <functional>

/**
 * One training geometry with its class id.
 *
 * \a pixelIndices is an optional cache (row * width + col, as produced by
 * RsRoi); when empty the geometry is rasterized on demand.
 */
struct QGIS_ANALYSIS_EXPORT RsTrainingGeometry
{
    int classId = 0;
    QgsGeometry geometry;
    QVector<quint64> pixelIndices;
};

struct QGIS_ANALYSIS_EXPORT RsTrainingDataResult
{
    /// Machine-readable failure reason, so adapters can keep their own
    /// externally-visible error codes/messages.
    enum class Error
    {
      None = 0,
      RasterOpenFailed,
      InvalidBand,
      RasterReadFailed,
      VectorOpenFailed,
      VectorNoLayers,
      ClassFieldNotFound,
      NoValidPixels,
      InsufficientSamples,
      Cancelled,
    };

    bool ok = false;
    Error error = Error::None;
    QString errorMessage;      ///< Human-readable detail, set when !ok.
    cv::Mat X;                 ///< CV_32F NxB (rows = samples, cols = bands).
    cv::Mat y;                 ///< CV_32S Nx1, aligned with X.
    QHash<int, int> classCounts; ///< classId → kept sample count.
    int featuresRead = 0;      ///< OGR features visited (vector path only).
};

class QGIS_ANALYSIS_EXPORT RsTrainingDataExtraction
{
  public:
    /**
     * Progress/cancel sink. Called with a fraction in [0,1] and a message;
     * return false to cancel (extraction then fails with Error::Cancelled).
     * May also throw (e.g. an operator context bridging throwIfCancelled).
     */
    using Progress = std::function<bool( double fraction, const QString &message )>;

    struct Options
    {
        Options();

        RsPixelIgnoreOptions ignore;
        /// Cap kept samples per class (0 = unlimited). Deterministic
        /// subsampling with std::mt19937(42) + std::shuffle.
        int maxSamplesPerClass;
        /// Fail with Error::InsufficientSamples when fewer samples survive.
        int minSamples;
        /// Deterministic pseudo-random seed for per-class subsampling.
        unsigned int seed = 42u;
    };

    /// In-memory geometry path (classification window ROIs).
    static RsTrainingDataResult extract( const QString &rasterPath,
                                         const QVector<int> &bands,
                                         const QVector<RsTrainingGeometry> &geometries,
                                         const Options &options = Options(),
                                         const Progress &progress = Progress() );

    /**
     * Resolve the class id field index on an OGR feature definition:
     * \a classField first, falling back to "class" then "id". Returns -1
     * when no candidate matches. Shared by extractFromVector() and thin
     * adapters that open the training vector themselves (e.g. the OBIA
     * operator's segment majority labeling) so the fallback chain lives in
     * exactly one place (ADR 0019 S4).
     */
    static int classFieldIndex( OGRFeatureDefnH defn, const QString &classField );

    /**
     * OGR vector path. The class id field is \a classField with fallback to
     * "class" then "id"; features with class id <= 0 or null geometry are
     * skipped.
     */
    static RsTrainingDataResult extractFromVector( const QString &rasterPath,
                                                   const QVector<int> &bands,
                                                   const QString &vectorPath,
                                                   const QString &classField,
                                                   const Options &options = Options(),
                                                   const Progress &progress = Progress() );
};
