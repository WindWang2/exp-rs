// tests/test_temporal_phenology_multi.cpp — known-answer tests for the
// Phenology 2.0 automatic multi-cycle kernel (package D). Oracles are the
// closed-form corpus shapes (peak doys, cycle counts, gap arithmetic) —
// never the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_fit.h"
#include "temporal_corpus.h"

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;

namespace
{
constexpr double kSigma = 0.01;
} // namespace

TEST_CASE( "phenology multi: double-cropping shape yields two cycles per year",
           "[temporal][phenology2]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::doubleSeason( 3, 0.2, 0.8, kSigma, 20260930u );
  const PhenologyMultiResult result = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  REQUIRE( result.valid );
  CHECK( result.cyclesPerYearMax == 2 );
  // At least two complete (interior) season years produced valid cycles.
  int validCycles = 0;
  for ( const PhenologyCycle &cycle : result.cycles )
    if ( cycle.quality.valid )
      ++validCycles;
  CHECK( validCycles >= 4 );
  // Cycle indices are chronological per season year starting at 0.
  for ( int y = 2020; y <= 2022; ++y )
  {
    int seen = 0;
    for ( const PhenologyCycle &cycle : result.cycles )
      if ( cycle.seasonYear == y )
      {
        CHECK( cycle.cycleIndex == seen );
        ++seen;
      }
  }
}

TEST_CASE( "phenology multi: wrapped windows report the harvest year",
           "[temporal][phenology2][crossyear]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::winterCrossYear( 4, 0.2, 0.7, kSigma, 20260931u );
  const PhenologyMultiResult result = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  REQUIRE( result.valid );
  REQUIRE( !result.cycles.empty() );
  // The winter peak (doy ~15) must generate a WRAPPED window (start > end).
  bool sawWrapped = false;
  for ( const PhenologyCycle &cycle : result.cycles )
  {
    if ( cycle.window.startDoy > cycle.window.endDoy )
    {
      sawWrapped = true;
      // A valid wrapped cycle's seasonYear is the year of the window END:
      // its metrics' POS must sit in the low-doy part of that year.
      if ( cycle.quality.valid )
      {
        CHECK( cycle.metrics.pos >= 1 );
        CHECK( cycle.metrics.pos <= cycle.window.endDoy + 30 );
      }
    }
  }
  CHECK( sawWrapped );
  // Interior complete seasons (2021, 2022) have valid metrics near doy 15.
  int validWinter = 0;
  for ( const PhenologyCycle &cycle : result.cycles )
  {
    if ( !cycle.quality.valid )
      continue;
    ++validWinter;
    CHECK( cycle.metrics.pos == Approx( 15.0 ).margin( 40.0 ) );
    CHECK( cycle.quality.amplitudeRatio > 0.5 );
  }
  CHECK( validWinter >= 2 );
}

TEST_CASE( "phenology multi: single-season control yields one cycle per year",
           "[temporal][phenology2]" )
{
  // Plain summer sinusoid (peak doy ~183): one cycle per calendar year,
  // plain (non-wrapped) windows.
  const int n = 3 * 23;
  temporal_corpus::Scenario scenario;
  scenario.name = "single_season";
  scenario.grid = temporal_corpus::makeGrid( n );
  temporal_corpus::Gaussian gauss( 20260932u );
  scenario.y.resize( static_cast<size_t>( n ) );
  for ( int i = 0; i < n; ++i )
  {
    const double t = scenario.grid.tDays[static_cast<size_t>( i )];
    scenario.y[static_cast<size_t>( i )] = static_cast<float>(
      0.3 + 0.6 * std::sin( 2.0 * kPi * t / temporal_corpus::kHarmonicPeriod ) +
      kSigma * gauss() );
  }
  const PhenologyMultiResult result = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  REQUIRE( result.valid );
  CHECK( result.cyclesPerYearMax == 1 );
  for ( const PhenologyCycle &cycle : result.cycles )
    if ( cycle.quality.valid )
      CHECK( cycle.metrics.pos == Approx( 183.0 ).margin( 45.0 ) );
}

