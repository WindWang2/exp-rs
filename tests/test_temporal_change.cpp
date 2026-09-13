// tests/test_temporal_change.cpp — known-answer tests for the joint
// seasonal-trend change kernel (Temporal Platform 10.0, T-3).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_change.h"
#include "processing/algorithms/temporal/temporal_fit.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "fitSeasonalTrendBreaks: a pure seasonal cycle has no breaks", "[temporal][change]" )
{
  // Two full years of a sinusoid + slow warming trend, no regime change.
  std::vector<float> y;
  std::vector<double> t;
  for ( int i = 0; i < 104; ++i ) // weekly, 2 years
  {
    t.push_back( 7.0 * i );
    y.push_back( static_cast<float>( 10.0 + 0.002 * ( 7.0 * i ) +
                                     4.0 * std::sin( 2.0 * M_PI * ( 7.0 * i ) / 365.25 ) ) );
  }
  const SeasonalTrendBreaksResult fit =
    fitSeasonalTrendBreaks( y, t, 2, 3, 5, 0.10, false );
  CAPTURE( fit.breaks.size(), fit.rmse, fit.iterations );
  REQUIRE( fit.breaks.empty() );
  REQUIRE( fit.segments.size() == 1 );
  REQUIRE( fit.rmse < 0.5 );
  REQUIRE( fit.r2 > 0.9 );
  // The trend is recovered through the seasonality.
  REQUIRE( fit.segments[0].slopePerDay == Approx( 0.002 ).margin( 0.001 ) );
}

TEST_CASE( "fitSeasonalTrendBreaks: a level shift in the trend is localized", "[temporal][change]" )
{
  // Seasonal series whose trend flips from +0.02/day to −0.02/day at t=200
  // (plus a level drop of 3 at the break).
  std::vector<float> y;
  std::vector<double> t;
  for ( int i = 0; i < 140; ++i )
  {
    const double ti = 5.0 * i; // 5-day steps, 700-day series
    t.push_back( ti );
    const double seasonal = 2.0 * std::sin( 2.0 * M_PI * ti / 365.25 );
    const double trend = ti < 200.0 ? 0.02 * ti : 0.02 * 200.0 - 0.02 * ( ti - 200.0 );
    const double level = ti < 200.0 ? 0.0 : -3.0;
    y.push_back( static_cast<float>( 10.0 + trend + seasonal + level ) );
  }
  const SeasonalTrendBreaksResult fit =
    fitSeasonalTrendBreaks( y, t, 2, 3, 5, 0.10, false );
  REQUIRE_FALSE( fit.breaks.empty() );
  const double breakDay = fit.breaks.front().breakDays;
  CAPTURE( breakDay, fit.breaks.front().magnitude, fit.rmse );
  REQUIRE( std::abs( breakDay - 200.0 ) <= 25.0 ); // within 5 samples
  REQUIRE( fit.breaks.front().magnitude > 2.0 );   // the level drop dominates
  REQUIRE( fit.segments.size() >= 2 );
  // Slope sign flips across the break.
  REQUIRE( fit.segments[0].slopePerDay > 0.0 );
  REQUIRE( fit.segments[1].slopePerDay < 0.0 );
}

TEST_CASE( "fitSeasonalTrendBreaks: NaN gaps and short series are honest", "[temporal][change]" )
{
  std::vector<float> y{ 1.0f, kNan, 3.0f, kNan };
  std::vector<double> t{ 0.0, 1.0, 2.0, 3.0 };
  const SeasonalTrendBreaksResult fit =
    fitSeasonalTrendBreaks( y, t, 1, 3, 3, 0.1, false );
  REQUIRE( fit.breaks.empty() );
  REQUIRE( std::isnan( fit.rmse ) ); // fewer valid samples than model terms
  REQUIRE( std::isnan( fit.r2 ) );
  REQUIRE( fit.validCount == 2 );

  std::vector<float> shortY{ 1.0f };
  const SeasonalTrendBreaksResult tooShort =
    fitSeasonalTrendBreaks( shortY, { 0.0 }, 1, 3, 3, 0.1, false );
  REQUIRE( std::isnan( tooShort.rmse ) );
}

