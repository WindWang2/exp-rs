// rs_classifier_isodata.h — Scientific Algorithms 7.0 (capability package E).
//
// ISODATA (Ball & Hall 1965) unsupervised clustering: k-means style iterative
// assignment with the classic self-organising corrections — clusters smaller
// than a minimum sample count are discarded, over-dispersed clusters split
// along their most variable component, and centre pairs closer than a
// minimum distance merge. This is the deterministic, fixed-seed subset: the
// initial centres are evenly spaced rows of the training matrix (no random
// sampling), every comparison is fixed-order, and the result is bit-stable
// across runs. Like K-Means it emits arbitrary 1..K cluster ids (the
// pipeline remaps them via the Hungarian assignment, ADR 0061).
#pragma once

#include "qgis_analysis_export.h"

#include "rs_classifier_backend.h"

class QGIS_ANALYSIS_EXPORT RsClassifierIsodata : public RsClassifierBackend
{
  public:
    struct Params
    {
        int targetClusters = 2;       ///< split cap (classic K)
        int maxIterations = 25;       ///< assignment/update loop bound
        int minSamplesPerCluster = 5; ///< discard smaller clusters (classic Lmin... N/2 bound relaxed)
        double sigmaFactor = 1.0;     ///< split when a component std > factor·overall std
        double mergeDistance = 0.0;   ///< merge centre pairs closer than this (<=0 disables)
    };

    RsClassifierIsodata();
    explicit RsClassifierIsodata( const Params &params );

    bool fit( const cv::Mat &X, const cv::Mat &y ) override; // y ignored for training
    cv::Mat predict( const cv::Mat &X ) const override;
    QString name() const override { return QStringLiteral( "ISODATA" ); }
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;
    bool isFitted() const override { return !m_centers.empty(); }
    /// Unsupervised cluster ids — the pipeline must Hungarian-remap them
    /// onto real class ids when trained against true labels (see K-Means).
    bool needsLabelRemap() const override { return m_remapNeeded; }

    cv::Mat centers() const { return m_centers; }

  private:
    Params m_params;
    bool m_remapNeeded = false;
    cv::Mat m_centers; // K x bands, CV_32F (K may differ from targetClusters
                       // after discards/merges/splits)
};
