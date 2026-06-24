// rs_classifier_kmeans.h — Phase 10A Task 10.8.
//
// Wrapper around cv::kmeans for unsupervised clustering. fit() ignores the
// supplied y matrix. predict() assigns each sample to the nearest cluster
// centroid (Euclidean), returning 1-based class IDs so the output integrates
// with the same ColorTable scheme used for supervised classifiers.
#pragma once

#include "rs_classifier_backend.h"

class QGIS_ANALYSIS_EXPORT RsClassifierKMeans : public RsClassifierBackend
{
  public:
    explicit RsClassifierKMeans( int k = 3 );

    bool fit( const cv::Mat &X, const cv::Mat &y ) override; // y ignored
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "K-Means" ); }
    bool isFitted() const override { return !m_centers.empty(); }

    void setK( int k ) { m_k = k; }
    int k() const { return m_k; }
    cv::Mat centers() const { return m_centers; }

  private:
    int m_k;
    cv::Mat m_centers; // K x bands, CV_32F
};
