// rs_probability_calibration.h — Classification & Object Intelligence 11.0 (F12).
//
// Post-hoc probability calibration for class-probability / decision-score
// matrices, plus reliability metrics (Brier, ECE, reliability bins).
//
// Contract (see DECISIONS D-004/D-005):
//   * The calibrator is fitted OUTSIDE the classifier on a held-out
//     calibration set: raw scores (or already-produced probabilities)
//     of shape N x K plus their true labels. No classifier is modified.
//   * Column order authority is RsClassOrder: column k refers to
//     classIds[k], classIds strictly ascending.
//   * Per class c a binary sub-problem is built with target
//     t = [label == classIds[c]].
//   * Platt: logistic regression on the 1-D score, p = 1/(1+exp(A*s+B)),
//     fitted by Newton iterations (deterministic, no RNG).
//   * Isotonic: pool-adjacent-violators on (score, target); application is
//     monotone piecewise-linear interpolation between knot midpoints.
//   * apply() normalises each output row to sum 1; a row whose transform
//     sums to <= 0 (possible only for isotonic all-zero outputs) falls back
//     to the uniform 1/K distribution (documented, not silent corruption).
//
// All fits are fail-closed: degenerate input (mismatched sizes, non-finite
// values, a class with no positive or no negative calibration member)
// returns false and leaves the model unusable (isValid() == false).
#pragma once

#include "qgis_analysis_export.h"

#include <QJsonObject>
#include <QVector>

#include <span>
#include <vector>

class QGIS_ANALYSIS_EXPORT RsCalibrationModel
{
  public:
    enum class Method
    {
      None = 0,
      Platt,
      Isotonic,
    };

    bool isValid() const { return method != Method::None && !classIds.isEmpty(); }

    /// Serialises {version, method, classIds, platt{a[],b[]} | isotonic{x[][],y[][]}}.
    QJsonObject toJson() const;
    /// Inverse of toJson(); returns false and resets state on any
    /// structural/monotonicity violation.
    bool fromJson( const QJsonObject &obj );

    Method method = Method::None;
    /// Strictly ascending class ids — probability column order (RsClassOrder).
    QVector<int> classIds;
    /// Platt per-class parameters: p = 1 / (1 + exp(A * s + B)).
    QVector<double> plattA;
    QVector<double> plattB;
    /// Isotonic per-class knots: monotone rates at knot scores. Knot x
    /// vectors are strictly ascending; y vectors non-decreasing in [0,1].
    QVector<QVector<double>> isotonicX;
    QVector<QVector<double>> isotonicY;
};

class QGIS_ANALYSIS_EXPORT RsProbabilityCalibrator
{
  public:
    /// Fit per-class Platt sigmoids. \a rawScores is row-major N x K
    /// (column k ↔ classIds[k]); \a labels has N entries drawn from classIds.
    /// \a maxIter caps Newton iterations (default 100); convergence uses the
    /// gradient norm. Returns false (model untouched) on degenerate input.
    static bool fitPlatt( std::span<const float> rawScores,
                          int sampleCount,
                          const QVector<int> &classIds,
                          std::span<const int> labels,
                          int maxIter,
                          RsCalibrationModel &outModel );

    /// Fit per-class isotonic calibrators via pool-adjacent-violators.
    static bool fitIsotonic( std::span<const float> rawScores,
                             int sampleCount,
                             const QVector<int> &classIds,
                             std::span<const int> labels,
                             RsCalibrationModel &outModel );

    /// Apply a fitted model to raw scores (row-major N x K), producing
    /// calibrated, row-normalised probabilities (row-major N x K).
    /// Returns false when the model is invalid, sizes mismatch, or any
    /// input value is non-finite. Output rows always sum to 1.
    static bool apply( const RsCalibrationModel &model,
                       std::span<const float> rawScores,
                       int sampleCount,
                       std::vector<float> &outProbs );
};

/// Reliability report for an already-normalised probability matrix.
class QGIS_ANALYSIS_EXPORT RsCalibrationMetrics
{
  public:
    struct Bin
    {
      double meanConfidence = 0.0;  ///< mean top-class probability in bin
      double empiricalAccuracy = 0.0; ///< fraction of top-class-correct in bin
      int count = 0;
    };

    struct Report
    {
      bool ok = false;
      QVector<int> classIds;
      double brier = 0.0;   ///< mean over samples of Σ_k (p_k − 1{k=y})²
      double ece = 0.0;     ///< Σ_bins (count/N)·|accuracy − confidence|
      double logLoss = 0.0; ///< mean −ln p_true (clamped at 1e-12)
      int binCount = 0;
      QVector<Bin> bins;    ///< equal-width bins over top-class confidence [0,1]
      QJsonObject toJson() const;
    };

    /// \a probs is row-major N x K with rows summing to 1 (tolerance 1e-2;
    /// anything else fails closed). Bins use \a binCount equal-width edges.
    static Report compute( std::span<const int> labels,
                           std::span<const float> probs,
                           int sampleCount,
                           const QVector<int> &classIds,
                           int binCount = 10 );
};