TEST_CASE( "disturbanceOnset: decrease onset with recovery length", "[temporal][change]" )
{
  // Fitted series: 10 for 6 samples, drops to 4 at the break, recovers to
  // 9.5 two samples later, holds.
  std::vector<float> fitted{ 10, 10, 10, 10, 10, 10, 4, 4, 9.5f, 9.5f };
  std::vector<double> t{ 0, 10, 20, 30, 40, 50, 60, 70, 80, 90 };
  std::vector<BreakEvent> breaks{ { 6, 60.0, 6.0 } };

  double recovery = std::numeric_limits<double>::quiet_NaN();
  const double onset = disturbanceOnset( fitted, t, breaks, /*decrease=*/true,
                                         /*minMagnitude=*/3.0, &recovery, /*tolerance=*/1.0 );
  REQUIRE( onset == Approx( 60.0 ) );
  // Recovery target = 10 − 1 = 9; fitted reaches 9.5 at t = 80.
  REQUIRE( recovery == Approx( 20.0 ) );

  // No qualifying onset when the magnitude gate is above the jump.
  recovery = std::numeric_limits<double>::quiet_NaN();
  const double none = disturbanceOnset( fitted, t, breaks, true, /*minMagnitude=*/7.0,
                                        &recovery, 1.0 );
  REQUIRE( none == -1.0 );

  // Increase direction ignores a decreasing jump.
  const double increase = disturbanceOnset( fitted, t, breaks, false, 3.0, nullptr, 1.0 );
  REQUIRE( increase == -1.0 );

  // Never-recovered series report -1 recovery (a result, not an error).
  std::vector<float> neverRecovers{ 10, 10, 10, 4, 4, 4 };
  std::vector<double> nt{ 0, 10, 20, 30, 40, 50 };
  std::vector<BreakEvent> nb{ { 3, 30.0, 6.0 } };
  const double stuck = disturbanceOnset( neverRecovers, nt, nb, true, 3.0, &recovery, 1.0 );
  REQUIRE( stuck == Approx( 30.0 ) );
  REQUIRE( recovery == Approx( -1.0 ) );
}

TEST_CASE( "whittakerSmoothRobust: a spike is damped more than plain smoothing", "[temporal][change]" )
{
  // Flat series with one spike; the robust variant must hug the flat line
  // tighter at the spike position.
  std::vector<float> y( 30, 5.0f );
  y[15] = 25.0f;
  const std::vector<float> plain = whittakerSmooth( y, {}, 10.0 );
  const std::vector<float> robust = whittakerSmoothRobust( y, {}, 10.0, 3 );
  REQUIRE( plain.size() == 30 );
  REQUIRE( robust.size() == 30 );
  REQUIRE( robust[15] < plain[15] );           // spike pulled down harder
  REQUIRE( robust[0] == Approx( 5.0f ).margin( 0.5f ) );
  REQUIRE( robust[29] == Approx( 5.0f ).margin( 0.5f ) );
  // A gap is bridged by the penalty (the documented whittakerSmooth w=0
  // semantics the robust variant reuses); the bridged value stays near the
  // data level, and an all-NaN series produces an all-NaN result.
  std::vector<float> gapped{ kNan, 1.0f, 1.0f, 1.0f };
  const std::vector<float> out = whittakerSmoothRobust( gapped, {}, 5.0, 2 );
  REQUIRE( std::isfinite( out[0] ) );
  REQUIRE( out[0] < 2.0f );
  const std::vector<float> allNan( 5, kNan );
  const std::vector<float> refused = whittakerSmoothRobust( allNan, {}, 5.0, 2 );
  for ( float v : refused )
    REQUIRE( std::isnan( v ) );
}
