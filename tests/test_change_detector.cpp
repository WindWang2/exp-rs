// tests/test_change_detector.cpp — D15 Package E.
//
// Ground-truth policy: 3-4-5 triangle magnitudes, atan2 branch values and
// hand-expanded log identities are closed forms; PCA expectations come from
// an analytic covariance with known eigenvectors.  Nothing is recomputed
// through the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "processing/algorithms/change_detector.h"

using Catch::Matchers::WithinAbs;
using rs::processing::ChangeDetector;
using rs::processing::CvaChangeResult;

TEST_CASE( "Difference and normalized difference follow algebraic identities", "[d15][change]" )
{
  const std::vector<float> a = { 10.0f, 0.0f, 0.2f };
  const std::vector<float> b = { 13.0f, 0.0f, 0.6f };

  const auto diff = ChangeDetector::computeDifference( a, b );
  REQUIRE( diff.size() == 3 );
  REQUIRE_THAT( diff[0], WithinAbs( 3.0, 1e-6 ) );
  REQUIRE_THAT( diff[2], WithinAbs( 0.4f, 1e-6 ) );

  // NDVI geometry: (0.6 - 0.2) / (0.6 + 0.2) = 0.5; zero-sum pixel -> 0.
  const auto nd = ChangeDetector::computeNormalizedDifference( a, b );
  REQUIRE_THAT( nd[1], WithinAbs( 0.0, 1e-6 ) ); // float32 API
  REQUIRE_THAT( nd[2], WithinAbs( 0.5, 1e-6 ) );

  const std::vector<float> one = { 1.0f };
  const std::vector<float> two = { 1.0f, 2.0f };
  REQUIRE( ChangeDetector::computeDifference( one, two ).empty() );
}

TEST_CASE( "Log ratio is exactly zero for identical dates and matches the hand-expanded constant", "[d15][change]" )
{
  const std::vector<float> same = { 10.0f, 0.25f, 1000.0f };
  const auto zero = ChangeDetector::computeLogRatio( same, same );
  for ( const float v : zero )
    REQUIRE_THAT( v, WithinAbs( 0.0, 1e-9 ) );

  // ln((13 + 1e-4)/(10 + 1e-4)) = ln13 - ln10 + ln(1 + 1e-4/13) - ln(1 + 1e-4/10)
  //                             = 0.26236426446749 + 7.69227e-6 - 9.99950e-6
  //                             = 0.26236195723...
  const std::vector<float> t1 = { 10.0f };
  const std::vector<float> t2 = { 13.0f };
  const auto lr = ChangeDetector::computeLogRatio( t1, t2 );
  REQUIRE_THAT( lr[0], WithinAbs( 0.26236195723, 1e-6 ) ); // float32 rounding on ~0.26

  // eps = 1: ln(14/11) = ln14 - ln11 = 0.2411620568168877.
  const auto lr1 = ChangeDetector::computeLogRatio( t1, t2, 1.0f );
  REQUIRE_THAT( lr1[0], WithinAbs( 0.2411620568168877, 1e-6 ) );
}

TEST_CASE( "CVA magnitude and direction recover the 3-4-5 triangle", "[d15][change]" )
{
  // One pixel, two bands: t1=[10,20], t2=[13,24] -> delta [3,4].
  const std::vector<float> t1 = { 10.0f, 20.0f };
  const std::vector<float> t2 = { 13.0f, 24.0f };
  const CvaChangeResult r = ChangeDetector::computeCva( t1.data(), t2.data(), 1, 1, 2 );
  REQUIRE( r.changeMagnitude.size() == 1 );
  REQUIRE_THAT( r.changeMagnitude[0], WithinAbs( 5.0, 1e-6 ) );
  REQUIRE_THAT( r.changeDirectionAngle[0], WithinAbs( 0.9272952180016122, 1e-6 ) ); // float32 API
  REQUIRE( r.binaryChangeMask[0] == 1 ); // sole pixel: it dominates its own statistics
}

TEST_CASE( "CVA direction angle hits exact axis branches", "[d15][change]" )
{
  const auto run = []( float dx0, float dx1 )
  {
    const std::vector<float> t1 = { 0.0f, 0.0f };
    const std::vector<float> t2 = { dx0, dx1 };
    return ChangeDetector::computeCva( t1.data(), t2.data(), 1, 1, 2 ).changeDirectionAngle[0];
  };
  REQUIRE_THAT( run( 3.0f, 0.0f ), WithinAbs( 0.0, 1e-6 ) );
  REQUIRE_THAT( run( 0.0f, 2.0f ), WithinAbs( M_PI / 2.0, 1e-6 ) );
  REQUIRE_THAT( run( -3.0f, 0.0f ), WithinAbs( M_PI, 1e-6 ) );
  REQUIRE_THAT( run( 0.0f, -2.0f ), WithinAbs( 3.0 * M_PI / 2.0, 1e-6 ) );
  REQUIRE_THAT( run( 0.0f, 0.0f ), WithinAbs( 0.0, 1e-6 ) );

  // Three bands [3,4,12]: magnitude = 13 (5-12-13 triangle), plane angle unchanged.
  const std::vector<float> t1 = { 0.0f, 0.0f, 0.0f };
  const std::vector<float> t2 = { 3.0f, 4.0f, 12.0f };
  const CvaChangeResult r = ChangeDetector::computeCva( t1.data(), t2.data(), 1, 1, 3 );
  REQUIRE_THAT( r.changeMagnitude[0], WithinAbs( 13.0, 1e-6 ) );
  REQUIRE_THAT( r.changeDirectionAngle[0], WithinAbs( 0.9272952180016122, 1e-6 ) );
}

