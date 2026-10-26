// test_r22_science_numerics.cpp — R22-01 science track regression anchors
// for #1707 (exact Euclidean distance transform) and #1684 (accumulator
// statistics), plus a >3000-case brute-force differential oracle for the
// distance transform.
//
// RED provenance: every case below failed on master@911e0b17 (see
// .planning/r22-science/RED_GREEN.md); they are the minimal independent
// reproductions from the issues, extended with randomized differential
// coverage.

#include "processing/algorithms/primitives/distance_transform.h"
#include "processing/algorithms/math_utils.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace
{

/// Fills a width×height byte mask from rows of '0'/'1'.
std::vector<uint8_t> maskFrom( int width, int height, const char *const *rows )
{
  std::vector<uint8_t> mask( static_cast<size_t>( width ) * height, 0 );
  for ( int y = 0; y < height; ++y )
    for ( int x = 0; x < width; ++x )
      mask[static_cast<size_t>( y ) * width + x] = rows[y][x] == '1' ? 1 : 0;
  return mask;
}

/// Brute-force nearest-foreground squared distance (O(N²) reference oracle).
double bruteSquared( const std::vector<uint8_t> &mask, int width, int height, int x, int y )
{
  double best = std::numeric_limits<double>::infinity();
  for ( int yy = 0; yy < height; ++yy )
    for ( int xx = 0; xx < width; ++xx )
    {
      if ( mask[static_cast<size_t>( yy ) * width + xx] != 1 )
        continue;
      const double dx = xx - x, dy = yy - y;
      const double d2 = dx * dx + dy * dy;
      if ( d2 < best )
        best = d2;
    }
  return best;
}

} // namespace

// ---------------------------------------------------------------------------
// #1707 — squaredDistancePass evaluation overwrote f in place, so parabola
// heights f[v[j]] read back already-transformed values.
// ---------------------------------------------------------------------------

TEST_CASE( "r22/#1707: issue 3x4 counterexample gives exact distances",
           "[r22_science][distance][primitives]" )
{
  using namespace sicnu::rs::primitives;
  const char *rows[] = { "010", "001", "000", "000" };
  const auto mask = maskFrom( 3, 4, rows );
  std::vector<float> out( 12, -1.0f );
  REQUIRE( distanceToForeground( mask.data(), 3, 4, out.data() ) );

  // Hand-derived squared distances (fg at (0,1) and (1,2)):
  //   1 0 1
  //   2 1 0
  //   5 2 1
  //   8 5 4      <- (3,0) is 8 (to (1,2)), NOT 6.
  const double expected2[4][3] = { { 1, 0, 1 }, { 2, 1, 0 }, { 5, 2, 1 }, { 8, 5, 4 } };
  for ( int y = 0; y < 4; ++y )
    for ( int x = 0; x < 3; ++x )
    {
      const float d = out[static_cast<size_t>( y ) * 3 + x];
      REQUIRE( d == Catch::Approx( std::sqrt( expected2[y][x] ) ).margin( 1e-6 ) );
    }
}

TEST_CASE( "r22/#1707: randomized differential vs brute force (3000+ masks)",
           "[r22_science][distance][primitives]" )
{
  using namespace sicnu::rs::primitives;
  // Fixed seed: the point is regression pinning, not sampling theory. The
  // distributions here are std:: (test-local); that is fine because the
  // oracle is self-consistent — whatever masks this run generates are
  // brute-force-compared within the same run.
  std::mt19937 rng( 20261025u );
  std::uniform_int_distribution<int> wgen( 1, 20 );
  std::uniform_int_distribution<int> hgen( 1, 20 );
  std::uniform_int_distribution<int> fgen( 0, 100 );

  int cases = 0;
  int withForeground = 0;
  while ( cases < 3200 || withForeground < 3000 )
  {
    const int w = wgen( rng ), h = hgen( rng );
    std::vector<uint8_t> mask( static_cast<size_t>( w ) * h, 0 );
    bool any = false;
    for ( auto &m : mask )
    {
      m = ( fgen( rng ) < 45 ) ? 1 : 0; // ~45% foreground density
      any = any || m == 1;
    }
    ++cases;
    if ( any )
      ++withForeground;

    std::vector<float> out( mask.size(), -1.0f );
    REQUIRE( distanceToForeground( mask.data(), w, h, out.data() ) );
    for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
        const double ref2 = bruteSquared( mask, w, h, x, y );
        const float got = out[static_cast<size_t>( y ) * w + x];
        if ( std::isinf( ref2 ) )
        {
          REQUIRE( std::isinf( got ) );
        }
        else
        {
          // Exact-integer squared distances: compare through the float
          // round-trip with a margin far below any real defect (the #1707
          // bug produced errors of 2+ in squared units).
          REQUIRE( got == Catch::Approx( std::sqrt( ref2 ) ).margin( 1e-4 ) );
        }
      }
  }
  REQUIRE( cases >= 3200 );
  REQUIRE( withForeground >= 3000 );
}

