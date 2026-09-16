// rs_spatial_cross_validation.h — Classification & Object Intelligence 11.0 (F12).
//
// Spatially aware k-fold cross validation for sample matrices, with an
// explicit leakage audit (GOAL Oracle 1: a synthetic spatial leak must be
// caught).
//
// Why: classic stratified k-fold shuffles spatially clustered samples into
// train/test, so autocorrelated data inflates accuracy estimates. This seam
// generates folds that respect space:
//
//   groupFolds(k)          — group ids define folds (whole groups move
//                            together); groups smaller than the fold count
//                            degrade by assignment, never by splitting.
//   blockFolds(nx, ny)     — the bounding box of the sample coordinates is
//                            partitioned into an nx×ny grid of blocks; each
//                            block is one fold.
//   bufferedBlockFolds(nx, ny, bufferDistance) — block folds where training
//                            samples closer than bufferDistance to the
//                            fold's test block are EXCLUDED (reported as
//                            excludedBuffer). Isolation invariant mirrors
//                            src/core/spatial_split.h:
//                            ||p_train − p_eval|| > bufferDistance for
//                            every retained train/test pair.
//
// The audit records, per fold, the minimum retained train↔test coordinate
// distance (Euclidean, same unit as the coordinates) and the train/test
// group overlap count. evaluate() then runs per-fold fit/predict exactly
// like RsCrossValidation::kFold (per-fold scaler, deterministic seed,
// cooperative cancel) and reports fold accuracies + the audit.
//
// Decoupled on purpose from the D19 dataset platform (no storage, no
// catalog types): pure matrices, coordinates and group ids (DECISIONS D-009).
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"

#include <opencv2/core.hpp>

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class QGIS_ANALYSIS_EXPORT RsSpatialCrossValidation
{
  public:
    /// One generated fold: index sets over the sample rows.
    struct Fold
    {
      QVector<int> testIndices;
      QVector<int> trainIndices;
      /// Train samples removed by the buffer rule (bufferedBlockFolds only;
      /// empty otherwise). Never part of train or test.
      QVector<int> excludedBuffer;
    };

    /// Per-fold leakage audit record.
    struct FoldAudit
    {
      /// Minimum Euclidean distance between any retained train sample and
      /// any test sample (infinity when either side is empty).
      double minTrainTestDistance = 0.0;
      /// Number of distinct group ids appearing on BOTH sides (0 when no
      /// group ids were supplied). Non-zero is a leak signal.
      int groupOverlap = 0;
    };

    /// Audit over all folds of one CV run.
    struct AuditReport
    {
      QVector<FoldAudit> folds;
      /// True when every fold has minTrainTestDistance > 0 (no coincident
      /// train/test points) and no group overlap. Coincident points are the
      /// classic spatial-leak signature this audit exists to catch.
      bool spatiallyClean() const;
      /// Smallest per-fold minimum distance (infinity when no folds).
      double overallMinDistance() const;
    };

    struct Result
    {
      double meanAccuracy = 0.0;
      double stdAccuracy = 0.0;
      QVector<double> foldAccuracies;
      AuditReport audit;
      QString errorMessage;
      bool ok() const { return errorMessage.isEmpty(); }
    };

    /// Group folds: each group is one atomic unit. Groups are greedily
    /// assigned to the currently smallest fold in descending-size order
    /// (deterministic; ties by ascending group id). Needs >= k distinct
    /// groups.
    static QVector<Fold> groupFolds( const std::vector<int> &groupIds, int k );

    /// Block folds: bounding-box grid partition of \a coords (x,y pairs,
    /// row-major xyxy…, length 2N). Blocks with no samples are skipped
    /// (they carry no test samples); needs >= k non-empty blocks.
    static QVector<Fold> blockFolds( const std::vector<double> &coords,
                                     int sampleCount, int nx, int ny );

    /// Buffered block folds: block folds plus exclusion of train samples
    /// closer than \a bufferDistance to the fold's test samples.
    /// bufferDistance must be > 0 (otherwise plain blockFolds applies).
    /// O(N_test × N_train) per fold by design — audit-grade exactness.
    static QVector<Fold> bufferedBlockFolds( const std::vector<double> &coords,
                                             int sampleCount, int nx, int ny,
                                             double bufferDistance );

    /// Convenience: plain random stratified folds over samples for
    /// comparison runs (the baseline a spatial audit should beat/flag).
    /// Deterministic mt19937(\a seed) shuffle per class, round-robin
    /// assignment to folds.
    static QVector<Fold> randomFolds( const cv::Mat &y, int k, unsigned int seed = 42u );

    /// Runs k-fold CV with pre-generated folds. Each fold fits a fresh
    /// backend (factory) on its train set (optional per-fold scaler, fitted
    /// on the fold's train rows only) and evaluates on its test set; the
    /// audit is computed on the retained index sets. Returns a cancelled
    /// result when \a isCanceled fires between folds.
    static Result evaluate( const cv::Mat &X,
                            const cv::Mat &y,
                            const std::vector<double> &coords,
                            const std::vector<int> &groupIds,
                            const QVector<Fold> &folds,
                            std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                            bool scaleFeatures = true,
                            std::function<bool()> isCanceled = nullptr );
};
