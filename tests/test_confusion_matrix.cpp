// tests/test_confusion_matrix.cpp — D15 Package F.
//
// Ground-truth policy: the canonical 3x3 matrix [[50,10,5],[8,60,12],
// [2,10,43]] has fully hand-computed metrics (derivations inline); edge
// expectations come from the closed-form kappa definition.  Nothing is
// recomputed through the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "processing/algorithms/confusion_matrix.h"

using Catch::Matchers::WithinAbs;
using rs::processing::ConfusionMatrixEvaluator;
using rs::processing::EvaluationMetrics;

namespace
{
  // Expands the canonical matrix into paired label vectors:
  //   rows: truth 0 => [50 x pred0, 10 x pred1, 5 x pred2], etc.
  struct Canonical
  {
    std::vector<int> gt, pred;
    static constexpr int kClassCount = 3;
    Canonical()
    {
      static const int counts[kClassCount][kClassCount] = {
        { 50, 10, 5 },
        { 8, 60, 12 },
        { 2, 10, 43 },
      };
      for ( int t = 0; t < kClassCount; ++t )
        for ( int p = 0; p < kClassCount; ++p )
          for ( int k = 0; k < counts[t][p]; ++k )
          {
            gt.push_back( t );
            pred.push_back( p );
          }
    }
  };
} // namespace

TEST_CASE( "Perfect diagonal gives OA = kappa = 1", "[d15][confusion]" )
{
  const std::vector<int> gt = { 0, 1, 1, 0 };
  const std::vector<int> pred = { 0, 1, 1, 0 };
  const std::vector<int> classes01 = { 0, 1 };
  const EvaluationMetrics m = ConfusionMatrixEvaluator::compute( gt, pred, classes01 );
  REQUIRE( m.totalSampleCount == 4 );
  REQUIRE_THAT( m.overallAccuracy, WithinAbs( 1.0, 1e-12 ) );
  REQUIRE_THAT( m.cohensKappa, WithinAbs( 1.0, 1e-12 ) );
}

TEST_CASE( "Canonical 3x3 matrix reproduces every hand-computed metric", "[d15][confusion]" )
{
  // N = 200; diagonal = 153 -> OA = 0.765.
  // row marginals (65, 80, 55); col marginals (60, 80, 60).
  // pe = (65*60 + 80*80 + 55*60)/40000 = 13600/40000 = 0.34
  // kappa = (0.765 - 0.34) / (1 - 0.34) = 0.425/0.66 = 0.6439393939...
  // class 0: PA = 50/65 = 0.7692307..., UA = 50/60 = 0.8333333...,
  //          F1 = 100/125 = 0.8
  // class 1: PA = UA = 60/80 = 0.75, F1 = 120/160 = 0.75
  // class 2: PA = 43/55 = 0.7818181..., UA = 43/60 = 0.7166666...,
  //          F1 = 86/115 = 0.7478260869...
  // macroF1 = (0.8 + 0.75 + 86.0/115.0) / 3 = 0.76594202898...
  const Canonical data;
  const std::vector<int> classes = { 0, 1, 2 };
  const EvaluationMetrics m = ConfusionMatrixEvaluator::compute( data.gt, data.pred, classes );

  REQUIRE( m.totalSampleCount == 200 );
  REQUIRE( m.classLabels == classes );
  REQUIRE( m.matrix.size() == 3 );
  REQUIRE_THAT( m.overallAccuracy, WithinAbs( 0.765, 1e-12 ) );
  REQUIRE_THAT( m.cohensKappa, WithinAbs( 0.425 / 0.66, 1e-12 ) );
  REQUIRE_THAT( m.macroF1Score, WithinAbs( ( 0.8 + 0.75 + 86.0 / 115.0 ) / 3.0, 1e-12 ) );

  // Raw matrix entries match the hand-written table.
  static const int64_t expected[3][3] = { { 50, 10, 5 }, { 8, 60, 12 }, { 2, 10, 43 } };
  for ( size_t i = 0; i < 3; ++i )
    for ( size_t j = 0; j < 3; ++j )
      REQUIRE( m.matrix[i][j] == expected[i][j] );

  const auto &s0 = m.perClassStats[0];
  REQUIRE( s0.classId == 0 );
  REQUIRE( s0.groundTruthTotal == 65 );
  REQUIRE( s0.predictedTotal == 60 );
  REQUIRE( s0.truePositives == 50 );
  REQUIRE_THAT( s0.producerAccuracy, WithinAbs( 50.0 / 65.0, 1e-12 ) );
  REQUIRE_THAT( s0.userAccuracy, WithinAbs( 50.0 / 60.0, 1e-12 ) );
  REQUIRE_THAT( s0.f1Score, WithinAbs( 0.8, 1e-12 ) );

  REQUIRE_THAT( m.perClassStats[1].producerAccuracy, WithinAbs( 0.75, 1e-12 ) );
  REQUIRE_THAT( m.perClassStats[1].f1Score, WithinAbs( 0.75, 1e-12 ) );
  REQUIRE_THAT( m.perClassStats[2].producerAccuracy, WithinAbs( 43.0 / 55.0, 1e-12 ) );
  REQUIRE_THAT( m.perClassStats[2].userAccuracy, WithinAbs( 43.0 / 60.0, 1e-12 ) );
  REQUIRE_THAT( m.perClassStats[2].f1Score, WithinAbs( 86.0 / 115.0, 1e-12 ) );
}

