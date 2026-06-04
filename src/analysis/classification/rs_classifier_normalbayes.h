// rs_classifier_normalbayes.h — Phase 10A Task 10.8.
//
// Wrapper around cv::ml::NormalBayesClassifier — the OpenCV equivalent of the
// classical Maximum Likelihood Classifier widely taught in remote-sensing
// courses. Assumes a multivariate Gaussian PDF per class.
#pragma once

#include "rs_classifier_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierNormalBayes : public RsClassifierBackend
{
  public:
    RsClassifierNormalBayes();

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "NormalBayes (最大似然)" ); }
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;
    bool isFitted() const override { return mClf && mClf->isTrained(); }

  private:
    cv::Ptr<cv::ml::NormalBayesClassifier> mClf;
};
