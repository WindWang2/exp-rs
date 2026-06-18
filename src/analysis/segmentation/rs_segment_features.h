// rs_segment_features.h — Phase 10B Task 10B.1: per-segment feature extraction.
//
// Extracts spectral statistics (mean, stddev, min, max per band) and shape
// descriptors (area, perimeter, shape index) from a raster + segment map.
// Output is a QMap<segmentId, SegmentStat> and can be converted to an
// OpenCV Mat for classification.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

#include <QMap>
#include <QVector>

#ifdef SICNU_HAS_OPENCV
#include <opencv2/core.hpp>
#endif

class QGIS_ANALYSIS_EXPORT RsSegmentFeatures
{
  public:
    struct SegmentStat
    {
        QVector<double> mean;     // per-band mean
        QVector<double> stddev;   // per-band standard deviation
        QVector<double> min;      // per-band minimum
        QVector<double> max;      // per-band maximum
        double area = 0;          // pixel count
        double perimeter = 0;     // boundary pixel count
        double shapeIndex = 0;    // perimeter / (4 * sqrt(area))
    };

    /// Extract features for all segments from a raster.
    /// bandIndices: 1-based GDAL band numbers to read.
    static QMap<quint32, SegmentStat> extract(
        const QString &rasterPath,
        const RsSegmentMap &segMap,
        const QVector<int> &bandIndices );

#ifdef SICNU_HAS_OPENCV
    /// Convert segment stats to a feature matrix (rows = segments, cols = features).
    /// segmentIds: output — the segment ID for each row.
    static cv::Mat toFeatureMatrix(
        const QMap<quint32, SegmentStat> &stats,
        QVector<quint32> &segmentIds );
#endif

  private:
    static double computeShapeIndex( double area, double perimeter );
};
