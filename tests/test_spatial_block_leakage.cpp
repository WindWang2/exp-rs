// tests/test_spatial_block_leakage.cpp — D15 Package A.
//
// Ground-truth policy: every expected number below is hand-derived in
// closed form; nothing is recomputed by calling the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/spatial_split.h"

using Catch::Matchers::WithinAbs;
using rs::core::SampleRole;
using rs::core::SpatialAutocorrelationAuditor;
using rs::core::SpatialBlockConfig;
using rs::core::SpatialBlockPartitioner;
using rs::core::SpatialSamplePoint;
using rs::core::SpatialSplitReport;

namespace
{
  // 3x3 unit-spaced lattice, row-major: (0,0)..(2,2).
  std::vector<SpatialSamplePoint> lattice3x3()
  {
    std::vector<SpatialSamplePoint> pts;
    for ( int y = 0; y < 3; ++y )
      for ( int x = 0; x < 3; ++x )
      {
        SpatialSamplePoint p;
        p.id = static_cast<int64_t>( pts.size() );
        p.x = static_cast<double>( x );
        p.y = static_cast<double>( y );
        pts.push_back( p );
      }
    return pts;
  }

  // Deterministic test-fixture jitter (own LCG — fixture *generation* may
  // compute; only expected assertion values must not derive from the code
  // under test).
  class Lcg
  {
    public:
      explicit Lcg( uint64_t seed ) : m_state( seed ) {}
      double nextBipolar()
      {
        m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>( ( m_state >> 33 ) & 0xFFFFFF ) / 8388608.0 - 1.0;
      }

    private:
      uint64_t m_state;
  };

  // Four 50-point clusters centred one 100-unit block apart, jittered so
  // every point stays inside its own block.
  std::vector<SpatialSamplePoint> fourClusterSamples( uint64_t seed )
  {
    static const double kCenters[4][2] = { { 50.0, 50.0 }, { 150.0, 50.0 }, { 50.0, 150.0 }, { 150.0, 150.0 } };
    Lcg lcg( seed );
    std::vector<SpatialSamplePoint> pts;
    pts.reserve( 200 );
    for ( int i = 0; i < 200; ++i )
    {
      const auto &c = kCenters[i % 4];
      SpatialSamplePoint p;
      p.id = i;
      p.x = c[0] + lcg.nextBipolar() * 10.0;
      p.y = c[1] + lcg.nextBipolar() * 10.0;
      p.label = i % 2;
      pts.push_back( p );
    }
    return pts;
  }
} // namespace

TEST_CASE( "Moran's I matches the closed-form lattice solution", "[d15][spatial]" )
{
  // 3x3 lattice at unit spacing, z = 1,1,1 / 0,0,0 / -1,-1,-1, cutoff 1.5.
  // Hand derivation (w_ij = 1/d_ij inside the cutoff, ordered pairs):
  //   neighbour pairs: 12 orthogonal (d=1, w=1) + 8 diagonal (d=sqrt2,
  //   w=1/sqrt2) => S0 = 2*(12 + 8/sqrt2) = 24 + 8*sqrt2.
  //   zbar = 0; sum((z-zbar)^2) = 6 (three +1, three 0, three -1).
  //   unordered sum(w*z_i*z_j): only the four horizontal edges inside the
  //   z=+-1 rows contribute (each 1*1) => 4; vertical and diagonal products
  //   are all 0 => ordered sum = 8.
  //   I = (9 / (24+8sqrt2)) * (8/6) = 12 / (24+8sqrt2)
  //     = 3*(3 - sqrt2)/14 = 0.3398113794914797
  //   (cross-checked against a brute-force reference implementation of the
  //   definition after two hand-derivation slips; see REVIEW_LOG.)
  const double kExpectedI = 3.0 * ( 3.0 - std::sqrt( 2.0 ) ) / 14.0;
  auto pts = lattice3x3();
  std::vector<double> z = { 1, 1, 1, 0, 0, 0, -1, -1, -1 };

  const double i = SpatialAutocorrelationAuditor::computeMoransI( pts, z, 1.5 );
  REQUIRE_THAT( i, WithinAbs( kExpectedI, 1e-10 ) );
}

