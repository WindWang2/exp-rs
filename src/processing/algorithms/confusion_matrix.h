// src/processing/algorithms/confusion_matrix.h — D15 Package F public seam.
//
// Accuracy assessment: K x K confusion matrix (row = ground truth,
// col = prediction) with OA, Cohen's kappa, per-class producer/user accuracy
// and F1, plus a streaming tile-accumulation path that is bit-identical to
// the single-shot compute().  Samples whose class is not listed in
// targetClasses are excluded from the evaluation entirely.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rs::processing
{

struct ClassAccuracyStats
{
    int classId{ 0 };
    std::string className;
    int64_t groundTruthTotal{ 0 }; // row marginal  n_i+
    int64_t predictedTotal{ 0 };   // column marginal n_+i
    int64_t truePositives{ 0 };    // diagonal     n_ii
    double producerAccuracy{ 0.0 }; // recall    n_ii / n_i+
    double userAccuracy{ 0.0 };     // precision n_ii / n_+i
    double f1Score{ 0.0 };          // 2 n_ii / (n_i+ + n_+i)
};

struct EvaluationMetrics
{
    std::vector<std::vector<int64_t>> matrix; // [row=truth][col=pred], targetClasses order
    std::vector<int> classLabels;             // == deduplicated targetClasses
    int64_t totalSampleCount{ 0 };
    double overallAccuracy{ 0.0 };
    double cohensKappa{ 0.0 };
    double macroF1Score{ 0.0 };
    std::vector<ClassAccuracyStats> perClassStats;
};

class ConfusionMatrixEvaluator
{
  public:
    /// Single-shot evaluation over paired label vectors (sizes must match,
    /// otherwise an empty metrics object is returned).
    static EvaluationMetrics compute( std::span<const int> groundTruth,
                                      std::span<const int> predictions,
                                      const std::vector<int> &targetClasses );

    /// Adds one tile's votes into @p inOutMatrix (sized
    /// targetClasses.size() x targetClasses.size(), zero-initialized).
    static void accumulateTile( std::vector<std::vector<int64_t>> &inOutMatrix,
                                std::span<const int> gtTile,
                                std::span<const int> predTile,
                                const std::vector<int> &targetClasses );

    /// Derives all metrics from a accumulated matrix.  Guards: N == 0 ->
    /// all-zero metrics; po == 1 -> kappa == 1 (even when pe degenerates);
    /// pe == 1 otherwise -> kappa == 0; zero marginals -> 0 accuracies.
    static EvaluationMetrics finalize( const std::vector<std::vector<int64_t>> &matrix,
                                       const std::vector<int> &targetClasses );
};

} // namespace rs::processing
