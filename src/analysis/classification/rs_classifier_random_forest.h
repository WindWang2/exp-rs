// rs_classifier_random_forest.h — OBIA Random Forest classifier backend.
//
// Wraps cv::ml::RTrees and reports *true* per-class probabilities derived from
// the random forest's tree votes (RTrees::getVotes), normalised by the total
// tree count. No synthetic 0.9/0.1 fallback heuristic is applied (per
// .scratch/obia-classification-optimization/spec.md §Solution).
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_cv_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsRandomForestBackend : public RsClassifierCvBackend<cv::ml::RTrees>
{
  public:
    explicit RsRandomForestBackend( int numTrees = 100, int maxDepth = 10, int minSampleCount = 5 );

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predictProbabilities( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "RandomForest (随机森林)" ); }

  private:
    /// Distinct training class IDs in sorted order. RTrees::getVotes reports
    /// vote counts per class but not the class labels themselves, so we capture
    /// them at fit() time to map columns back to true class IDs.
    cv::Mat mClassLabels;
};
