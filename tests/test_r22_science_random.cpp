// test_r22_science_random.cpp — R22-01 science track golden anchors for
// the portable deterministic-random primitives (#1685).
//
// The expected values below were generated once with a standalone
// reference program compiled against plain libstdc++ std::mt19937 (raw
// engine draws ARE standard-pinned, [rand.eng.mersenne]); every fixed
// algorithm in portable_random.h is pinned by these sequences so any STL
// or compiler change that perturbs them fails here instead of silently
// changing train/test splits, reservoirs, PPI projections or RANSAC
// samples (see #1685).

#include "processing/algorithms/portable_random.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

using namespace rs::algorithms::portable;

TEST_CASE( "r22/#1685: golden u32/u64/bounded sequences (seed 42)",
           "[r22_science][random]" )
{
  std::mt19937 rng( 42 );

  // First eight raw engine draws of mt19937(42).
  const std::uint32_t expectedU32[8] = { 1608637542u, 3421126067u, 4083286876u,
                                         787846414u,  3143890026u, 3348747335u,
                                         2571218620u, 2563451924u };
  for ( const std::uint32_t e : expectedU32 )
    REQUIRE( rng() == e );

  std::mt19937 rng64( 42 );
  const std::uint64_t expectedU64[4] = { 6909045637428952499ull,
                                         17537583593393853710ull,
                                         13502904847239337031ull,
                                         11043299886329703444ull };
  for ( const std::uint64_t e : expectedU64 )
    REQUIRE( nextU64( rng64 ) == e );

  std::mt19937 rngB( 42 );
  const std::uint32_t expectedBounded[8] = { 42, 67, 76, 14, 26, 35, 20, 24 };
  for ( const std::uint32_t e : expectedBounded )
    REQUIRE( boundedBelow( rngB, 100 ) == e );
}

TEST_CASE( "r22/#1685: golden uniformUnit53 and normal01 (seed 42)",
           "[r22_science][random]" )
{
  std::mt19937 rngU( 42 );
  const double expectedUnit[4] = { 0.37454011449509828, 0.95071431160518771,
                                   0.73199393851209682, 0.59865848640837938 };
  for ( const double e : expectedUnit )
    REQUIRE( uniformUnit53( rngU ) == Catch::Approx( e ).margin( 1e-15 ) );

  std::mt19937 rngN( 42 );
  const double expectedNormal[4] = { 1.3348056210763137, -0.64294568721741674,
                                     1.0734766584120987, 1.5908981597418601 };
  for ( const double e : expectedNormal )
    REQUIRE( normal01( rngN ) == Catch::Approx( e ).margin( 1e-12 ) );
}

TEST_CASE( "r22/#1685: golden Fisher-Yates permutations (seeds 42 and 7)",
           "[r22_science][random]" )
{
  std::mt19937 rng( 42 );
  std::vector<int> v{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  fisherYatesShuffle( rng, v );
  const int expected[10] = { 1, 3, 9, 7, 6, 0, 8, 4, 5, 2 };
  for ( size_t i = 0; i < 10; ++i )
    REQUIRE( v[i] == expected[i] );

  // Result is a permutation of the input.
  std::vector<int> sorted = v;
  std::sort( sorted.begin(), sorted.end() );
  for ( int i = 0; i < 10; ++i )
    REQUIRE( sorted[static_cast<size_t>( i )] == i );

  std::mt19937 rng7( 7 );
  std::vector<int> w{ 0, 1, 2, 3, 4 };
  fisherYatesShuffle( rng7, w );
  const int expected7[5] = { 2, 3, 1, 4, 0 };
  for ( size_t i = 0; i < 5; ++i )
    REQUIRE( w[i] == expected7[i] );
}

TEST_CASE( "r22/#1685: same seed replays, different seeds diverge",
           "[r22_science][random]" )
{
  std::mt19937 a( 99 ), b( 99 ), c( 100 );
  std::vector<std::uint64_t > va, vb, vc;
  for ( int i = 0; i < 16; ++i )
  {
    va.push_back( nextU64( a ) );
    vb.push_back( nextU64( b ) );
    vc.push_back( nextU64( c ) );
  }
  REQUIRE( va == vb );
  REQUIRE( va != vc );
}

TEST_CASE( "r22/#1685: normal01 moments over 200k draws (seed 123)",
           "[r22_science][random]" )
{
  std::mt19937 rng( 123 );
  double sum = 0.0, sumSq = 0.0;
  const int n = 200000;
  for ( int i = 0; i < n; ++i )
  {
    const double x = normal01( rng );
    REQUIRE( std::isfinite( x ) );
    sum += x;
    sumSq += x * x;
  }
  const double mean = sum / n;
  const double sigma = std::sqrt( sumSq / n - mean * mean );
  REQUIRE( std::abs( mean ) < 0.01 );
  REQUIRE( sigma == Catch::Approx( 1.0 ).epsilon( 0.005 ) );
}

TEST_CASE( "r22/#1685: uniformIndex and fisherYatesShuffleRange contracts",
           "[r22_science][random]" )
{
  std::mt19937 rng( 2026 );
  // uniformIndex(0) and uniformIndex(1) are trivially 0 and consume no
  // ambiguity about the bound.
  REQUIRE( uniformIndex( rng, 0 ) == 0 );
  REQUIRE( uniformIndex( rng, 1 ) == 0 );
  for ( int i = 0; i < 1000; ++i )
  {
    const std::size_t bound = 1 + ( rng() % 1000 );
    const std::size_t idx = uniformIndex( rng, bound );
    REQUIRE( idx < bound );
  }

  // Range-based shuffle over a plain array.
  int arr[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
  fisherYatesShuffleRange( rng, arr, arr + 8 );
  std::sort( arr, arr + 8 );
  for ( int i = 0; i < 8; ++i )
    REQUIRE( arr[i] == i );

  // Shuffle of an empty and a single-element container is a no-op.
  std::vector<int> empty;
  fisherYatesShuffle( rng, empty );
  REQUIRE( empty.empty() );
  std::vector<int> one{ 5 };
  fisherYatesShuffle( rng, one );
  REQUIRE( one[0] == 5 );
}
