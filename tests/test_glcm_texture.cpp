// tests/test_glcm_texture.cpp — D15 Package B.
//
// Ground-truth policy: all expected values below are hand-derived closed
// forms on idealised stripe/constant windows (derivation inline).  Nothing
// is recomputed through the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "processing/algorithms/glcm_texture.h"

using Catch::Matchers::WithinAbs;
using rs::processing::GlcmConfig;
using rs::processing::GlcmDirection;
using rs::processing::GlcmHaralickMetrics;
using rs::processing::GlcmTextureCalculator;

namespace
{
  // 4x4 vertical stripes: even columns 1.0, odd columns 0.0.
  std::vector<float> stripeWindow4x4()
  {
    std::vector<float> win( 16 );
    for ( int y = 0; y < 4; ++y )
      for ( int x = 0; x < 4; ++x )
        win[static_cast<size_t>( y ) * 4 + x] = ( x % 2 == 0 ) ? 1.0f : 0.0f;
    return win;
  }

  GlcmConfig twoLevel( GlcmDirection dir )
  {
    GlcmConfig cfg;
    cfg.windowSize = 4;
    cfg.stepDistance = 1;
    cfg.direction = dir;
    cfg.quantLevels = 2;
    cfg.minVal = 0.0f;
    cfg.maxVal = 1.0f;
    return cfg;
  }
} // namespace

TEST_CASE( "GLCM deg0 on ideal stripes hits hand-derived metrics", "[d15][glcm]" )
{
  // Pairs across column boundaries only: P(0,1)=P(1,0)=0.5 exactly.
  // contrast = 1*(0.5+0.5) = 1; homogeneity = 0.5/2+0.5/2 = 0.5;
  // ASM = 0.25+0.25 = 0.5; energy = sqrt(0.5); entropy = ln 2;
  // dissimilarity = 1; mu = 0.5; var = 0.25; correlation = -1 (perfect
  // anti-correlation: i=0 forces j=1).
  const auto win = stripeWindow4x4();
  const auto m = GlcmTextureCalculator::computeForWindow( win, 4, 4, twoLevel( GlcmDirection::Deg0 ) );

  REQUIRE_THAT( m.contrast, WithinAbs( 1.0, 1e-9 ) );
  REQUIRE_THAT( m.homogeneity, WithinAbs( 0.5, 1e-9 ) );
  REQUIRE_THAT( m.angularSecondMoment, WithinAbs( 0.5, 1e-9 ) );
  REQUIRE_THAT( m.energy, WithinAbs( std::sqrt( 0.5 ), 1e-9 ) );
  REQUIRE_THAT( m.entropy, WithinAbs( std::log( 2.0 ), 1e-9 ) );
  REQUIRE_THAT( m.dissimilarity, WithinAbs( 1.0, 1e-9 ) );
  REQUIRE_THAT( m.mean, WithinAbs( 0.5, 1e-9 ) );
  REQUIRE_THAT( m.variance, WithinAbs( 0.25, 1e-9 ) );
  REQUIRE_THAT( m.correlation, WithinAbs( -1.0, 1e-9 ) );
}

TEST_CASE( "GLCM deg90 on ideal stripes is perfectly self-similar", "[d15][glcm]" )
{
  // Vertical neighbours share a column => P(0,0)=P(1,1)=0.5.
  // contrast = 0; homogeneity = 1; correlation = +1; variance = 0.25.
  const auto win = stripeWindow4x4();
  const auto m = GlcmTextureCalculator::computeForWindow( win, 4, 4, twoLevel( GlcmDirection::Deg90 ) );

  REQUIRE_THAT( m.contrast, WithinAbs( 0.0, 1e-9 ) );
  REQUIRE_THAT( m.homogeneity, WithinAbs( 1.0, 1e-9 ) );
  REQUIRE_THAT( m.correlation, WithinAbs( 1.0, 1e-9 ) );
  REQUIRE_THAT( m.variance, WithinAbs( 0.25, 1e-9 ) );
}

TEST_CASE( "GLCM omnidirectional stripes average to closed-form metrics", "[d15][glcm]" )
{
  // Directional P matrices (stripes): deg0/deg45/deg135 -> off-diagonal
  // 0.5/0.5; deg90 -> diagonal 0.5/0.5.  Mean matrix:
  // P(0,0)=P(1,1)=0.125, P(0,1)=P(1,0)=0.375 (sums to 1).
  // contrast = 0.75 (linear in P); homogeneity = 0.375 + 0.25 = 0.625;
  // ASM = 2*0.125^2 + 2*0.375^2 = 0.3125 (quadratic in P - matrix mean
  // first, then metrics, per DECISIONS B2).
  // marginals (0.5, 0.5) => mean = 0.5; variance = 0.25;
  // numerator = (0.5^2)*0.25 - 2*(0.5^2)*0.375 = -0.125
  // correlation = -0.125/(0.5*0.5) = -0.5;
  // entropy = -2*0.125*ln0.125 - 2*0.375*ln0.375 = 1.255482325...
  const auto win = stripeWindow4x4();
  const auto m = GlcmTextureCalculator::computeForWindow( win, 4, 4, twoLevel( GlcmDirection::Omnidirectional ) );

  REQUIRE_THAT( m.contrast, WithinAbs( 0.75, 1e-9 ) );
  REQUIRE_THAT( m.homogeneity, WithinAbs( 0.625, 1e-9 ) );
  REQUIRE_THAT( m.angularSecondMoment, WithinAbs( 0.3125, 1e-9 ) );
  REQUIRE_THAT( m.mean, WithinAbs( 0.5, 1e-9 ) );
  REQUIRE_THAT( m.variance, WithinAbs( 0.25, 1e-9 ) );
  REQUIRE_THAT( m.correlation, WithinAbs( -0.5, 1e-9 ) );
  REQUIRE_THAT( m.entropy, WithinAbs( 1.25548232523931, 1e-9 ) );
}

