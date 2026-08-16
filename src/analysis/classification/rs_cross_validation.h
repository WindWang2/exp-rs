// rs_cross_validation.h — Phase 10A.1.2: stratified k-fold cross validation.
//
// Splits samples into k folds round-robin per class so each fold preserves
// the original class proportions. Classes with fewer than k samples are
// kept in the train set across every fold (their test contribution is
// empty). A deterministic mt19937 seed makes results reproducible.
//
// Feature scaling mirrors the Apply pipeline: per fold, RsFeatureScaler is
// fit on that fold's trainX only, then train and test are transformed.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"

#include <opencv2/core.hpp>

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class QGIS_ANALYSIS_EXPORT RsCrossValidation
{
  public:
    struct Result
    {
      double meanAccuracy = 0.0;
      double stdAccuracy = 0.0;
      QVector<double> foldAccuracies;
      QString errorMessage;
      bool ok() const { return errorMessage.isEmpty(); }
    };

    /// Stratified k-fold CV.
    /// factory() instantiates a fresh backend per fold.
    /// Returns per-fold accuracies + mean + std.
    /// Classes with < k samples are kept in train for every fold
    /// (their test contribution is empty).
    /// When \a scaleFeatures is true (default), each fold fits a scaler on
    /// train only and transforms train/test before fit/predict — same as Apply.
    /// \a isCanceled, if set, is checked between folds; returns cancelled result.
    static Result kFold( const cv::Mat &X, const cv::Mat &y,
                         std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                         int k = 5,
                         bool scaleFeatures = true,
                         std::function<bool()> isCanceled = nullptr,
                         unsigned int seed = 42u );
};
