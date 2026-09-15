// test_probability_calibration.cpp — F12: Platt/isotonic calibration and
// reliability metrics against hand-computed oracles.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_probability_calibration.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
// Sigmoid with the model's own convention, independent of the implementation.
double refSigmoid( double z )
{
  return 1.0 / ( 1.0 + std::exp( -z ) );
}

// Two well-separated score clusters, 2 classes: class 1 scores ≈ -2,
// class 2 scores ≈ +2. A perfect calibrator must push the clusters to
// opposite sides of 0.5 (oracle: visual/geometric).
void fillSeparableScores( std::vector<float> &scores, std::vector<int> &labels,
                          int perClass = 40 )
{
  scores.clear();
  labels.clear();
  for ( int i = 0; i < perClass; ++i )
  {
    scores.push_back( -2.0f + 0.05f * i / perClass );
    labels.push_back( 1 );
  }
  for ( int i = 0; i < perClass; ++i )
  {
    scores.push_back( 2.0f - 0.05f * i / perClass );
    labels.push_back( 2 );
  }
}
} // namespace

TEST_CASE( "Platt fit: separable clusters cross 0.5 near score 0", "[classify][calibration]" )
{
  std::vector<float> scores;
  std::vector<int> labels;
  fillSeparableScores( scores, labels );
  const QVector<int> classIds = { 1, 2 };

  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, static_cast<int>( labels.size() ),
                                              classIds, labels, 100, model ) );
  REQUIRE( model.isValid() );
  REQUIRE( model.plattA.size() == 2 );

  // Oracle: with monotone increasing sigmoid per class, the decision score s
  // where p == 0.5 is s* = -B/A. For class 1 (positives at -2, negatives at +2)
  // s* must lie in (-2, 2) and be negative-centred; class 2 mirrors it.
  const double s1 = -model.plattB[0] / model.plattA[0];
  const double s2 = -model.plattB[1] / model.plattA[1];
  REQUIRE( s1 > -2.0 );
  REQUIRE( s1 < 2.0 );
  REQUIRE( s2 > -2.0 );
  REQUIRE( s2 < 2.0 );
  // Symmetric construction → symmetric crossing points (tolerance 0.5).
  REQUIRE( std::abs( s1 + s2 ) < 0.5 );
}

TEST_CASE( "Platt apply: rows normalised, order preserved via classIds", "[classify][calibration]" )
{
  std::vector<float> scores;
  std::vector<int> labels;
  fillSeparableScores( scores, labels, 20 );
  const QVector<int> classIds = { 1, 2 };
  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, static_cast<int>( labels.size() ),
                                              classIds, labels, 100, model ) );

  // One row per class at its cluster centre: class 1 → p(class1) > 0.5.
  const std::vector<float> probe = { -2.0f, 2.0f, -2.0f, 2.0f };
  std::vector<float> probs;
  REQUIRE( RsProbabilityCalibrator::apply( model, probe, 2, probs ) );
  REQUIRE( probs.size() == 4 );
  // Row 0: label space {1,2} → column 0 = class 1.
  REQUIRE( probs[0] + probs[1] == Approx( 1.0 ).margin( 1e-5 ) );
  REQUIRE( probs[0] > 0.5 );
  // Row 1: class 2 wins.
  REQUIRE( probs[2] + probs[3] == Approx( 1.0 ).margin( 1e-5 ) );
  REQUIRE( probs[3] > 0.5 );
}

TEST_CASE( "Platt is deterministic: two fits give identical parameters", "[classify][calibration]" )
{
  std::vector<float> scores;
  std::vector<int> labels;
  fillSeparableScores( scores, labels );
  const QVector<int> classIds = { 1, 2 };
  RsCalibrationModel m1, m2;
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, static_cast<int>( labels.size() ),
                                              classIds, labels, 100, m1 ) );
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, static_cast<int>( labels.size() ),
                                              classIds, labels, 100, m2 ) );
  for ( int c = 0; c < 2; ++c )
  {
    REQUIRE( m1.plattA[c] == m2.plattA[c] );
    REQUIRE( m1.plattB[c] == m2.plattB[c] );
  }
}

TEST_CASE( "Platt fit fails closed on degenerate calibration sets", "[classify][calibration]" )
{
  // All samples from one class → no negatives for class 1 sub-problem.
  std::vector<float> scores = { 1.0f, 2.0f, 3.0f };
  std::vector<int> labels = { 1, 1, 1 };
  RsCalibrationModel model;
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 3, { 1, 2 }, labels, 100, model ) );
  REQUIRE( !model.isValid() );

  // Non-finite score.
  scores = { 1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f, 4.0f };
  labels = { 1, 2, 1, 2 };
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 4, { 1, 2 }, labels, 100, model ) );

  // Size mismatch.
  scores = { 1.0f, 2.0f, 3.0f, 4.0f };
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 4, { 1, 2 }, { 1, 2, 1 }, 100, model ) );

  // Invalid class order (duplicate).
  scores = { 1.0f, 2.0f };
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 2, { 1, 1 }, { 1, 2 }, 100, model ) );
}

