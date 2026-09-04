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

/// Optional per-method hyperparameters (ADR 0061 extension). Fields irrelevant
/// to the resolved method are ignored; the in-struct defaults are the backend
/// constructor defaults and the single source of truth for operator schemas
/// and GUI parameter dialogs.
struct QGIS_ANALYSIS_EXPORT RsClassifierBackendParams
{
  int rfNumTrees = 100;
  int rfMaxDepth = 10;
  int rfMinSampleCount = 5;
  int mlpHiddenLayerSize = 16;
  int mlpMaxIter = 500;
};

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

    /// Same name resolution with explicit hyperparameters (RandomForest /
    /// MLP). Callers that tune backends (operator schema params, the OBIA
    /// GUI Params dialog) must come through this overload so backend
    /// construction stays in one place — no direct backend constructors at
    /// call sites.
    static std::unique_ptr<RsClassifierBackend> create(
      const QString &methodName,
      const RsClassifierBackendParams &params );

    /// K-Means with an explicit cluster count (operator path).
    static std::unique_ptr<RsClassifierBackend> createKMeans( int k );
};
