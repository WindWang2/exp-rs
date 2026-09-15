// test_uncertainty.cpp — F12 Oracle 2 (uncertainty definitions machine-verifiable):
// every expected value below is hand-derived from the definitions in rs_uncertainty.h.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_uncertainty.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;

TEST_CASE( "entropy: uniform row over K classes is log2(K)", "[classify][uncertainty]" )
{
  double h = 0.0;
  const std::vector<float> uniform3 = { 1.0f / 3, 1.0f / 3, 1.0f / 3 };
  REQUIRE( RsUncertainty::entropy( uniform3, h ) );
  REQUIRE( h == Approx( std::log2( 3.0 ) ).margin( 1e-12 ) );

  const std::vector<float> uniform4 = { 0.25f, 0.25f, 0.25f, 0.25f };
  REQUIRE( RsUncertainty::entropy( uniform4, h ) );
  REQUIRE( h == Approx( 2.0 ).margin( 1e-12 ) ); // log2(4) = 2 exactly
}

TEST_CASE( "entropy: deterministic row is 0 and zero entries contribute nothing", "[classify][uncertainty]" )
{
  double h = 0.0;
  REQUIRE( RsUncertainty::entropy( { 1.0f, 0.0f, 0.0f }, h ) );
  REQUIRE( h == Approx( 0.0 ).margin( 1e-12 ) );

  // 0·log2(0) := 0 — explicit zero entries are legal, not NaN.
  REQUIRE( RsUncertainty::entropy( { 0.5f, 0.5f, 0.0f }, h ) );
  REQUIRE( h == Approx( 1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "entropy: known mixed value (0.5, 0.25, 0.25) == 1.5 bits", "[classify][uncertainty]" )
{
  double h = 0.0;
  REQUIRE( RsUncertainty::entropy( { 0.5f, 0.25f, 0.25f }, h ) );
  REQUIRE( h == Approx( 1.5 ).margin( 1e-12 ) );
}

TEST_CASE( "entropy: fails closed on negative, non-finite, unnormalised rows", "[classify][uncertainty]" )
{
  double h = 0.0;
  REQUIRE( !RsUncertainty::entropy( { 0.5f, 0.7f, -0.2f }, h ) );
  REQUIRE( !RsUncertainty::entropy( { 0.5f, std::numeric_limits<float>::quiet_NaN(), 0.5f }, h ) );
  REQUIRE( !RsUncertainty::entropy( { 0.5f, 0.5f, 0.5f }, h ) ); // sum 1.5
}

TEST_CASE( "margin: top1 minus top2 in descending order", "[classify][uncertainty]" )
{
  double m = 0.0;
  REQUIRE( RsUncertainty::margin( { 0.6f, 0.3f, 0.1f }, m ) );
  REQUIRE( m == Approx( 0.3 ).margin( 1e-12 ) );

  // Tie at the top gives margin 0 (oracle: 0.5-0.5).
  REQUIRE( RsUncertainty::margin( { 0.5f, 0.5f }, m ) );
  REQUIRE( m == Approx( 0.0 ).margin( 1e-12 ) );

  // Single-class row: margin defined as 0 (no second class exists).
  REQUIRE( RsUncertainty::margin( { 1.0f }, m ) );
  REQUIRE( m == Approx( 0.0 ).margin( 1e-12 ) );

  // Unsorted input: the two largest values win regardless of position.
  REQUIRE( RsUncertainty::margin( { 0.1f, 0.6f, 0.3f }, m ) );
  REQUIRE( m == Approx( 0.3 ).margin( 1e-12 ) );
}

TEST_CASE( "confidence: max probability", "[classify][uncertainty]" )
{
  double c = 0.0;
  REQUIRE( RsUncertainty::confidence( { 0.2f, 0.7f, 0.1f }, c ) );
  REQUIRE( c == Approx( 0.7 ).margin( 1e-12 ) );
}

TEST_CASE( "normalizeEntropy: divides by log2(K), degenerate K guards", "[classify][uncertainty]" )
{
  REQUIRE( RsUncertainty::normalizeEntropy( 2.0, 4 ) == Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( RsUncertainty::normalizeEntropy( 1.5, 3 ) == Approx( 1.5 / std::log2( 3.0 ) ).margin( 1e-12 ) );
  REQUIRE( RsUncertainty::normalizeEntropy( 5.0, 1 ) == 0.0 );
  REQUIRE( RsUncertainty::normalizeEntropy( 5.0, 0 ) == 0.0 );
}

TEST_CASE( "reject policy: direction locked per measure (double-sided)", "[classify][uncertainty]" )
{
  // Entropy: high = uncertain.
  REQUIRE( RsUncertainty::isRejected( RsUncertainty::Measure::Entropy, 0.9, 0.8 ) );
  REQUIRE( !RsUncertainty::isRejected( RsUncertainty::Measure::Entropy, 0.7, 0.8 ) );
  // Boundary value counts as rejected (>=).
  REQUIRE( RsUncertainty::isRejected( RsUncertainty::Measure::Entropy, 0.8, 0.8 ) );

  // Margin / Confidence: low = uncertain.
  REQUIRE( RsUncertainty::isRejected( RsUncertainty::Measure::Margin, 0.1, 0.2 ) );
  REQUIRE( !RsUncertainty::isRejected( RsUncertainty::Measure::Margin, 0.3, 0.2 ) );
  REQUIRE( RsUncertainty::isRejected( RsUncertainty::Measure::Margin, 0.2, 0.2 ) ); // <=
  REQUIRE( RsUncertainty::isRejected( RsUncertainty::Measure::Confidence, 0.4, 0.5 ) );
  REQUIRE( !RsUncertainty::isRejected( RsUncertainty::Measure::Confidence, 0.6, 0.5 ) );
}

TEST_CASE( "ensembleDisagreement: hand-computed population-variance average", "[classify][uncertainty]" )
{
  // Oracle by hand, K=2, m=2 members:
  //  member0: (1.0, 0.0), member1: (0.0, 1.0)
  //  μ = (0.5, 0.5); per-class var (population, /m): (0.25+0.25)/2 = 0.25 each
  //  D = (0.25 + 0.25)/2 = 0.25 — the theoretical maximum for K=2.
  double d = 0.0;
  REQUIRE( RsUncertainty::ensembleDisagreement( { 1.0f, 0.0f, 0.0f, 1.0f }, 2, 2, d ) );
  REQUIRE( d == Approx( 0.25 ).margin( 1e-12 ) );

  // Identical members → 0 disagreement even at high entropy.
  REQUIRE( RsUncertainty::ensembleDisagreement( { 0.5f, 0.5f, 0.5f, 0.5f }, 2, 2, d ) );
  REQUIRE( d == Approx( 0.0 ).margin( 1e-12 ) );

  // Three members on one class: (1,0),(0.5,0.5),(0,1):
  //  μ0 = 0.5, var0 = (0.25+0+0.25)/3; μ1 symmetric; D = 0.5/3*2/2 = 1/6.
  REQUIRE( RsUncertainty::ensembleDisagreement(
             { 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f }, 3, 2, d ) );
  REQUIRE( d == Approx( 1.0 / 6.0 ).margin( 1e-12 ) );
}

TEST_CASE( "ensembleDisagreement: fails closed on invalid input", "[classify][uncertainty]" )
{
  double d = 0.0;
  REQUIRE( !RsUncertainty::ensembleDisagreement( { 1.0f, 0.0f }, 1, 2, d ) ); // m < 2
  REQUIRE( !RsUncertainty::ensembleDisagreement( { 1.0f, 0.0f }, 2, 2, d ) ); // size mismatch
  REQUIRE( !RsUncertainty::ensembleDisagreement( { 1.0f, 0.0f, 0.0f, -1.0f }, 2, 2, d ) ); // negative
}

TEST_CASE( "measure(): dispatch returns the same values as direct calls", "[classify][uncertainty]" )
{
  const std::vector<float> row = { 0.6f, 0.3f, 0.1f };
  double v = 0.0;
  REQUIRE( RsUncertainty::measure( RsUncertainty::Measure::Confidence, row, v ) );
  REQUIRE( v == Approx( 0.6 ).margin( 1e-12 ) );
  REQUIRE( RsUncertainty::measure( RsUncertainty::Measure::Margin, row, v ) );
  REQUIRE( v == Approx( 0.3 ).margin( 1e-12 ) );
  const double expectH = -( 0.6 * std::log2( 0.6 ) + 0.3 * std::log2( 0.3 ) + 0.1 * std::log2( 0.1 ) );
  REQUIRE( RsUncertainty::measure( RsUncertainty::Measure::Entropy, row, v ) );
  REQUIRE( v == Approx( expectH ).margin( 1e-12 ) );
}
