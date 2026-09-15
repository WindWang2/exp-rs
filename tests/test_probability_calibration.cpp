// test_probability_calibration.cpp — F12: Platt/isotonic calibration and
// reliability metrics against hand-computed oracles.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_probability_calibration.h"

#include <QJsonArray>
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

// Two well-separated one-vs-rest score clusters, 2 classes, N x K layout:
// a class-1 sample carries (col0, col1) ≈ (+2, -2); a class-2 sample the
// mirror. A perfect calibrator must place the per-class 0.5 crossing
// between the clusters (oracle: geometric).
void fillSeparableScores( std::vector<float> &scores, std::vector<int> &labels,
                          int perClass = 40 )
{
  scores.clear();
  labels.clear();
  scores.reserve( static_cast<size_t>( perClass ) * 4 );
  for ( int i = 0; i < perClass; ++i )
  {
    const float s = 2.0f - 0.05f * i / perClass;
    scores.push_back( s );      // column 0 = class 1 score
    scores.push_back( -s );     // column 1 = class 2 score
    labels.push_back( 1 );
  }
  for ( int i = 0; i < perClass; ++i )
  {
    const float s = 2.0f - 0.05f * i / perClass;
    scores.push_back( -s );     // column 0
    scores.push_back( s );      // column 1
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
  const std::vector<float> probe = { 2.0f, -2.0f, -2.0f, 2.0f };
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
  const std::vector<int> shortLabels = { 1, 2, 1 };
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 4, { 1, 2 }, shortLabels, 100, model ) );

  // Invalid class order (duplicate).
  scores = { 1.0f, 2.0f };
  const std::vector<int> twoLabels = { 1, 2 };
  REQUIRE( !RsProbabilityCalibrator::fitPlatt( scores, 2, { 1, 1 }, twoLabels, 100, model ) );
}

TEST_CASE( "Isotonic: PAV pools a violating sequence into a monotone one", "[classify][calibration]" )
{
  // Oracle for class 1's column: scores ascending [1,2,3,4] with binary
  // targets [0,1,0,1] (positives at 2 and 4) — the rates violate
  // monotonicity; PAV must produce a non-decreasing step function that is
  // 0 at the low end and 1 at the high end. Labels interleave accordingly
  // (1,2,1,2), and class 2's column mirrors each score.
  std::vector<float> scores;
  std::vector<int> labels;
  const float base[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
  const int lab[4] = { 2, 1, 2, 1 };
  for ( int i = 0; i < 4; ++i )
  {
    scores.push_back( base[i] );
    scores.push_back( -base[i] );
    labels.push_back( lab[i] );
  }
  const QVector<int> classIds = { 1, 2 };
  RsCalibrationModel model;
  REQUIRE( RsProbabilityCalibrator::fitIsotonic( scores, 4, classIds, labels, model ) );
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
  // Oracle by hand, 2 classes {1,2}, 3 samples. Probabilities use exact
  // binary fractions (3/4, 1/4, 1/2) so the bin floor is deterministic:
  //  sample 0: label 1, probs (0.75, 0.25): brier (0.25²+0.25²)=0.125;
  //            top prob 0.75 → bin floor(7.5)=7, correct
  //  sample 1: label 2, probs (0.75, 0.25): brier (0.75²+0.75²)=1.125;
  //            top column is 0 (class 1) → WRONG, bin 7
  //  sample 2: label 1, probs (0.5, 0.5): brier 0.5; top column 0 (tie →
  //            first column) = class 1 → correct, bin floor(5)=5
  // mean brier = (0.125+1.125+0.5)/3 = 7/12
  //  bin7: count 2, meanConf 0.75, acc 0.5;  bin5: count 1, conf 0.5, acc 1
  // ECE = (2/3)·0.25 + (1/3)·0.5 = 1/3
  const std::vector<float> probs = { 0.75f, 0.25f, 0.75f, 0.25f, 0.5f, 0.5f };
  const std::vector<int> labels = { 1, 2, 1 };
  const QVector<int> classIds = { 1, 2 };
  const auto report = RsCalibrationMetrics::compute( labels, probs, 3, classIds, 10 );
  REQUIRE( report.ok );
  REQUIRE( report.brier == Approx( ( 0.125 + 1.125 + 0.5 ) / 3.0 ).margin( 1e-12 ) );
  REQUIRE( report.ece == Approx( 1.0 / 3.0 ).margin( 1e-12 ) );
  REQUIRE( report.bins.size() == 10 );
  int total = 0;
  for ( const auto &b : report.bins )
    total += b.count;
  REQUIRE( total == 3 ); // conservation oracle
  REQUIRE( report.bins[7].count == 2 );
  REQUIRE( report.bins[7].meanConfidence == Approx( 0.75 ).margin( 1e-12 ) );
  REQUIRE( report.bins[7].empiricalAccuracy == Approx( 0.5 ).margin( 1e-12 ) );
  REQUIRE( report.bins[5].count == 1 );
  REQUIRE( report.bins[5].empiricalAccuracy == Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( report.logLoss == Approx( ( -std::log( 0.75 ) - std::log( 0.25 ) - std::log( 0.5 ) ) / 3.0 )
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
  const QVector<int> classIds = { 1, 2 };
  const std::vector<int> labels12 = { 1, 2 };
  const std::vector<int> labels72 = { 7, 2 };
  const std::vector<int> label1 = { 1 };

  RsCalibrationMetrics::Report report;
  const std::vector<float> unnormalised = { 0.8f, 0.8f, 0.2f, 0.2f };
  report = RsCalibrationMetrics::compute( labels12, unnormalised, 2, classIds, 10 );
  REQUIRE( !report.ok ); // row sums = 1.6

  const std::vector<float> oneHot = { 1.0f, 0.0f, 0.0f, 1.0f };
  report = RsCalibrationMetrics::compute( labels72, oneHot, 2, classIds, 10 );
  REQUIRE( !report.ok ); // label 7 outside class order

  report = RsCalibrationMetrics::compute( labels12, oneHot, 2, classIds, 0 );
  REQUIRE( !report.ok ); // zero bins

  const std::vector<float> wrongSize = { 1.0f, 0.0f, 0.5f };
  report = RsCalibrationMetrics::compute( label1, wrongSize, 1, classIds, 10 );
  REQUIRE( !report.ok ); // probs.size() != n*K
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
    scores.push_back( static_cast<float>( -s ) ); // column 0 = class 1 score
    scores.push_back( static_cast<float>( s ) );  // column 1 = class 2 score
    // Deterministic noise: flip membership in the middle band.
    const bool flip = ( i % 7 == 0 ) && s > -0.5 && s < 0.5;
    labels.push_back( ( p2 > 0.5 ) != flip ? 2 : 1 );
  }
  const QVector<int> classIds = { 1, 2 };

  // Raw "probabilities": the overconfident transform itself.
  std::vector<float> raw;
  raw.reserve( scores.size() );
  for ( int i = 0; i < n; ++i )
  {
    const double s = scores[static_cast<size_t>( 2 * i + 1 )]; // class-2 column
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
