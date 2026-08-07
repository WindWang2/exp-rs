// rs_classifier_normalbayes.h — Phase 10A Task 10.8.
//
// Wrapper around cv::ml::NormalBayesClassifier — the OpenCV equivalent of the
// classical Maximum Likelihood Classifier widely taught in remote-sensing
// courses. Assumes a multivariate Gaussian PDF per class.
#pragma once

#include "rs_classifier_cv_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierNormalBayes : public RsClassifierCvBackend<cv::ml::NormalBayesClassifier>
{
  public:
    RsClassifierNormalBayes();

    cv::Mat predictProbabilities( const cv::Mat &X ) const override;

    bool supportsProbabilities() const override { return true; }

    QString name() const override { return QStringLiteral( "NormalBayes (最大似然)" ); }
};
