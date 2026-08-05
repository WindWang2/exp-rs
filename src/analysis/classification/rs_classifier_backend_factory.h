// rs_classifier_backend_factory.h — ADR 0061: single owner of
// method-name → backend construction. Backend construction previously lived
// in three byte-identical `makeBackend` copies in the operators plus the
// pipeline's predict-only "bayes" sniff; all four paths now construct here
// so name matching and hyperparameters cannot drift (hyperparameters stay in
// the analysis-layer backend classes).
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"

#include <QString>

#include <memory>

class QGIS_ANALYSIS_EXPORT RsClassifierBackendFactory
{
  public:
    /// Map a method-name string to a backend. Matching is case-insensitive
    /// substring matching that preserves the historical sniffs:
    ///   * contains "bayes"  → RsClassifierNormalBayes
    ///   * contains "kmeans" → RsClassifierKMeans
    ///   * anything else     → RsClassifierSvm (fallback)
    /// The "bayes" rule reproduces the pipeline predict-only sidecar sniff
    /// and the adapters' canonical vocabulary ("svm" / "normal_bayes").
    /// "kmeans" strings now construct the K-Means backend (previously the
    /// fallback SVM); K-Means has no load(), so a predict-only reload still
    /// fails cleanly with Error::ModelOpenFailed.
    static std::unique_ptr<RsClassifierBackend> create( const QString &methodName );

    /// K-Means with an explicit cluster count (operator path).
    static std::unique_ptr<RsClassifierBackend> createKMeans( int k );
};