TEST_CASE( "CVA adaptive threshold separates a calibrated change fraction", "[d15][change]" )
{
  // 10x10, 2 bands.  90 pixels unchanged (delta 0), 10 changed with delta
  // [3,4]: mu = 0.5, sigma = sqrt((90*0 + 10*25)/100 - 0.25) = 1.5
  // => T = 0.5 + 1.5*1.5 = 2.75; exactly the 10 changed pixels pass.
  const int w = 10, h = 10, bands = 2;
  const size_t pixels = static_cast<size_t>( w ) * h;
  std::vector<float> t1( pixels * bands, 0.0f );
  std::vector<float> t2( pixels * bands, 0.0f );
  // Band-sequential layout (contract): plane b at offset b*pixels.
  for ( int i = 0; i < 10; ++i )
  {
    t2[0 * pixels + i] = 3.0f;
    t2[1 * pixels + i] = 4.0f;
  }
  const CvaChangeResult r = ChangeDetector::computeCva( t1.data(), t2.data(), w, h, bands );
  REQUIRE_THAT( r.computedThreshold, WithinAbs( 2.75, 1e-5 ) );
  int changed = 0;
  for ( const uint8_t m : r.binaryChangeMask )
    changed += m;
  REQUIRE( changed == 10 );
  for ( int i = 0; i < 10; ++i )
    REQUIRE( r.binaryChangeMask[static_cast<size_t>( i )] == 1 );

  // A NaN pixel is excluded from statistics and masked out, not propagated
  // into the threshold.  With one unchanged pixel removed the finite set is
  // 89 zeros + 10 fives: mu = 50/99 ~ 0.50505, sigma ~ 1.50671
  // => T ~ 2.76512 (distinct from 2.75, and finite — no NaN poisoning).
  t2[50] = std::nanf( "" ); // band 0, pixel 50 (BSQ)
  const CvaChangeResult rn = ChangeDetector::computeCva( t1.data(), t2.data(), w, h, bands );
  REQUIRE( std::isnan( rn.changeMagnitude[50] ) );
  REQUIRE( rn.binaryChangeMask[50] == 0 );
  REQUIRE_THAT( rn.computedThreshold, WithinAbs( 2.76512, 2e-3 ) );
  changed = 0;
  for ( const uint8_t m : rn.binaryChangeMask )
    changed += m;
  REQUIRE( changed == 10 );
}

TEST_CASE( "PCA difference projects onto the analytic minor axis", "[d15][change]" )
{
  // Differences [2,0], [0,1], [-2,0], [0,-1]: mean 0, covariance
  // diag(2, 0.5) -> minor eigenvector [0,1].  |projections| = 0,1,0,1
  // (the major axis would instead give 2,0,2,0).
  const std::vector<float> t1( 8, 0.0f );
  // BSQ planes: band0 = [2,0,-2,0], band1 = [0,1,0,-1]
  // -> per-pixel differences (2,0) (0,1) (-2,0) (0,-1).
  const std::vector<float> t2 = { 2.0f, 0.0f, -2.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f };
  const auto scores = ChangeDetector::computePcaDifference( t1.data(), t2.data(), 1, 4, 2 );
  REQUIRE( scores.size() == 4 );
  REQUIRE_THAT( scores[0], WithinAbs( 0.0, 1e-6 ) );
  REQUIRE_THAT( scores[1], WithinAbs( 1.0, 1e-6 ) );
  REQUIRE_THAT( scores[2], WithinAbs( 0.0, 1e-6 ) );
  REQUIRE_THAT( scores[3], WithinAbs( 1.0, 1e-6 ) );

  // Rank-deficient differences (all on the line y=2x) carry zero minor
  // energy: every score collapses to 0.
  // BSQ: band0 = [1,2,4], band1 = [2,4,8] -> differences on the line y=2x.
  const std::vector<float> line = { 1.0f, 2.0f, 4.0f, 2.0f, 4.0f, 8.0f };
  const auto flat = ChangeDetector::computePcaDifference( t1.data(), line.data(), 1, 3, 2 );
  for ( const float v : flat )
    REQUIRE_THAT( v, WithinAbs( 0.0, 1e-6 ) );
}
