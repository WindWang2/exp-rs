// rs_roi_labeler.h — ADR 0060: canonical ROI-majority segment labeling.
//
// Labels segments of an RsSegmentMap from training polygons with an integer
// class field. One owner replaces the rs:obia_hierarchy labelFromRoi helper
// (point-in-polygon at pixel centers) and the rs:obia_classify votes loop
// (full-raster ALL_TOUCHED mask); both now converge here.
//
// Canonical pixel-membership rule: center-of-pixel rasterize via
// RsPixelRasterizer — the same analysis-layer rasterizer the training-sample
// extraction module uses, with windowed allocation and QGIS-geometry
// clipping. It matches the hierarchy path's pixel-center semantics (a
// boundary pixel belongs to the polygon whose interior contains its center)
// and retires ALL_TOUCHED, which double-counted pixels on shared boundaries.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

#include <QMap>
#include <QString>

#include <functional>

class QGIS_ANALYSIS_EXPORT RsRoiLabeler
{
  public:
    /// Label segments by pixel majority of training polygons.
    ///
    /// \a classField uses the shared fallback chain (classField → "class" →
    /// "id", RsTrainingDataExtraction::classFieldIndex); features with
    /// class id <= 0 or null geometry are skipped. Each segment receives the
    /// class id with the most covered pixels (ties → smaller id,
    /// majorityKeyWithTieBreak); segments with fewer than \a minLabelPixels
    /// ROI pixels stay unlabeled (absent from the result).
    ///
    /// The raster's geotransform (from \a rasterPath, which must cover the
    /// same grid as \a segMap) georeferences the polygons; fail-closed: on a
    /// missing raster/vector, an unresolvable class field, or a degenerate
    /// geotransform, the result is empty and \a error is set.
    ///
    /// \a isCanceled is polled once per training feature; when it turns true
    /// the call returns early with an empty map and \a error = "canceled".
    static QMap<quint32, int> labelByMajority( const RsSegmentMap &segMap,
                                               const QString &rasterPath,
                                               const QString &trainingPath,
                                               const QString &classField,
                                               int minLabelPixels,
                                               QString *error = nullptr,
                                               const std::function<bool()> &isCanceled = {} );
};
