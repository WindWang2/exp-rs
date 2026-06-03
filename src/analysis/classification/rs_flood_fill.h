// Phase 10A Task 10.7 — Tolerance-based flood fill for the magic-wand ROI tool.
//
// 4-connected BFS that visits pixels whose L2 spectral distance to the seed
// pixel is strictly less than `tolerance`. Operates on any multi-band float
// image (CV_32FC<B>) and returns linear pixel indices (row * cols + col).
#pragma once

#include "qgis_analysis_export.h"

#include <QSet>
#include <cstdint>
#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsFloodFill
{
  public:
    /**
     * \brief 4-connected flood fill starting at (seedRow, seedCol).
     *
     * \param multibandFloat input multi-band image (must be CV_32F depth).
     * \param seedRow        seed row index.
     * \param seedCol        seed column index.
     * \param tolerance      strict upper bound on L2 spectral distance from seed.
     * \returns set of linear indices (row * cols + col) of pixels in the fill.
     *
     * Returns empty if the image is empty, the seed is out of bounds, or the
     * image depth is not CV_32F.
     */
    static QSet<quint64> run( const cv::Mat &multibandFloat,
                              int seedRow, int seedCol, double tolerance );
};
