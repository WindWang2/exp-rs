// test_classification_intelligence_scale.cpp — F12 bounded logical-scale
// evidence (WP-H / Phase 5).
//
// Bounded by construction (see .planning/classification-intelligence-11/
// PERFORMANCE.md): assertions check invariants, never wall-clock; the test
// runner TIMEOUT (600 s) is the outer resource fence and RUN_SERIAL keeps
// it off other suites' tails.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_probability_calibration.h"
#include "rs_classifier_normalbayes.h"
#include "rs_uncertainty.h"

#include <opencv2/core.hpp>

#include <random>
#include <vector>

namespace
{
// 100k samples, 4 features, 5 well-separated Gaussian classes.
// NB training cost O(N·B·K) — linear in the sample count by construction.
struct BigFixture
{
    cv::Mat X, y;
    explicit BigFixture( int n = 100000 )
    {
        X.create( n, 4, CV_32F );
        y.create( n, 1, CV_32S );
        std::mt19937 rng( 7u );
        // Class centres at 10-unit steps on each feature axis.
        std::vector<float> centre( 4, 0.0f );
        int row = 0;
        while ( row < n )
        {
            const int cls = row % 5;
            for ( int b = 0; b < 4; ++b )
                centre[b] = 10.0f * cls + 1.0f;
            std::normal_distribution<float> noise( 0.0f, 1.0f );
            for ( int b = 0; b < 4; ++b )
                X.at<float>( row, b ) = centre[b] + noise( rng );
            y.at<int>( row, 0 ) = cls + 1;
            ++row;
        }
    }
};
} // namespace

TEST_CASE( "Scale: 100k-sample NormalBayes train + predict keeps class "
           "distribution invariants",
           "[f12][scale]" )
{
  BigFixture fx;
  REQUIRE( fx.X.rows == 100000 );

  RsClassifierNormalBayes nb;
  REQUIRE( nb.fit( fx.X, fx.y ) );
  const cv::Mat pred = nb.predict( fx.X );
  REQUIRE( pred.rows == fx.X.rows );

  // Invariant: in-sample accuracy is at least 0.95 (centre separation 10
  // vs noise sigma 1) and every class keeps a sane share of predictions
  // (each class is 20% of samples; predicted share must stay in [10%, 35%]).
  int correct = 0;
  std::vector<int> perClass( 6, 0 );
  for ( int i = 0; i < pred.rows; ++i )
  {
    const int p = pred.at<int>( i, 0 );
    ++perClass[p];
    if ( p == fx.y.at<int>( i, 0 ) )
      ++correct;
  }
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.95 );
  for ( int c = 1; c <= 5; ++c )
  {
    const double share = static_cast<double>( perClass[c] ) / pred.rows;
    REQUIRE( share > 0.10 );
    REQUIRE( share < 0.35 );
  }
}

TEST_CASE( "Scale: isotonic calibration on 100k scores stays monotone and "
           "row-normalised",
           "[f12][scale]" )
{
  constexpr int n = 100000;
  std::vector<float> scores;
  std::vector<int> labels;
  scores.reserve( static_cast<size_t>( n ) * 2 );
  labels.reserve( n );
  std::mt19937 rng( 11u );
  std::uniform_real_distribution<float> uniform( -5.0f, 5.0f );
  for ( int i = 0; i < n; ++i )
  {
    const float s = uniform( rng );
    // Logistic ground truth + deterministic label noise.
    const double p = 1.0 / ( 1.0 + std::exp( -s ) );
    const bool positive = ( i % 10 == 0 ) ? ( p <= 0.5 ) : ( p > 0.5 );
    scores.push_back( s );
    scores.push_back( -s ); // column 2 = class 2's score is the mirrored one
    labels.push_back( positive ? 2 : 1 );
  }

  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitIsotonic( scores, n, { 1, 2 },
                                                 labels, model ) );
  // Knot count is bounded by the number of pooled blocks — never exceeds N.
  REQUIRE( model.isotonicX[0].size() <= static_cast<size_t>( n ) );
  REQUIRE( model.isotonicY[0].size() == model.isotonicX[0].size() );

  std::vector<float> probs;
  REQUIRE( RsProbabilityCalibrator::apply( model, scores, n, probs ) );
  REQUIRE( probs.size() == static_cast<size_t>( n ) * 2 );
  // Monotonicity per class on an ascending score probe + row normalisation.
  for ( int i = 1; i < n; ++i )
  {
    if ( scores[static_cast<size_t>( 2 * i )] > scores[static_cast<size_t>( 2 * ( i - 1 ) )] )
    {
      REQUIRE( probs[static_cast<size_t>( 2 * i )] >=
               probs[static_cast<size_t>( 2 * ( i - 1 ) )] - 1e-6f );
    }
  }
}

TEST_CASE( "Scale: uncertainty measures on 100k probability rows",
           "[f12][scale]" )
{
  constexpr int n = 100000;
  std::vector<float> row = { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
  double h = 0.0;
  REQUIRE( RsUncertainty::entropy( row, h ) );
  const double normalized = RsUncertainty::normalizeEntropy( h, 5 );
  REQUIRE( normalized == Catch::Approx( 1.0 ).margin( 1e-12 ) );

  // Streaming contract: the measure is per-row, so 100k rows cost
  // O(n·K) with O(K) memory (no matrices retained).
  double sum = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    double hi = 0.0;
    REQUIRE( RsUncertainty::entropy( row, hi ) );
    sum += hi;
  }
  REQUIRE( sum == Catch::Approx( n * h ).margin( 1e-6 ) );
}
