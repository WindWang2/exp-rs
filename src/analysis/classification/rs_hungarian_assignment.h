// rs_hungarian_assignment.h — Phase 10A.1.1 / 10A.3.
//
// Munkres O(n^3) Hungarian algorithm for min-cost assignment.
// Accepts square or rectangular cost matrices (n true classes × m clusters).
// Used by the classification pipeline to remap K-Means cluster IDs to ROI
// class IDs before accuracy assessment.
#pragma once

#include "qgis_analysis_export.h"

#include <QVector>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsHungarianAssignment
{
  public:
    /**
     * Solve min-cost assignment on an n×m cost matrix (n and m may differ).
     *
     * Internally pads to sz = max(n, m) with kPadCost = 1e9 so pad columns/rows
     * are never chosen unless no real alternative exists.
     *
     * Returns vector of length n (original rows): assign[row] = chosen column
     * in [0, m), or -1 if that row was matched to a pad column (only possible
     * when n > m).
     *
     * Cost matrix must be CV_64F or CV_32F. Empty input → empty output.
     */
    static QVector<int> solve( const cv::Mat &costMatrix );
};
