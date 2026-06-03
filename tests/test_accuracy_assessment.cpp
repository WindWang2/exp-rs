// tests/test_accuracy_assessment.cpp — Phase 10A Task 10.9.
//
// Verifies RsAccuracyAssessment::compute() produces correct confusion
// matrix, Overall Accuracy and Cohen's Kappa on canonical test inputs.
//
//   1. Perfect prediction   → OA = 1.0, kappa = 1.0 (po==1 guard)
//   2. Known confusion      → OA = 5/6, kappa ≈ 0.75
//   3. Single-class input   → OA = 1.0, kappa = 1.0 (degenerate guard)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_accuracy_assessment.h"

using Catch::Approx;

TEST_CASE( "Accuracy: perfect prediction → kappa 1.0, OA 1.0", "[classify][acc]" )
{
  QVector<int> yt = { 1, 1, 2, 2, 3, 3 };
  QVector<int> yp = { 1, 1, 2, 2, 3, 3 };
  const auto r = RsAccuracyAssessment::compute( yt, yp );

  REQUIRE( r.classIds.size() == 3 );
  REQUIRE( r.overallAccuracy == Approx( 1.0 ) );
  REQUIRE( r.kappa == Approx( 1.0 ) );

  // Diagonal of the confusion matrix should equal class counts.
  REQUIRE( r.confusion.rows == 3 );
  REQUIRE( r.confusion.cols == 3 );
  REQUIRE( r.confusion.at<int>( 0, 0 ) == 2 );
  REQUIRE( r.confusion.at<int>( 1, 1 ) == 2 );
  REQUIRE( r.confusion.at<int>( 2, 2 ) == 2 );
}

TEST_CASE( "Accuracy: known confusion → expected Kappa", "[classify][acc]" )
{
  QVector<int> yt = { 1, 1, 2, 2, 3, 3 };
  QVector<int> yp = { 1, 2, 2, 2, 3, 3 };
  const auto r = RsAccuracyAssessment::compute( yt, yp );

  // 5 of 6 correct.
  REQUIRE( r.overallAccuracy == Approx( 5.0 / 6.0 ).margin( 1e-6 ) );

  // Kappa for this confusion matrix is around 0.74; allow a generous margin.
  REQUIRE( r.kappa == Approx( 0.75 ).margin( 0.05 ) );

  // Producer/User accuracy sanity: class 2 producer = 2/2 = 1.0 because all
  // true class-2 samples were predicted as class 2; class 1 producer = 1/1
  // because only one sample was predicted as class 1 and it was true class 1.
  REQUIRE( r.producerAcc.value( 1 ) == Approx( 1.0 ).margin( 1e-6 ) );
  REQUIRE( r.userAcc.value( 1 ) == Approx( 0.5 ).margin( 1e-6 ) );
}

TEST_CASE( "Accuracy: single-class degenerate case", "[classify][acc]" )
{
  QVector<int> yt = { 1, 1, 1 };
  QVector<int> yp = { 1, 1, 1 };
  const auto r = RsAccuracyAssessment::compute( yt, yp );

  REQUIRE( r.classIds.size() == 1 );
  REQUIRE( r.overallAccuracy == Approx( 1.0 ) );
  REQUIRE( r.kappa == Approx( 1.0 ) );
}
