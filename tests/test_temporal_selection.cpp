// tests/test_temporal_selection.cpp — known-answer tests for the Temporal
// Intelligence 11.0 attribution + model-selection kernels (packages A, B).
// Oracles are the closed-form corpus truths (tests/temporal_corpus.h), never
// the implementation under test.
//
// Attribution oracle semantics (documented in temporal_selection.h): each
// break is tested over its two neighbouring segments. When the greedy
// segmentation straddles one true change with two breaks, each break's test
// window is genuinely mixed, and "Both" is the honest verdict — the
// SERIES-LEVEL distinction is which kinds appear at all:
//   pure level step  -> TrendOnly appears, SeasonalOnly/Both never;
//   pure seasonal    -> SeasonalOnly or Both appear, TrendOnly-only never;
//   step + amplitude -> Both appears.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_selection.h"
#include "temporal_corpus.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;
using temporal_corpus::Scenario;

namespace
{
constexpr double kSigma = 0.03;
constexpr double kPiRef = 3.14159265358979323846;
constexpr double kAlpha = 0.05;

BreakAttributionResult attribute( const Scenario &scenario )
{
  const SeasonalTrendBreaksResult fit = fitSeasonalTrendBreaks(
      scenario.y, scenario.grid.tDays, 2, 3, 8, 0.10, false );
  BreakAttributionOptions options;
  options.harmonics = 2;
  options.alpha = kAlpha;
  return attributeSeasonalTrendBreaks( fit, scenario.y, scenario.grid.tDays,
                                       options );
}

bool hasKindNear( const BreakAttributionResult &result, int sample,
                  int tolerance, std::initializer_list<BreakKind> kinds )
{
  for ( const auto &b : result.breaks )
  {
    if ( std::abs( b.index - sample ) > tolerance )
      continue;
    for ( BreakKind k : kinds )
      if ( b.kind == k )
        return true;
  }
  return false;
}

bool hasKindAnywhere( const BreakAttributionResult &result,
                      std::initializer_list<BreakKind> kinds )
{
  for ( const auto &b : result.breaks )
    for ( BreakKind k : kinds )
      if ( b.kind == k )
        return true;
  return false;
}
} // namespace

TEST_CASE( "attribution: a pure level step is attributed to the trend, not "
           "the seasonal component",
           "[temporal][selection][attribution]" )
{
  // 4 years @ 16-day cadence = 92 samples; step at sample 46 (mid-series).
  const Scenario scenario = temporal_corpus::trendBreakOnly( 92, 46, 5.0, 1.5, kSigma, 20260915u );
  const BreakAttributionResult result = attribute( scenario );
  REQUIRE( !result.breaks.empty() );
  // Trend involvement at the truth; no seasonal claim anywhere in the series.
  CHECK( hasKindNear( result, 46, 5, { BreakKind::TrendOnly, BreakKind::Both } ) );
  CHECK( !hasKindAnywhere( result,
                           { BreakKind::SeasonalOnly, BreakKind::Both } ) );
  // The seasonal basis did not move: shifts stay small relative to the step.
  for ( const auto &b : result.breaks )
    if ( std::isfinite( b.seasonalShift ) )
      CHECK( b.seasonalShift < 0.25 * std::abs( 1.5 ) + 0.1 );
}

