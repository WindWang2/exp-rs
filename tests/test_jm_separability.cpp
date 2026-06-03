// Phase 10A Task 10.6 — Jeffries-Matusita separability tests.
//
// Validates RsJmSeparability::pairJm() across four regimes:
//   - identical distributions (JM ~ 0)
//   - completely separated (JM ~ 2)
//   - moderate overlap (0.8 < JM < 1.9)
//   - degenerate samples (epsilon ridge keeps result in [0, 2])

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_jm_separability.h"

#include <opencv2/core.hpp>

using Catch::Approx;

TEST_CASE( "JM: identical distributions yield ~0", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 100, 3, CV_32F );
  cv::randn( a, 50.0, 10.0 );
  cv::Mat b = a.clone();
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm == Approx( 0.0 ).margin( 0.05 ) );
}

TEST_CASE( "JM: completely separated yields ~2", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 100, 3, CV_32F );
  cv::randn( a, 50.0, 2.0 );
  cv::Mat b( 100, 3, CV_32F );
  cv::randn( b, 200.0, 2.0 );
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm > 1.95 );
}

TEST_CASE( "JM: moderate overlap yields between 0.8 and 1.9", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 500, 3, CV_32F );
  cv::randn( a, 50.0, 8.0 );
  cv::Mat b( 500, 3, CV_32F );
  cv::randn( b, 65.0, 8.0 );
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm > 0.8 );
  REQUIRE( jm < 1.9 );
}

TEST_CASE( "JM: degenerate covariance with epsilon ridge stays in range", "[classify][jm]" )
{
  cv::Mat a( 2, 3, CV_32F );
  a.setTo( 50.0 );
  cv::Mat b( 2, 3, CV_32F );
  b.setTo( 60.0 );
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm >= 0.0 );
  REQUIRE( jm <= 2.0 );
}