TEST_CASE( "GLCM constant window saturates to the ordered extreme", "[d15][glcm]" )
{
  std::vector<float> flat( 9, 1.0f );
  GlcmConfig cfg = twoLevel( GlcmDirection::Deg45 );
  cfg.windowSize = 3;
  const auto m = GlcmTextureCalculator::computeForWindow( flat, 3, 3, cfg );

  REQUIRE_THAT( m.contrast, WithinAbs( 0.0, 1e-12 ) );
  REQUIRE_THAT( m.homogeneity, WithinAbs( 1.0, 1e-12 ) );
  REQUIRE_THAT( m.angularSecondMoment, WithinAbs( 1.0, 1e-12 ) );
  REQUIRE_THAT( m.variance, WithinAbs( 0.0, 1e-12 ) );
  REQUIRE( std::abs( m.entropy ) < 1e-9 );  // -1*ln(1+1e-12) ~ -1e-12
  REQUIRE_THAT( m.correlation, WithinAbs( 0.0, 1e-12 ) ); // sigma = 0 guard
}

TEST_CASE( "GLCM quantization clamps the top bin", "[d15][glcm]" )
{
  // [1, 0, 1, 0] with G=4 -> levels [3,0,3,0] (floor(1*4)=4 clamps to 3).
  // P(0,3)=P(3,0)=0.5 => contrast = 9 (a missing clamp would give 16).
  std::vector<float> row = { 1.0f, 0.0f, 1.0f, 0.0f };
  GlcmConfig cfg = twoLevel( GlcmDirection::Deg0 );
  cfg.quantLevels = 4;
  const auto m = GlcmTextureCalculator::computeForWindow( row, 4, 1, cfg );
  REQUIRE_THAT( m.contrast, WithinAbs( 9.0, 1e-9 ) );
  REQUIRE_THAT( m.dissimilarity, WithinAbs( 3.0, 1e-9 ) );
}

TEST_CASE( "GLCM normalized co-occurrence conserves probability mass", "[d15][glcm]" )
{
  const auto win = stripeWindow4x4();
  for ( const GlcmDirection dir : { GlcmDirection::Deg0, GlcmDirection::Deg90, GlcmDirection::Omnidirectional } )
  {
    int levels = 0;
    const auto p = GlcmTextureCalculator::normalizedCooccurrence( win, 4, 4, twoLevel( dir ), levels );
    REQUIRE( levels == 2 );
    double sum = 0.0;
    for ( const double v : p )
      sum += v;
    REQUIRE_THAT( sum, WithinAbs( 1.0, 1e-10 ) );
  }
}

TEST_CASE( "GLCM non-finite or empty windows yield NaN sentinels", "[d15][glcm]" )
{
  std::vector<float> dirty = stripeWindow4x4();
  dirty[5] = std::nanf( "" );
  const auto m = GlcmTextureCalculator::computeForWindow( dirty, 4, 4, twoLevel( GlcmDirection::Deg0 ) );
  REQUIRE( std::isnan( m.contrast ) );
  REQUIRE( std::isnan( m.homogeneity ) );

  const auto empty = GlcmTextureCalculator::computeForWindow( {}, 0, 0, twoLevel( GlcmDirection::Deg0 ) );
  REQUIRE( std::isnan( empty.contrast ) );
}

TEST_CASE( "GLCM feature map is exact in interiors and NaN-propagating", "[d15][glcm]" )
{
  // 6x6 stripes, window 3, deg0: every fully-inside 3x3 window sees the same
  // (p,q,p) column pattern -> contrast exactly 1.
  std::vector<float> raster( 36 );
  for ( int y = 0; y < 6; ++y )
    for ( int x = 0; x < 6; ++x )
      raster[static_cast<size_t>( y ) * 6 + x] = ( x % 2 == 0 ) ? 1.0f : 0.0f;

  GlcmConfig cfg = twoLevel( GlcmDirection::Deg0 );
  cfg.windowSize = 3;
  const auto map = GlcmTextureCalculator::computeTextureFeatureMap( raster.data(), 6, 6, cfg, "contrast" );
  REQUIRE( map.size() == 36 );
  for ( int y = 1; y <= 4; ++y )
    for ( int x = 1; x <= 4; ++x )
      REQUIRE_THAT( map[static_cast<size_t>( y ) * 6 + x], WithinAbs( 1.0, 1e-9 ) );
  for ( const float v : map )
    REQUIRE( std::isfinite( v ) );

  // A single NaN poisons exactly the (2k+1)^2 windows containing it.
  std::vector<float> flat( 36, 0.25f );
  flat[static_cast<size_t>( 2 ) * 6 + 2] = std::nanf( "" );
  const auto nanMap = GlcmTextureCalculator::computeTextureFeatureMap( flat.data(), 6, 6, cfg, "contrast" );
  int nanCount = 0;
  for ( const float v : nanMap )
    nanCount += std::isnan( v ) ? 1 : 0;
  REQUIRE( nanCount == 9 );

  const auto bogus = GlcmTextureCalculator::computeTextureFeatureMap( raster.data(), 6, 6, cfg, "bogus" );
  REQUIRE( std::isnan( bogus[static_cast<size_t>( 3 ) * 6 + 3] ) );
}