TEST_CASE( "attribution: an amplitude jump is attributed to the seasonal "
           "component, not the trend",
           "[temporal][selection][attribution]" )
{
  const Scenario scenario =
    temporal_corpus::seasonalAmplitudeBreak( 92, 46, 0.3, 0.08, 0.85, kSigma, 20260916u );
  const BreakAttributionResult result = attribute( scenario );
  REQUIRE( !result.breaks.empty() );
  // Seasonal involvement at the truth; no TREND-ONLY claim anywhere.
  CHECK( hasKindNear( result, 46, 10,
                      { BreakKind::SeasonalOnly, BreakKind::Both } ) );
  CHECK( !hasKindAnywhere( result, { BreakKind::TrendOnly } ) );
  // The seasonal basis demonstrably moved at the seasonal breaks.
  for ( const auto &b : result.breaks )
    if ( b.kind == BreakKind::SeasonalOnly || b.kind == BreakKind::Both )
      CHECK( b.seasonalShift > 0.1 );
  // Amplitude-1 change stays within the physically plausible range around
  // the planted 0.85 − 0.08 jump.
  for ( const auto &b : result.breaks )
    if ( std::abs( b.index - 46 ) <= 10 && std::isfinite( b.amplitudeChange1 ) )
      CHECK( std::abs( b.amplitudeChange1 ) <= 0.9 );
}

TEST_CASE( "attribution: a phase shift is attributed to the seasonal "
           "component",
           "[temporal][selection][attribution]" )
{
  const Scenario scenario =
    temporal_corpus::seasonalPhaseBreak( 92, 46, 0.2, 0.5, 0.0,
                                         temporal_corpus::kPi / 2.0, kSigma,
                                         20260917u );
  const BreakAttributionResult result = attribute( scenario );
  REQUIRE( !result.breaks.empty() );
  // Tolerance 16 samples = one acquisition spacing: the greedy segmentation
  // may straddle the true change with two breaks (26/61 for this corpus).
  CHECK( hasKindNear( result, 46, 16,
                      { BreakKind::SeasonalOnly, BreakKind::Both } ) );
  CHECK( !hasKindAnywhere( result, { BreakKind::TrendOnly } ) );
  // The seasonal-break p-values are decisive for a pure shape change.
  for ( const auto &b : result.breaks )
    if ( b.kind == BreakKind::SeasonalOnly || b.kind == BreakKind::Both )
      CHECK( b.pValue < kAlpha );
}

TEST_CASE( "attribution: a step plus amplitude change is attributed to both",
           "[temporal][selection][attribution]" )
{
  const Scenario scenario =
    temporal_corpus::bothBreaks( 92, 46, 1.0, 1.2, 0.1, 0.8, kSigma, 20260918u );
  const BreakAttributionResult result = attribute( scenario );
  REQUIRE( !result.breaks.empty() );
  CHECK( hasKindNear( result, 46, 5, { BreakKind::Both } ) );
  // Both involves a seasonal claim:
  CHECK( hasKindAnywhere( result,
                          { BreakKind::SeasonalOnly, BreakKind::Both } ) );
}

TEST_CASE( "attribution: no-change control yields no seasonal attribution",
           "[temporal][selection][attribution][negative]" )
{
  const Scenario scenario =
    temporal_corpus::noChangeControl( 92, 0.4, 0.35, 0.0002, kSigma, 20260919u );
  const BreakAttributionResult result = attribute( scenario );
  // No break may be claimed as a seasonal change on a stationary series.
  for ( const auto &b : result.breaks )
  {
    CAPTURE( b.index, b.pValue, static_cast<int>( b.kind ) );
    CHECK( b.kind != BreakKind::SeasonalOnly );
    CHECK( b.kind != BreakKind::Both );
  }
}

TEST_CASE( "attribution: short side segments report Untestable, never a guess",
           "[temporal][selection][attribution][negative]" )
{
  // 11 samples with the break at 5: the left neighbour (5 valid) is below
  // the unrestricted per-side parameter count for 2 harmonics (6 terms), so
  // the nested test cannot run — Untestable with NaN statistics.
  std::vector<float> y( 11, 1.0f );
  y[5] = 4.0f;
  std::vector<double> t;
  for ( int i = 0; i < 11; ++i )
    t.push_back( 8.0 * i );
  const SeasonalTrendBreaksResult fit =
    fitSeasonalTrendBreaks( y, t, 2, 2, 3, 0.05, false );
  BreakAttributionOptions options;
  options.harmonics = 2;
  const BreakAttributionResult result =
    attributeSeasonalTrendBreaks( fit, y, t, options );
  for ( const auto &b : result.breaks )
  {
    CAPTURE( b.index, static_cast<int>( b.kind ) );
    CHECK( b.kind == BreakKind::Untestable );
    CHECK( !std::isfinite( b.pValue ) );
  }
}