TEST_CASE( "Isotonic: PAV pools a violating sequence into a monotone one", "[classify][calibration]" )
{
  // Oracle: scores ascending [1,2,3,4], targets [0,1,0,1]. Rates violate
  // monotonicity; PAV must produce a non-decreasing step function that is
  // 0 at the low end and 1 at the high end.
  std::vector<float> scores;
  std::vector<int> labels;
  const float base[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 3.0f, 4.0f };
  const int lab[8] = { 1, 1, 1, 1, 2, 2, 2, 2 };
  for ( int i = 0; i < 8; ++i )
  {
    scores.push_back( base[i] );
    labels.push_back( lab[i] );
  }
  const QVector<int> classIds = { 1, 2 };
  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitIsotonic( scores, 8, classIds, labels, model ) );
  REQUIRE( model.method == RsCalibrationModel::Method::Isotonic );

  // Apply: output must be monotone in the score per class.
  const std::vector<float> probe = { 0.5f, 0.5f, 1.5f, 1.5f, 2.5f, 2.5f, 5.0f, 5.0f };
  std::vector<float> probs;
  REQUIRE( RsProbabilityCalibrator::apply( model, probe, 4, probs ) );
  for ( int row = 1; row < 4; ++row )
  {
    REQUIRE( probs[static_cast<size_t>( row ) * 2] >=
             probs[static_cast<size_t>( row - 1 ) * 2] - 1e-6 );
  }
  // Extreme ends clamp to [0,1]; rows normalise.
  REQUIRE( probs[7] + probs[6] == Approx( 1.0 ).margin( 1e-5 ) );
}

TEST_CASE( "Model JSON round-trip preserves parameters and class order", "[classify][calibration]" )
{
  std::vector<float> scores;
  std::vector<int> labels;
  fillSeparableScores( scores, labels, 15 );
  const QVector<int> classIds = { 1, 2 };
  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, static_cast<int>( labels.size() ),
                                              classIds, labels, 100, model ) );
  const QJsonObject obj = model.toJson();
  RsCalibrationModel restored;
  REQUIRE( restored.fromJson( obj ) );
  REQUIRE( restored.classIds == model.classIds );
  REQUIRE( restored.plattA.size() == 2 );
  for ( int c = 0; c < 2; ++c )
  {
    REQUIRE( restored.plattA[c] == Approx( model.plattA[c] ).margin( 1e-12 ) );
    REQUIRE( restored.plattB[c] == Approx( model.plattB[c] ).margin( 1e-12 ) );
  }
  // Rejected: structurally broken documents must not produce a valid model.
  RsCalibrationModel bad;
  REQUIRE( !bad.fromJson( QJsonObject() ) );
  QJsonObject wrongVersion = model.toJson();
  wrongVersion.insert( QStringLiteral( "version" ), 99 );
  REQUIRE( !bad.fromJson( wrongVersion ) );
  QJsonObject wrongOrder = model.toJson();
  wrongOrder.insert( QStringLiteral( "classIds" ), QJsonArray { 2, 1 } );
  REQUIRE( !bad.fromJson( wrongOrder ) );
}

TEST_CASE( "Metrics: Brier/ECE/logLoss match hand-computed values", "[classify][calibration]" )
{
  // Oracle by hand, 2 classes {1,2}, 3 samples:
  //  sample 0: label 1, probs (0.8, 0.2): brier (0.2²+0.2²)=0.08; conf 0.8, correct
  //  sample 1: label 2, probs (0.9, 0.1): brier (0.9²+0.9²)=1.62; conf 0.9, wrong
  //  sample 2: label 1, probs (0.5, 0.5): brier (0.5²+0.5²)=0.50; conf 0.5, correct
  // mean brier = (0.08+1.62+0.50)/3 = 0.7333…
  // 10 bins (width 0.1): bin index = min(9, floor(conf*10)) → 8, 9, 5.
  //  bin8: conf (0.8)/1, acc 1/1
  //  bin9: conf 0.9/1, acc 0/1
  //  bin5: conf 0.5/1, acc 1/1
  // ECE = (1/3)(|1-0.8| + |0-0.9| + |1-0.5|) = (0.2+0.9+0.5)/3 = 0.5333…
  const std::vector<float> probs = { 0.8f, 0.2f, 0.9f, 0.1f, 0.5f, 0.5f };
  const std::vector<int> labels = { 1, 2, 1 };
  const auto report = RsCalibrationMetrics::compute( labels, probs, 3, { 1, 2 }, 10 );
  REQUIRE( report.ok );
  REQUIRE( report.brier == Approx( 0.08 / 3.0 + 1.62 / 3.0 + 0.50 / 3.0 ).margin( 1e-12 ) );
  REQUIRE( report.ece == Approx( ( 0.2 + 0.9 + 0.5 ) / 3.0 ).margin( 1e-12 ) );
  REQUIRE( report.bins.size() == 10 );
  int total = 0;
  for ( const auto &b : report.bins )
    total += b.count;
  REQUIRE( total == 3 ); // conservation oracle
  REQUIRE( report.bins[8].count == 1 );
  REQUIRE( report.bins[8].meanConfidence == Approx( 0.8 ).margin( 1e-12 ) );
  REQUIRE( report.bins[9].empiricalAccuracy == Approx( 0.0 ).margin( 1e-12 ) );
  REQUIRE( report.logLoss == Approx( ( -std::log( 0.8 ) - std::log( 0.1 ) - std::log( 0.5 ) ) / 3.0 )
             .margin( 1e-12 ) );
}