TEST_CASE( "Moran's I degenerate guards return neutral zero", "[d15][spatial]" )
{
  auto pts = lattice3x3();
  std::vector<double> constant( 9, 3.0 );
  REQUIRE_THAT( SpatialAutocorrelationAuditor::computeMoransI( pts, constant, 1.5 ), WithinAbs( 0.0, 1e-12 ) );

  // Cutoff below the nearest-neighbour distance: S0 = 0.
  std::vector<double> z = { 1, 1, 1, 0, 0, 0, -1, -1, -1 };
  REQUIRE_THAT( SpatialAutocorrelationAuditor::computeMoransI( pts, z, 0.5 ), WithinAbs( 0.0, 1e-12 ) );

  // Too few points.
  std::vector<SpatialSamplePoint> solo( 1 );
  solo[0].x = 1.0;
  solo[0].y = 1.0;
  std::vector<double> zSolo = { 2.0 };
  REQUIRE_THAT( SpatialAutocorrelationAuditor::computeMoransI( solo, zSolo, 10.0 ), WithinAbs( 0.0, 1e-12 ) );
  REQUIRE_THAT( SpatialAutocorrelationAuditor::computeMoransI( {}, {}, 10.0 ), WithinAbs( 0.0, 1e-12 ) );
}

TEST_CASE( "Block partition keeps train strictly isolated from evaluation sets", "[d15][spatial]" )
{
  SpatialBlockConfig cfg;
  cfg.bufferDistance = 25.0;
  auto samples = fourClusterSamples( 20260914ULL );

  const SpatialSplitReport report = SpatialBlockPartitioner().partition( samples, cfg );

  // Every sample ends in exactly one non-Unassigned role.
  REQUIRE( report.assignments.size() == samples.size() );
  REQUIRE( report.trainCount + report.valCount + report.testCount + report.bufferCount == samples.size() );
  REQUIRE( report.trainCount > 0 );
  REQUIRE( report.valCount + report.testCount > 0 );

  // Independent recomputation of the isolation invariant over the public
  // role output: every retained evaluation sample is farther than the
  // buffer from every train sample, and the report's audit distance agrees.
  double minEvalDistance = std::numeric_limits<double>::infinity();
  for ( size_t a = 0; a < samples.size(); ++a )
  {
    if ( report.assignments[a] != SampleRole::Train )
      continue;
    for ( size_t b = 0; b < samples.size(); ++b )
    {
      const SampleRole rb = report.assignments[b];
      if ( rb != SampleRole::Validation && rb != SampleRole::Test )
        continue;
      const double dx = samples[a].x - samples[b].x;
      const double dy = samples[a].y - samples[b].y;
      minEvalDistance = std::min( minEvalDistance, std::sqrt( dx * dx + dy * dy ) );
    }
  }
  REQUIRE( minEvalDistance > cfg.bufferDistance );
  REQUIRE_THAT( report.minTrainTestDistance, WithinAbs( minEvalDistance, 1e-9 ) );

  for ( const SampleRole r : report.assignments )
    REQUIRE( r != SampleRole::Unassigned );
}

TEST_CASE( "Block partition is deterministic for a fixed seed", "[d15][spatial]" )
{
  auto samples = fourClusterSamples( 7ULL );
  SpatialBlockConfig cfg;
  const SpatialSplitReport a = SpatialBlockPartitioner().partition( samples, cfg );
  const SpatialSplitReport b = SpatialBlockPartitioner().partition( samples, cfg );
  REQUIRE( a.assignments == b.assignments );
  REQUIRE( a.trainCount == b.trainCount );
  REQUIRE( a.bufferCount == b.bufferCount );
}

TEST_CASE( "Partition degenerate inputs produce empty reports", "[d15][spatial]" )
{
  SpatialBlockConfig cfg;
  const SpatialSplitReport empty = SpatialBlockPartitioner().partition( {}, cfg );
  REQUIRE( empty.assignments.empty() );
  REQUIRE( empty.trainCount == 0 );
  REQUIRE( empty.minTrainTestDistance == 0.0 );

  SpatialBlockConfig broken;
  broken.blockWidth = 0.0; // degenerate grid -> refuse, do not crash
  auto samples = fourClusterSamples( 1ULL );
  const SpatialSplitReport bad = SpatialBlockPartitioner().partition( samples, broken );
  REQUIRE( bad.assignments.empty() );
}