TEST_CASE( "Streaming tile accumulation is bit-identical to single-shot", "[d15][confusion]" )
{
  const Canonical data;
  const std::vector<int> classes = { 0, 1, 2 };
  const EvaluationMetrics reference = ConfusionMatrixEvaluator::compute( data.gt, data.pred, classes );

  std::vector<std::vector<int64_t>> acc( 3, std::vector<int64_t>( 3, 0 ) );
  // Seven uneven tiles (plus a trailing empty one) must sum without drift.
  const size_t tileSizes[] = { 40, 40, 40, 30, 25, 15, 10 };
  size_t offset = 0;
  for ( const size_t n : tileSizes )
  {
    ConfusionMatrixEvaluator::accumulateTile( acc,
                                              std::span( data.gt ).subspan( offset, n ),
                                              std::span( data.pred ).subspan( offset, n ),
                                              classes );
    offset += n;
  }
  ConfusionMatrixEvaluator::accumulateTile( acc, {}, {}, classes );
  REQUIRE( offset == 200 );

  const EvaluationMetrics streamed = ConfusionMatrixEvaluator::finalize( acc, classes );
  REQUIRE( streamed.matrix == reference.matrix );
  // Bit-exact: same integer matrix, same metric code path.
  REQUIRE( streamed.overallAccuracy == reference.overallAccuracy );
  REQUIRE( streamed.cohensKappa == reference.cohensKappa );
  REQUIRE( streamed.macroF1Score == reference.macroF1Score );
  REQUIRE( streamed.totalSampleCount == 200 );
}

TEST_CASE( "Off-target classes are excluded and zero-presence classes degrade to zero", "[d15][confusion]" )
{
  // Class 7 exists in the labels but not in targetClasses -> excluded from N.
  const std::vector<int> gtMix = { 0, 7, 1, 7 };
  const std::vector<int> predMix = { 0, 7, 0, 1 };
  const std::vector<int> classes01 = { 0, 1 };
  const EvaluationMetrics m = ConfusionMatrixEvaluator::compute( gtMix, predMix, classes01 );
  REQUIRE( m.totalSampleCount == 2 );
  REQUIRE_THAT( m.overallAccuracy, WithinAbs( 0.5, 1e-12 ) );

  // Requested class 3 has no samples: zero marginals -> zero accuracies, no NaN.
  const std::vector<int> zeros = { 0, 0 };
  const std::vector<int> classes03 = { 0, 3 };
  const EvaluationMetrics z = ConfusionMatrixEvaluator::compute( zeros, zeros, classes03 );
  REQUIRE( z.totalSampleCount == 2 );
  const auto &s3 = z.perClassStats[1];
  REQUIRE( s3.classId == 3 );
  REQUIRE( s3.groundTruthTotal == 0 );
  REQUIRE_THAT( s3.producerAccuracy, WithinAbs( 0.0, 1e-12 ) );
  REQUIRE_THAT( s3.f1Score, WithinAbs( 0.0, 1e-12 ) );
  REQUIRE( std::isfinite( z.cohensKappa ) );
}

TEST_CASE( "Degenerate agreement cases keep kappa finite", "[d15][confusion]" )
{
  // Single-class perfect agreement: pe degenerates to 1 but po == 1
  // must still report kappa == 1 (repo-wide convention).
  const std::vector<int> zeros = { 0, 0 };
  const std::vector<int> class0 = { 0 };
  const EvaluationMetrics perfect = ConfusionMatrixEvaluator::compute( zeros, zeros, class0 );
  REQUIRE_THAT( perfect.cohensKappa, WithinAbs( 1.0, 1e-12 ) );

  // Empty input: all-zero metrics, no division by zero.
  const std::vector<int> classes01 = { 0, 1 };
  const EvaluationMetrics empty = ConfusionMatrixEvaluator::compute( {}, {}, classes01 );
  REQUIRE( empty.totalSampleCount == 0 );
  REQUIRE_THAT( empty.overallAccuracy, WithinAbs( 0.0, 1e-12 ) );

  // Size mismatch is refused, not truncated.
  const std::vector<int> gtTwo = { 0, 1 };
  const std::vector<int> predOne = { 0 };
  const EvaluationMetrics mismatch = ConfusionMatrixEvaluator::compute( gtTwo, predOne, classes01 );
  REQUIRE( mismatch.totalSampleCount == 0 );
}
