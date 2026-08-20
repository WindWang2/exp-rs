// rs_classifier_kmeans.h — Phase 10A Task 10.8.
//
// Wrapper around cv::kmeans for unsupervised clustering. fit() ignores the
// supplied y matrix for training; it inspects y only to decide whether the
// arbitrary 1..K cluster ids must be remapped onto real class ids (see
// needsLabelRemap). predict() assigns each sample to the nearest cluster
// centroid (Euclidean), returning 1-based class IDs so the output integrates
// with the same ColorTable scheme used for supervised classifiers.
#pragma once

#include "rs_classifier_backend.h"

class QGIS_ANALYSIS_EXPORT RsClassifierKMeans : public RsClassifierBackend
{
  public:
    explicit RsClassifierKMeans( int k = 3 );

    bool fit( const cv::Mat &X, const cv::Mat &y ) override; // y ignored for training
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "K-Means" ); }
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;
    bool isFitted() const override { return !m_centers.empty(); }
    /// Cluster ids are arbitrary 1..K, so when the backend was trained with
    /// real class labels the pipeline must align them via the Hungarian
    /// remap (ADR 0061). Trained with an all-zero / empty label matrix (the
    /// unsupervised operator's dummy y) there are no true labels to align
    /// to and the raw cluster ids must survive verbatim.
    bool needsLabelRemap() const override { return m_remapNeeded; }

    void setK( int k ) { m_k = k; }
    int k() const { return m_k; }
    cv::Mat centers() const { return m_centers; }

  private:
    int m_k;
    bool m_remapNeeded = false;
    cv::Mat m_centers; // K x bands, CV_32F
};
