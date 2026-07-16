// rs_hungarian_assignment.h — Phase 10A.1.1.
//
// Munkres O(n^3) Hungarian algorithm for min-cost assignment.
// Accepts rectangular cost matrices by padding to square with a large
// finite pad cost, then mapping assignments back to original columns.
// Used by RsClassificationTask to remap K-Means cluster IDs to ROI
// class IDs before accuracy assessment.
#pragma once

#include "qgis_analysis_export.h"

#include <QVector>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsHungarianAssignment
{
  public:
    /**
     * Solve min-cost assignment on an N×M cost matrix (rectangular OK).
     * Returns vector of length N (original rows): assign[row] = chosen
     * column in [0, M), or -1 if the row was matched only to a pad column
     * (more rows than columns).
     * Cost matrix must be CV_64F or CV_32F. Empty input → empty output.
     */
    static QVector<int> solve( const cv::Mat &costMatrix );
};