TEST_CASE( "phenology multi: a long observation gap refuses instead of "
           "inventing metrics",
           "[temporal][phenology2][negative]" )
{
  temporal_corpus::Scenario scenario =
    temporal_corpus::doubleSeason( 3, 0.2, 0.8, kSigma, 20260933u );
  // Mask doy 120..280 every year: the second-season window (≈ [184, 364])
  // loses its whole head to a >90-day hole and must refuse on the gap gate.
  for ( size_t i = 0; i < scenario.y.size(); ++i )
  {
    const int doy = scenario.grid.doyOf[i];
    if ( doy >= 120 && doy <= 280 )
      scenario.y[i] = temporal_corpus::kNanF;
  }
  const PhenologyMultiResult result = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  // Whatever survives must be flagged, never silently scored: every cycle
  // crossing the hole reports a gap/coverage refusal with NO metrics.
  for ( const PhenologyCycle &cycle : result.cycles )
  {
    CAPTURE( cycle.seasonYear, cycle.cycleIndex, cycle.quality.refusalReason );
    if ( !cycle.quality.valid )
    {
      CHECK( cycle.quality.refusalReason != nullptr );
      CHECK( !cycle.metrics.valid );
      CHECK( cycle.metrics.sos < 0.0 );
      CHECK( cycle.metrics.pos < 0.0 );
      CHECK( cycle.metrics.eos < 0.0 );
    }
  }
  bool sawGapRefusal = false;
  for ( const PhenologyCycle &cycle : result.cycles )
    if ( cycle.quality.refusalReason != nullptr &&
         std::string( cycle.quality.refusalReason ) == "coverage_gap" )
      sawGapRefusal = true;
  CHECK( sawGapRefusal );
}

TEST_CASE( "phenology multi: sparse windows below the sample floor are refused",
           "[temporal][phenology2][negative]" )
{
  temporal_corpus::Scenario scenario =
    temporal_corpus::doubleSeason( 3, 0.2, 0.8, kSigma, 20260934u );
  temporal_corpus::applyMissing( scenario, 0.85, 1234u );
  PhenologyMultiOptions options;
  options.minValidPerSeason = 10;
  const PhenologyMultiResult result = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      options );
  // With 85% dropped and a floor of 10, few or no windows qualify — but any
  // refused window carries a reason and no metrics.
  for ( const PhenologyCycle &cycle : result.cycles )
  {
    if ( !cycle.quality.valid )
    {
      REQUIRE( cycle.quality.refusalReason != nullptr );
      CHECK( !cycle.metrics.valid );
    }
    else
    {
      CHECK( cycle.quality.sampleCount >= options.minValidPerSeason );
    }
  }
}

TEST_CASE( "phenology multi: degenerate inputs refuse with stable codes",
           "[temporal][phenology2][negative]" )
{
  SECTION( "constant series" )
  {
    std::vector<float> y( 60, 1.5f );
    const auto grid = temporal_corpus::makeGrid( 60 );
    const PhenologyMultiResult result =
      phenologyMultiCycle( y, grid.tDays, grid.doyOf, grid.yearOf, PhenologyMultiOptions{} );
    CHECK( !result.valid );
    CHECK( std::string( result.refusalReason ) == "degenerate_seasonal_component" );
  }
  SECTION( "all-NaN series" )
  {
    std::vector<float> y( 60, temporal_corpus::kNanF );
    const auto grid = temporal_corpus::makeGrid( 60 );
    const PhenologyMultiResult result =
      phenologyMultiCycle( y, grid.tDays, grid.doyOf, grid.yearOf, PhenologyMultiOptions{} );
    CHECK( !result.valid );
    CHECK( std::string( result.refusalReason ) == "insufficient_valid_samples" );
  }
  SECTION( "size mismatch" )
  {
    std::vector<float> y( 30, 1.0f );
    const auto grid = temporal_corpus::makeGrid( 10 );
    const PhenologyMultiResult result =
      phenologyMultiCycle( y, grid.tDays, grid.doyOf, grid.yearOf, PhenologyMultiOptions{} );
    CHECK( !result.valid );
    CHECK( std::string( result.refusalReason ) == "insufficient_series" );
  }
}

TEST_CASE( "phenology multi: deterministic across runs",
           "[temporal][phenology2][determinism]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::doubleSeason( 3, 0.2, 0.8, kSigma, 20260935u );
  const PhenologyMultiResult a = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  const PhenologyMultiResult b = phenologyMultiCycle(
      scenario.y, scenario.grid.tDays, scenario.grid.doyOf, scenario.grid.yearOf,
      PhenologyMultiOptions{} );
  REQUIRE( a.cycles.size() == b.cycles.size() );
  for ( size_t i = 0; i < a.cycles.size(); ++i )
  {
    CHECK( a.cycles[i].seasonYear == b.cycles[i].seasonYear );
    CHECK( a.cycles[i].cycleIndex == b.cycles[i].cycleIndex );
    CHECK( a.cycles[i].window.startDoy == b.cycles[i].window.startDoy );
    CHECK( a.cycles[i].window.endDoy == b.cycles[i].window.endDoy );
    CHECK( a.cycles[i].metrics.sos == b.cycles[i].metrics.sos );
    CHECK( a.cycles[i].quality.valid == b.cycles[i].quality.valid );
  }
}
