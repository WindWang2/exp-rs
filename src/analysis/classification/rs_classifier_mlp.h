// rs_classifier_mlp.h — OBIA Artificial Neural Network (cv::ml::ANN_MLP)
// classifier backend.
//
// ANN_MLP emits a raw [N × numClasses] activation matrix, not class ids, so
// this backend overrides both fit() (to one-hot encode labels into a stable
// column order and capture that order) and predict() (to argmax the output
// back onto true class ids). predictProbabilities() softmaxes the activations.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_cv_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsMlpBackend : public RsClassifierCvBackend<cv::ml::ANN_MLP>
{
  public:
    explicit RsMlpBackend( int hiddenLayerSize = 16, int maxIter = 500 );

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    cv::Mat predictProbabilities( const cv::Mat &X ) const override;
    bool supportsProbabilities() const override { return true; }
    QString name() const override { return QStringLiteral( "Neural Network (MLP)" ); }

  private:
    int mHiddenLayerSize = 16;
    /// Distinct training class ids in ascending order; column c of the ANN's
    /// output corresponds to mClassLabels[c]. Captured at fit() time.
    cv::Mat mClassLabels;
};
