// rs_classifier_normalbayes.h — Phase 10A Task 10.8.
//
// Wrapper around cv::ml::NormalBayesClassifier — the OpenCV equivalent of the
// classical Maximum Likelihood Classifier widely taught in remote-sensing
// courses. Assumes a multivariate Gaussian PDF per class.
//
// Classification & Object Intelligence 11.0: probability columns follow the
// RsClassOrder contract — ascending distinct training labels, captured at
// fit() and persisted next to the model as `<path>.labels.json`
// (bare array, same companion format as RF/MLP).
#pragma once

#include "rs_classifier_cv_backend.h"

#include <QVector>

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierNormalBayes : public RsClassifierCvBackend<cv::ml::NormalBayesClassifier>
{
  public:
    RsClassifierNormalBayes();

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predictProbabilities( const cv::Mat &X ) const override;
    bool predictWithProbabilities( const cv::Mat &X, cv::Mat &outLabels,
                                   cv::Mat &outProbs ) const override;

    bool supportsProbabilities() const override { return true; }
    QVector<int> classOrder() const override { return mClassLabels; }

    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;

    QString name() const override { return QStringLiteral( "NormalBayes (最大似然)" ); }

  private:
    /// Ascending distinct training labels — probability column order.
    QVector<int> mClassLabels;
};