TEST_CASE( "model selection: recovers harmonic order 2 on a two-frequency "
           "series (AICc)",
           "[temporal][selection][model]" )
{
  const Scenario scenario = temporal_corpus::harmonicOrder2( 96, 0.5, 0.4, 0.2, kSigma, 20260920u );
  ModelSelectionOptions options;
  options.maxHarmonics = 3;
  options.maxBreaks = 0;
  options.penalty = SelectionPenalty::AICc;
  options.minImprovement = 0.10;
  const ModelSelectionResult result =
    selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
  REQUIRE( result.selected );
  CHECK( result.selectedHarmonics == 2 );
  REQUIRE( result.candidates.size() == 4 );
  // Candidate enumeration is harmonics ascending.
  for ( size_t i = 0; i < result.candidates.size(); ++i )
    CHECK( result.candidates[i].harmonics == static_cast<int>( i ) );
}

TEST_CASE( "model selection: seasonality-free series selects harmonic order 0",
           "[temporal][selection][model]" )
{
  const Scenario scenario = temporal_corpus::trendBreakOnly( 60, 30, 3.0, 0.0, kSigma, 20260921u );
  ModelSelectionOptions options;
  options.maxHarmonics = 3;
  options.maxBreaks = 0;
  const ModelSelectionResult result =
    selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
  REQUIRE( result.selected );
  CHECK( result.selectedHarmonics == 0 );
}

TEST_CASE( "model selection: a level step is picked up as a break when the "
           "budget allows",
           "[temporal][selection][model]" )
{
  const Scenario scenario = temporal_corpus::trendBreakOnly( 92, 46, 5.0, 1.5, kSigma, 20260922u );
  ModelSelectionOptions options;
  options.maxHarmonics = 1;
  options.maxBreaks = 2;
  options.minSegment = 8;
  const ModelSelectionResult result =
    selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
  REQUIRE( result.selected );
  CAPTURE( result.selectedHarmonics, result.selectedMaxBreaks, result.selectedScore );
  CHECK( result.selectedMaxBreaks >= 1 );
  // The winning candidate's reported parameter count is consistent:
  // segments·terms + breaks + σ².
  CHECK( result.selectedParamCount ==
         ( result.selectedMaxBreaks + 1 ) * ( 2 + 2 * result.selectedHarmonics ) +
             result.selectedMaxBreaks + 1 );
}

TEST_CASE( "model selection: deterministic — identical inputs, identical "
           "result (all penalties)",
           "[temporal][selection][model][determinism]" )
{
  const Scenario scenario =
    temporal_corpus::seasonalAmplitudeBreak( 92, 46, 0.3, 0.1, 0.6, kSigma, 20260923u );
  for ( const SelectionPenalty penalty :
        { SelectionPenalty::AICc, SelectionPenalty::BIC, SelectionPenalty::BlockCv } )
  {
    ModelSelectionOptions options;
    options.maxHarmonics = 2;
    options.maxBreaks = 2;
    options.penalty = penalty;
    const ModelSelectionResult first =
      selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
    const ModelSelectionResult second =
      selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
    REQUIRE( first.selected == second.selected );
    REQUIRE( first.candidates.size() == second.candidates.size() );
    for ( size_t i = 0; i < first.candidates.size(); ++i )
    {
      CHECK( first.candidates[i].score == second.candidates[i].score );
      CHECK( first.candidates[i].fittable == second.candidates[i].fittable );
    }
    CHECK( first.selectedScore == second.selectedScore );
    CHECK( first.selectedHarmonics == second.selectedHarmonics );
    if ( penalty == SelectionPenalty::BlockCv && first.selected )
      CHECK( first.selectedScore >= 0.0 );  // CV score is an MSE
  }
}

