// rs_classifier_statistical.h — classical statistical classifiers
// (Foundation 5.0, Milestone H): minimum distance and Mahalanobis.
//
// Own implementations over cv::Mat (no cv::ml dependency): training computes
// per-class statistics; prediction is closed-form scoring.
//
//   minimum_distance — nearest class mean (Euclidean).
//   mahalanobis      — nearest class mean under the pooled within-class
//                      covariance (ridge 1e-6·trace/d on the diagonal for
//                      singular small-sample classes; classes with < 2
//                      samples reuse the pooled covariance).
//
// Both are deterministic; predictProbabilities is unsupported (documented).
#pragma once

#include "rs_classifier_backend.h"

#include <QString>

#include <opencv2/core.hpp>

#include <vector>

class QGIS_ANALYSIS_EXPORT RsClassifierMinDistance : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "MinimumDistance" ); }
    bool isFitted() const override { return !m_means.empty(); }

  private:
    std::vector<int> m_classIds;
    std::vector<cv::Mat> m_means; // 1×bands CV_64F per class
};

class QGIS_ANALYSIS_EXPORT RsClassifierMahalanobis : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "Mahalanobis" ); }
    bool isFitted() const override { return !m_means.empty(); }

  private:
    std::vector<int> m_classIds;
    std::vector<cv::Mat> m_means;      // 1×bands CV_64F per class
    cv::Mat m_pooledInverse;           // bands×bands CV_64F
};
