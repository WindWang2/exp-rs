// Phase 10A Task 10.6 — Jeffries-Matusita separability metric.
//
// JM distance is a bounded ([0, 2]) measure of statistical separability
// between two multivariate Gaussian-modeled class distributions, derived
// from the Bhattacharyya distance B as JM = 2 * (1 - exp(-B)).
//
// Used by the classification GUI to populate the N x N separability matrix
// dock so analysts can prune overlapping ROI classes before training.
//
// ADR 0055: computeAll() is the GUI's only seam for the full matrix — it
// consumes RsTrainingDataExtraction output (X + y) directly, so the
// scanline-grouped reads and NoData / ignore-value filtering of the classify
// path apply to JM statistics too (previously the GUI hand-read 1×1 pixels
// and let NoData leak into the per-class buckets).
#pragma once

#include "qgis_analysis_export.h"

#include <QHash>
#include <QPair>

#include <opencv2/core.hpp>

/**
 * \brief Pairwise Jeffries-Matusita separability for multivariate samples.
 *
 * The static \a pairJm() helper accepts two row-sample matrices (each row is
 * one observation; columns are spectral bands) and returns JM in [0, 2].
 * Identical distributions yield ~0; completely separated distributions
 * approach 2. A small (1e-6) ridge is added to the diagonal of each
 * covariance matrix so degenerate / small-sample inputs do not produce
 * singular matrices.
 */
class QGIS_ANALYSIS_EXPORT RsJmSeparability
{
  public:
    /**
     * Compute the Jeffries-Matusita distance between two sample sets.
     *
     * \param xA Row-sample matrix for class A (N_A rows x d cols).
     * \param xB Row-sample matrix for class B (N_B rows x d cols).
     * \return JM distance in [0, 2]; returns 0 if either matrix is empty.
     */
    static double pairJm( const cv::Mat &xA, const cv::Mat &xB );

    /**
     * Full pairwise JM matrix from training-extraction output.
     *
     * \param X NxB CV_32F samples, \param y Nx1 CV_32S class ids — exactly
     * the shape RsTrainingDataExtraction::extract() returns, already
     * NoData/ignore-filtered.
     * \return map keyed by qMakePair( a, b ) with a < b for every class pair
     * having at least two samples (a covariance needs two observations;
     * single-sample classes are skipped, matching the pre-ADR-0055 GUI).
     */
    static QHash<QPair<int, int>, double> computeAll( const cv::Mat &X,
                                                      const cv::Mat &y );
};
