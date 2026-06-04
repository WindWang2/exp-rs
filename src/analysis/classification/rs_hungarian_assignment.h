// rs_hungarian_assignment.h — Phase 10A.1.1.
//
// Munkres O(n^3) Hungarian algorithm for min-cost assignment on a square
// cost matrix. Used by RsClassificationTask to remap K-Means cluster IDs
// to ROI class IDs before accuracy assessment.
#pragma once

#include "qgis_analysis_export.h"

#include <QVector>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsHungarianAssignment
{
  public:
    /**
     * Solve min-cost assignment on an N×N cost matrix.
     * Returns vector of length N: assign[row] = chosen column.
     * Cost matrix must be CV_64F or CV_32F. Empty input → empty output.
     * Non-square input is also rejected (returns empty).
     */
    static QVector<int> solve( const cv::Mat &costMatrix );
};