// ---------------------------------------------------------------------------
// #1684 — computeStatsFromAccumulators truncated the mean to float before
// forming sumSq/N - mean^2, destroying stddev for large-offset small-spread
// populations.
// ---------------------------------------------------------------------------

TEST_CASE( "r22/#1684: large offset, small variance (10000.3 +/- 0.5)",
           "[r22_science][stats][math_utils]" )
{
  MathUtils::AccumulatorStats acc;
  acc.count = 0;
  acc.sum = 0.0;
  acc.sumSq = 0.0;
  acc.min = std::numeric_limits<float>::max();
  acc.max = std::numeric_limits<float>::lowest();
  for ( size_t i = 0; i < 1000000; ++i )
  {
    const double v = ( i % 2 == 0 ) ? 10000.8 : 9999.8;
    acc.count += 1;
    acc.sum += v;
    acc.sumSq += v * v;
    acc.min = std::min( acc.min, static_cast<float>( v ) );
    acc.max = std::max( acc.max, static_cast<float>( v ) );
  }
  const MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators( acc );
  // True population sigma is exactly 0.5. With the float-mean truncation the
  // result was 0.584 (+17%, the issue repro); the double-mean form leaves
  // only the caller-side accumulation noise of these naive sums (<0.2%).
  REQUIRE( stats.mean == Catch::Approx( 10000.3f ).epsilon( 1e-6 ) );
  REQUIRE( stats.stddev == Catch::Approx( 0.5 ).epsilon( 0.01 ) );
}

TEST_CASE( "r22/#1684: constant population has exact zero stddev",
           "[r22_science][stats][math_utils]" )
{
  MathUtils::AccumulatorStats acc;
  acc.count = 1000;
  acc.sum = 1000.0 * 1234.5;
  acc.sumSq = 1000.0 * 1234.5 * 1234.5;
  acc.min = acc.max = 1234.5f;
  const MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators( acc );
  REQUIRE( stats.mean == Catch::Approx( 1234.5f ).epsilon( 1e-7 ) );
  REQUIRE( stats.stddev == 0.0f );
}

TEST_CASE( "r22/#1684: N=1 population has zero stddev and exact mean",
           "[r22_science][stats][math_utils]" )
{
  MathUtils::AccumulatorStats acc;
  acc.count = 1;
  acc.sum = -7.25;
  acc.sumSq = 52.5625;
  acc.min = acc.max = -7.25f;
  const MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators( acc );
  REQUIRE( stats.mean == Catch::Approx( -7.25f ) );
  REQUIRE( stats.stddev == 0.0f );
}

TEST_CASE( "r22/#1684: 1e6 offset with 0.25 spread — formula exactness",
           "[r22_science][stats][math_utils]" )
{
  // Exercises the variance FORMULA at mean=1e6, σ=0.25 (condition number
  // mean²/σ² = 1.6e13). Sums are reconstructed exactly around the offset c
  // (Σ(v−c)², Σ(v−c)) so the test measures computeStatsFromAccumulators,
  // not the rounding a naive caller-side Σv² accumulation would inject:
  // sumSq/N − mean² satisfies Sterbenz (ratio within [½,2]) and subtracts
  // exactly, so any error here is a real formula defect. Callers that
  // accumulate Σv² naively over millions of pixels retain a ~√N·ε floor —
  // documented in math_utils.h; production accumulators should sum shifted
  // values or use Kahan compensation (see rs_segment_features.cpp).
  constexpr double c = 1000000.0;
  constexpr int N = 200000;
  double s1 = 0.0, s2 = 0.0;
  for ( int i = 0; i < N; ++i )
  {
    const double d = ( i % 2 == 0 ) ? 0.25 : -0.25;
    s1 += d;
    s2 += d * d;
  }
  MathUtils::AccumulatorStats acc;
  acc.count = N;
  acc.sum = c * N + s1;
  acc.sumSq = c * c * N + 2.0 * c * s1 + s2;
  acc.min = 999999.75f;
  acc.max = 1000000.25f;
  const MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators( acc );
  REQUIRE( stats.mean == Catch::Approx( 1000000.0 ).epsilon( 1e-12 ) );
  REQUIRE( stats.stddev == Catch::Approx( 0.25 ).epsilon( 1e-4 ) );
}

TEST_CASE( "r22/#1684: small-scale sanity stays exact",
           "[r22_science][stats][math_utils]" )
{
  // {1,2,3,4}: mean 2.5, population variance 1.25, sigma sqrt(1.25).
  MathUtils::AccumulatorStats acc;
  acc.count = 4;
  acc.sum = 10.0;
  acc.sumSq = 30.0;
  acc.min = 1.0f;
  acc.max = 4.0f;
  const MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators( acc );
  REQUIRE( stats.mean == Catch::Approx( 2.5f ) );
  REQUIRE( stats.stddev == Catch::Approx( std::sqrt( 1.25 ) ).margin( 1e-6 ) );
  REQUIRE( stats.min == 1.0f );
  REQUIRE( stats.max == 4.0f );
  REQUIRE( stats.count == 4 );
  REQUIRE( stats.validCount == 4 );
}
