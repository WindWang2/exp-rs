// rs_classifier_svm.h — Phase 10A Task 10.8.
//
// Wrapper around cv::ml::SVM configured for C_SVC + RBF kernel.
// Defaults: C=10, γ=0.5 (educational defaults; cross-validated grid search
// is left to a v2 task).
#pragma once

#include "rs_classifier_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierSvm : public RsClassifierBackend
{
  public:
    RsClassifierSvm();

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "SVM (RBF)" ); }
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;
    bool isFitted() const override { return mClf && mClf->isTrained(); }

  private:
    cv::Ptr<cv::ml::SVM> mClf;
};