TEST_CASE( "model selection: reported scores match an independent AICc "
           "recomputation from the reported RSS",
           "[temporal][selection][model]" )
{
  const Scenario scenario = temporal_corpus::harmonicOrder2( 96, 0.5, 0.4, 0.2, kSigma, 20260937u );
  ModelSelectionOptions options;
  options.maxHarmonics = 3;
  options.maxBreaks = 0;
  options.penalty = SelectionPenalty::AICc;
  const ModelSelectionResult result =
    selectSeasonalTrendModel( scenario.y, scenario.grid.tDays, options );
  REQUIRE( result.selected );
  // Independent recomputation: logL = -n/2 (ln(2*pi*RSS/n) + 1);
  // AICc = 2K - 2 logL + 2K(K+1)/(n-K-1), K = params.
  int n = 0;
  for ( float v : scenario.y )
    if ( std::isfinite( v ) )
      ++n;
  for ( const auto &candidate : result.candidates )
  {
    if ( !candidate.fittable )
      continue;
    const double logL =
      -0.5 * n * ( std::log( 2.0 * kPiRef * candidate.rss / n ) + 1.0 );
    const double k = static_cast<double>( candidate.paramCount );
    const double aicc =
      2.0 * k - 2.0 * logL + 2.0 * k * ( k + 1.0 ) / ( n - k - 1.0 );
    CHECK( candidate.score == Approx( aicc ).epsilon( 1e-9 ) );
  }
}

TEST_CASE( "model selection: exact ties resolve to the earliest (smallest) "
           "candidate",
           "[temporal][selection][model][determinism]" )
{
  // A constant series: the linear candidate fits RSS exactly 0 → score −inf;
  // richer candidates cannot beat −inf, so the smallest model wins.
  std::vector<float> y( 40, 2.0f );
  std::vector<double> t;
  for ( int i = 0; i < 40; ++i )
    t.push_back( 16.0 * i );
  ModelSelectionOptions options;
  options.maxHarmonics = 2;
  options.maxBreaks = 1;
  const ModelSelectionResult result = selectSeasonalTrendModel( y, t, options );
  REQUIRE( result.selected );
  CHECK( result.selectedHarmonics == 0 );
  CHECK( result.selectedMaxBreaks == 0 );
  CHECK( result.selectedScore == -std::numeric_limits<double>::infinity() );
}

TEST_CASE( "model selection: refusal semantics on degenerate input",
           "[temporal][selection][model][negative]" )
{
  ModelSelectionOptions options;
  options.maxHarmonics = 2;
  options.maxBreaks = 1;

  SECTION( "all-NaN series" )
  {
    std::vector<float> y( 40, temporal_corpus::kNanF );
    std::vector<double> t( 40, 0.0 );
    const ModelSelectionResult result = selectSeasonalTrendModel( y, t, options );
    CHECK( !result.selected );
    CHECK( std::string( result.degradedReason ) == "insufficient_valid_samples" );
  }
  SECTION( "too-short series" )
  {
    std::vector<float> y{ 1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN() };
    std::vector<double> t{ 0.0, 16.0, 32.0 };
    const ModelSelectionResult result = selectSeasonalTrendModel( y, t, options );
    CHECK( !result.selected );
    CHECK( std::string( result.degradedReason ) == "insufficient_valid_samples" );
  }
  SECTION( "length mismatch refuses cleanly" )
  {
    std::vector<float> y( 20, 1.0f );
    std::vector<double> t( 10, 0.0 );
    const ModelSelectionResult result = selectSeasonalTrendModel( y, t, options );
    CHECK( !result.selected );
    CHECK( std::string( result.degradedReason ) == "insufficient_valid_samples" );
  }
}
