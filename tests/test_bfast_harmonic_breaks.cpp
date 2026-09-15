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

// ---------------------------------------------------------------------------
// Slice 2: deforestation step detection (D16 §D spec scenario).
// ---------------------------------------------------------------------------

TEST_CASE( "Deforestation intercept step is pinpointed exactly with significance",
           "[d16][bfast]" )
{
    // 4 years, 92 samples; harmonics + gentle trend until the step at
    // index 46, then the intercept drops by 0.35 (fitted-level jump).
    HarmonicSeries before;
    before.slope = 0.0001;
    HarmonicSeries after = before;
    after.intercept = before.intercept - 0.35;

    const auto t = fourYearAxis();
    std::vector<float> y( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( ( i < 46 ? before : after ).value( t[i] ) );

    const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 3, 2, 20 );
    REQUIRE( result.valid );
    REQUIRE( result.breakCount == 1 );
    REQUIRE( result.breakpoints[0].index == 46 ); // exact hit (D16 §D)
    REQUIRE( result.breakpoints[0].magnitude == Approx( -0.35 ).margin( 0.03 ) );
    REQUIRE( result.breakpoints[0].pValue < 0.01 );
    REQUIRE( result.overallRmse < 1e-4 );

    // Decomposition consistency: trend + harmonics reproduces the input.
    double worst = 0.0;
    for ( std::size_t i = 0; i < t.size(); ++i )
        worst = std::max( worst, static_cast<double>( std::abs(
            result.fittedTrend[i] + result.fittedHarmonics[i] - y[i] ) ) );
    REQUIRE( worst < 1e-4 );

    // A clean parallel series in the same call pattern must stay break-free
    // (the detector is selective, not trigger-happy).
    std::vector<float> yClean( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        yClean[i] = static_cast<float>( before.value( t[i] ) );
    const auto clean = BreakpointDetector::detectHarmonicBreaks( yClean, t, 3, 2, 20 );
    REQUIRE( clean.valid );
    REQUIRE( clean.breakCount == 0 );
}

// ---------------------------------------------------------------------------
// Slice 3: model selection bounds, NaN handling, degenerate refusals.
// ---------------------------------------------------------------------------

TEST_CASE( "maxBreaks and minSegmentSamples bound the model", "[d16][bfast]" )
{
    // Two steps: index 30 (−0.25) and index 62 (+0.20).
    HarmonicSeries s0;
    s0.slope = 0.0;
    HarmonicSeries s1 = s0;
    s1.intercept = s0.intercept - 0.25;
    HarmonicSeries s2 = s1;
    s2.intercept = s1.intercept + 0.20;

    const auto t = fourYearAxis();
    std::vector<float> y( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( ( i < 30 ? s0 : ( i < 62 ? s1 : s2 ) ).value( t[i] ) );

    SECTION( "both breaks found when maxBreaks allows" )
    {
        const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 3, 2, 20 );
        REQUIRE( result.valid );
        REQUIRE( result.breakCount == 2 );
        REQUIRE( result.breakpoints[0].index == 30 );
        REQUIRE( result.breakpoints[1].index == 62 );
    }

    SECTION( "maxBreaks=1 keeps only the strongest change" )
    {
        const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 3, 1, 20 );
        REQUIRE( result.valid );
        REQUIRE( result.breakCount == 1 );
        // The deeper step (|Δ| = 0.25 at index 30) is the RSS-dominant one.
        REQUIRE( result.breakpoints[0].index == 30 );
    }
}

TEST_CASE( "Constant series and NaN gaps are handled honestly", "[d16][bfast]" )
{
    const auto t = fourYearAxis();

    SECTION( "constant series: perfect fit, no break invented" )
    {
        std::vector<float> y( t.size(), 0.42f );
        const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 3, 3, 23 );
        REQUIRE( result.valid );
        REQUIRE( result.breakCount == 0 );
        REQUIRE( result.overallRmse < 1e-6 );
    }

    SECTION( "NaN samples are absent, never zero" )
    {
        HarmonicSeries series;
        series.slope = 0.0001;
        std::vector<float> y( t.size() );
        for ( std::size_t i = 0; i < t.size(); ++i )
            y[i] = static_cast<float>( series.value( t[i] ) );
        y[10] = kNan;
        y[11] = kNan;
        y[70] = kNan;
        const auto result = BreakpointDetector::detectHarmonicBreaks( y, t, 3, 2, 23 );
        REQUIRE( result.valid );
        REQUIRE( result.breakCount == 0 );
        // The NaN positions carry NaN residuals; finite positions fit.
        REQUIRE( std::isnan( result.residuals[10] ) );
        REQUIRE( std::isfinite( result.residuals[12] ) );
    }

    SECTION( "degenerate arguments are refused" )
    {
        std::vector<float> y( t.size(), 0.5f );
        REQUIRE( !BreakpointDetector::detectHarmonicBreaks( y, t, 0, 2, 23 ).valid );
        REQUIRE( !BreakpointDetector::detectHarmonicBreaks( y, t, 7, 2, 23 ).valid );
        REQUIRE( !BreakpointDetector::detectHarmonicBreaks( y, t, 3, -1, 23 ).valid );
        REQUIRE( !BreakpointDetector::detectHarmonicBreaks( y, {}, 3, 2, 23 ).valid );
    }
}