TEST_CASE( "Metrics: perfect predictions give brier 0 and ece 0", "[classify][calibration]" )
{
  const std::vector<float> probs = { 1.0f, 0.0f, 0.0f, 1.0f };
  const std::vector<int> labels = { 1, 2 };
  const auto report = RsCalibrationMetrics::compute( labels, probs, 2, { 1, 2 }, 10 );
  REQUIRE( report.ok );
  REQUIRE( report.brier == Approx( 0.0 ).margin( 1e-12 ) );
  REQUIRE( report.ece == Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "Metrics: fails closed on unnormalised rows and unknown labels", "[classify][calibration]" )
{
  RsCalibrationMetrics::Report report;
  report = RsCalibrationMetrics::compute( { 1, 2 }, { 0.8f, 0.8f, 0.2f, 0.2f }, 2, { 1, 2 }, 10 );
  REQUIRE( !report.ok ); // row sums = 1.6

  report = RsCalibrationMetrics::compute( { 7, 2 }, { 1.0f, 0.0f, 0.0f, 1.0f }, 2, { 1, 2 }, 10 );
  REQUIRE( !report.ok ); // label 7 outside class order

  report = RsCalibrationMetrics::compute( { 1, 2 }, { 1.0f, 0.0f, 0.0f, 1.0f }, 2, { 1, 2 }, 0 );
  REQUIRE( !report.ok ); // zero bins

  report = RsCalibrationMetrics::compute( { 1 }, { 1.0f, 0.0f }, 1, { 1, 2 }, 10 );
  REQUIRE( !report.ok ); // size mismatch
}

TEST_CASE( "Calibration improves overconfidence (Brier decreases)", "[classify][calibration]" )
{
  // Overconfident model: sigmoid(5s) — near-binary probabilities while the
  // labels carry noise (15% label flip on class 2 members). After Platt
  // recalibration on the noisy set, Brier must not increase.
  std::vector<float> scores;
  std::vector<int> labels;
  const int n = 300;
  for ( int i = 0; i < n; ++i )
  {
    const double s = -2.0 + 4.0 * static_cast<double>( i ) / ( n - 1 );
    const double p2 = refSigmoid( 5.0 * s );
    scores.push_back( static_cast<float>( s ) );
    // Deterministic noise: flip membership in the middle band.
    const bool flip = ( i % 7 == 0 ) && s > -0.5 && s < 0.5;
    labels.push_back( ( p2 > 0.5 ) != flip ? 2 : 1 );
  }
  const QVector<int> classIds = { 1, 2 };

  // Raw "probabilities": the overconfident transform itself.
  std::vector<float> raw;
  raw.reserve( scores.size() * 2 );
  for ( float s : scores )
  {
    const double p2 = refSigmoid( 5.0 * s );
    raw.push_back( static_cast<float>( 1.0 - p2 ) );
    raw.push_back( static_cast<float>( p2 ) );
  }
  const auto rawReport = RsCalibrationMetrics::compute( labels, raw, n, classIds, 10 );
  REQUIRE( rawReport.ok );

  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitPlatt( scores, n, classIds, labels, 200, model ) );
  std::vector<float> calibrated;
  REQUIRE( RsProbabilityCalibrator::apply( model, scores, n, calibrated ) );
  const auto calReport = RsCalibrationMetrics::compute( labels, calibrated, n, classIds, 10 );
  REQUIRE( calReport.ok );
  INFO( "raw brier=" << rawReport.brier << " calibrated=" << calReport.brier );
  REQUIRE( calReport.brier <= rawReport.brier + 1e-6 );
}
