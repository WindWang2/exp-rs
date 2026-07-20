// rs_accuracy_assessment.h — Phase 10A Task 10.9.
//
// Confusion-matrix-based accuracy assessment for pixel classification.
// Given paired (yTrue, yPred) integer class-ID vectors the static
// compute() returns:
//
//   confusion       — n x n CV_32S matrix (rows = true, cols = pred)
//   classIds        — sorted unique class IDs observed in either vector
//   overallAccuracy — sum(diag) / total
//   kappa           — Cohen's Kappa, with po==1.0 guard returning 1.0
//   producerAcc     — per-class TP / row-sum    (recall; omission complement)
//   userAcc         — per-class TP / column-sum (precision; commission complement)
//   f1              — harmonic mean of producer and user accuracy
//
// Empty / size-mismatched input yields an empty Result with all metrics
// at their default-initialised zero values; this is a safe no-op for the
// classification task's optional accuracy block.
#pragma once

#include "qgis_analysis_export.h"

#include <QHash>
#include <QVector>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsAccuracyAssessment
{
  public:
    struct Result
    {
      cv::Mat confusion;                 // CV_32S, rows = true, cols = pred
      QVector<int> classIds;             // sorted, deduplicated
      double overallAccuracy = 0.0;
      double kappa = 0.0;
      QHash<int, double> producerAcc;
      QHash<int, double> userAcc;
      QHash<int, double> f1;
    };

    /**
     * Compute the confusion matrix and accuracy metrics for paired class-ID
     * vectors. Returns a default-constructed Result when inputs are empty
     * or sizes mismatch.
     */
    static Result compute( const QVector<int> &yTrue,
                           const QVector<int> &yPred );
};
