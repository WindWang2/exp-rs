// tests/test_bfast_harmonic_breaks.cpp — D16 Package D: joint harmonic +
// piecewise-linear breakpoint detection. Truth: analytically constructed
// series with known harmonic amplitudes and hand-placed intercept steps.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/breakpoint_detection.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using sicnu::temporal::BreakpointDetector;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr double kPeriod = 365.25;

/// 4 years at 16-day steps -> 92 samples on the day axis (D16 §D).
std::vector<double> fourYearAxis()
{
    std::vector<double> t;
    for ( int i = 0; i < 92; ++i )
        t.push_back( static_cast<double>( i ) * 16.0 );
    return t;
}

struct HarmonicSeries
{
    double intercept = 0.5;
    double slope = 0.0;      // per day
    double a1 = 0.15;        // sin(2π t/T)
    double b1 = 0.08;        // cos(2π t/T)
    double a2 = 0.04;        // sin(4π t/T)
    double b2 = 0.02;        // cos(4π t/T)

    double value( double t ) const
    {
        return intercept + slope * t + a1 * std::sin( 2.0 * M_PI * t / kPeriod ) +
               b1 * std::cos( 2.0 * M_PI * t / kPeriod ) +
               a2 * std::sin( 4.0 * M_PI * t / kPeriod ) +
               b2 * std::cos( 4.0 * M_PI * t / kPeriod );
    }
};

} // namespace

TEST_CASE( "Harmonic-only series reconstructs exactly and yields no breakpoints",
           "[d16][bfast]" )
{
    HarmonicSeries series;
    series.slope = 0.0002;
    const auto t = fourYearAxis();
    std::vector<float> y( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( series.value( t[i] ) );

    const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 2, 2, 23 );
    REQUIRE( result.valid );
    INFO( "breakCount=" << result.breakCount << " rmse=" << result.overallRmse
          << " mosumMax=" << result.mosumMax );
    REQUIRE( result.breakCount == 0 );

    // Noise-free OLS: the fit reproduces the analytic series to fp accuracy.
    const std::size_t n = t.size();
    REQUIRE( result.fittedTrend.size() == n );
    REQUIRE( result.fittedHarmonics.size() == n );
    REQUIRE( result.residuals.size() == n );
    double worst = 0.0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        worst = std::max( worst, static_cast<double>( std::abs(
            result.fittedTrend[i] + result.fittedHarmonics[i] - static_cast<float>( series.value( t[i] ) ) ) ) );
        REQUIRE( std::isfinite( result.residuals[i] ) );
    }
    REQUIRE( worst < 1e-4 );
    REQUIRE( result.overallRmse < 1e-4 );

    // MOSUM screen bookkeeping: h = floor(0.15 * 92) = 13.
    REQUIRE( result.mosumH == 13 );
}
