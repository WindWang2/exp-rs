// tests/test_temporal_fit.cpp — Temporal Analysis 2.0 numeric references
// (goal §7/§12): Savitzky–Golay, Whittaker, harmonic regression, phenology,
// breakpoints, and seasonal decomposition against hand-derived or analytic
// expectations with locked tolerances.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>

#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_gapfill.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace sicnu::temporal;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_fit";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "Savitzky-Golay preserves polynomial signals up to the fit degree",
           "[temporal][fit]" )
{
    ensureApp();
    const std::vector<float> linear = { 1, 2, 3, 4, 5, 6, 7 };
    const auto fitLinear = savitzkyGolay( linear, 5, 2 );
    REQUIRE( fitLinear.size() == linear.size() );
    for ( size_t i = 0; i < linear.size(); ++i )
        REQUIRE( fitLinear[i] == Approx( linear[i] ).margin( 1e-5 ) );

    // Degree-2 series with a degree-2 fit is reproduced exactly (SG is exact
    // for polynomials up to the fit degree).
    std::vector<float> quad( 9 );
    for ( int i = 0; i < 9; ++i )
        quad[i] = static_cast<float>( ( i - 4 ) * ( i - 4 ) );
    const auto fitQuad = savitzkyGolay( quad, 5, 2 );
    for ( size_t i = 0; i < quad.size(); ++i )
        REQUIRE( fitQuad[i] == Approx( quad[i] ).margin( 1e-3 ) );

    // Constant series is invariant.
    const std::vector<float> flat( 11, 3.5f );
    const auto fitFlat = savitzkyGolay( flat, 7, 3 );
    for ( float v : fitFlat )
        REQUIRE( v == Approx( 3.5f ).margin( 1e-9 ) );

    // Window 3, degree 1 on a linear ramp: local line fit = ramp.
    const auto fit3 = savitzkyGolay( linear, 3, 1 );
    for ( size_t i = 0; i < linear.size(); ++i )
        REQUIRE( fit3[i] == Approx( linear[i] ).margin( 1e-5 ) );
}

TEST_CASE( "Savitzky-Golay shrinks the window at boundaries instead of failing",
           "[temporal][fit]" )
{
    ensureApp();
    // A short series still fits (window shrink): no NaNs on a clean series.
    const std::vector<float> y = { 2, 4, 9, 16, 25 };
    const auto fit = savitzkyGolay( y, 5, 2 );
    REQUIRE( fit.size() == 5 );
    for ( float v : fit )
        REQUIRE( std::isfinite( v ) );
}

TEST_CASE( "Whittaker identity and heavy-smoothing limits", "[temporal][fit]" )
{
    ensureApp();
    const std::vector<float> y = { 1, 3, 2, 5, 4, 6, 5, 7 };

    // λ → 0 with all weights = interpolation of the input.
    const auto tiny = whittakerSmooth( y, {}, 1e-9 );
    for ( size_t i = 0; i < y.size(); ++i )
        REQUIRE( tiny[i] == Approx( y[i] ).margin( 1e-4 ) );

    // λ huge: z is forced to the second-difference null space — a straight
    // line. Check linearity of the result (second differences ≈ 0).
    const auto heavy = whittakerSmooth( y, {}, 1e12 );
    for ( size_t i = 2; i < y.size(); ++i )
    {
        const double d2 = heavy[i] - 2.0 * heavy[i - 1] + heavy[i - 2];
        REQUIRE( std::fabs( d2 ) < 1e-4 );
    }
}

TEST_CASE( "Whittaker bridges interior gaps with finite values", "[temporal][fit]" )
{
    ensureApp();
    const std::vector<float> gappy = { 2, kNan, kNan, 5, 6, kNan, 8 };
    const auto fit = whittakerSmooth( gappy, {}, 2.0 );
    for ( float v : fit )
        REQUIRE( std::isfinite( v ) );
    // Known samples stay close at small λ (weighted data term dominates).
    REQUIRE( std::fabs( fit[3] - 5.0f ) < 0.6 );
    REQUIRE( std::fabs( fit[6] - 8.0f ) < 0.6 );
}

TEST_CASE( "Harmonic regression recovers synthetic annual components",
           "[temporal][fit]" )
{
    ensureApp();
    // y(t) = 2 + 3·sin(2πt/365.25) + 0.5·cos(4πt/365.25), sampled daily for
    // two years. The 2-harmonic OLS recovers every coefficient.
    std::vector<float> y;
    std::vector<double> t;
    for ( int d = 0; d < 730; ++d )
    {
        const double td = static_cast<double>( d );
        const double value = 2.0 + 3.0 * std::sin( 2.0 * M_PI * td / 365.25 ) +
                             0.5 * std::cos( 4.0 * M_PI * td / 365.25 );
        y.push_back( static_cast<float>( value ) );
        t.push_back( td );
    }
    const HarmonicFitResult fit = harmonicFit( y, t, 2, false );
    REQUIRE( fit.validCount == 730 );
    REQUIRE( fit.r2 > 0.9999 );
    REQUIRE( fit.rmse < 1e-4 );
    REQUIRE( fit.coefficients.size() == 5 );
    REQUIRE( fit.coefficients[0] == Approx( 2.0 ).margin( 1e-5 ) );  // intercept
    REQUIRE( fit.coefficients[1] == Approx( 3.0 ).margin( 1e-4 ) );  // sin 1
    REQUIRE( fit.coefficients[2] == Approx( 0.0 ).margin( 1e-4 ) );  // cos 1
    REQUIRE( fit.coefficients[3] == Approx( 0.0 ).margin( 1e-4 ) );  // sin 2
    REQUIRE( fit.coefficients[4] == Approx( 0.5 ).margin( 1e-4 ) );  // cos 2
}

TEST_CASE( "Harmonic fit tolerates gaps and refuses underdetermined series",
           "[temporal][fit]" )
{
    ensureApp();
    std::vector<float> y;
    std::vector<double> t;
    for ( int d = 0; d < 365; ++d )
    {
        const double td = static_cast<double>( d );
        t.push_back( td );
        // Half the year is masked (clouds) — the fit still solves.
        y.push_back( d < 180 ? kNan
                             : static_cast<float>( 1.0 + std::sin( 2.0 * M_PI * td / 365.25 ) ) );
    }
    const HarmonicFitResult fit = harmonicFit( y, t, 1, false );
    REQUIRE( fit.validCount == 185 );
    REQUIRE( fit.r2 > 0.99 );

    // Underdetermined: 2 valid samples cannot solve 3 terms.
    const std::vector<float> tiny = { 1.f, kNan, 2.f, kNan, kNan };
    const std::vector<double> t5 = { 0, 1, 2, 3, 4 };
    const HarmonicFitResult bad = harmonicFit( tiny, t5, 1, false );
    REQUIRE( bad.validCount == 0 );
    for ( float v : bad.fitted )
        REQUIRE( std::isnan( v ) );
}

TEST_CASE( "Phenology threshold metrics on a synthetic season", "[temporal][phenology]" )
{
    ensureApp();
    // Gaussian-shaped season peaking at doy 200: v = 0.2 + 0.8·exp(-((doy-200)/60)²).
    // frac 0.5 crossing: exp(…) = 0.5 at |doy-200| ≈ 49.97 → sos ≈ 150, eos ≈ 250.
    std::vector<float> y;
    std::vector<double> t;
    std::vector<int> doy;
    for ( int d = 1; d <= 365; ++d )
    {
        const double value = 0.2 + 0.8 * std::exp( -std::pow( ( d - 200 ) / 60.0, 2 ) );
        y.push_back( static_cast<float>( value ) );
        t.push_back( d - 1.0 );
        doy.push_back( d );
    }
    const SeasonalMetrics m = phenologyThreshold( y, t, doy, 1, 366, 0.5 );
    REQUIRE( m.valid );
    REQUIRE( m.base == Approx( 0.2 ).margin( 1e-3 ) );
    REQUIRE( m.amplitude == Approx( 0.8 ).margin( 1e-4 ) );
    REQUIRE( m.pos == 200 );
    REQUIRE( m.sos == Approx( 150.0 ).margin( 3.0 ) );
    REQUIRE( m.eos == Approx( 250.0 ).margin( 3.0 ) );
    REQUIRE( m.los == Approx( 100.0 ).margin( 4.0 ) );
    REQUIRE( m.integral > 0.0 );
}

TEST_CASE( "Phenology reports invalid for degenerate seasons", "[temporal][phenology]" )
{
    ensureApp();
    const std::vector<float> flat = { 1.f, 1.f, 1.f, 1.f, 1.f };
    const std::vector<double> t = { 0, 10, 20, 30, 40 };
    const std::vector<int> doy = { 100, 110, 120, 130, 140 };
    const SeasonalMetrics m = phenologyThreshold( flat, t, doy, 1, 366, 0.5 );
    // Zero amplitude: SOS/EOS still exist (threshold == base), integral 0.
    REQUIRE( m.amplitude == Approx( 0.0 ).margin( 1e-9 ) );

    // Too few valid samples → invalid.
    const std::vector<float> short_ = { 1.f, 2.f };
    const SeasonalMetrics bad = phenologyThreshold( short_, { 0, 10 }, { 100, 110 }, 1, 366, 0.5 );
    REQUIRE_FALSE( bad.valid );
}

TEST_CASE( "Piecewise linear trend finds the planted break exactly",
           "[temporal][breakpoints]" )
{
    ensureApp();
    // Rising 1/day for 100 samples, falling 1/day afterwards: greedy RSS
    // segmentation must split exactly at index 100 on noiseless data.
    std::vector<float> y;
    std::vector<double> t;
    for ( int i = 0; i < 200; ++i )
    {
        const double slope = i < 100 ? 1.0 : -1.0;
        const double base = i < 100 ? 0.0 : 200.0;
        y.push_back( static_cast<float>( base + slope * i ) );
        t.push_back( i );
    }
    const BreakpointResult r = piecewiseLinearTrend( y, t, 2, 10, 0.5 );
    REQUIRE( r.breakIndices.size() == 1 );
    REQUIRE( r.breakIndices.front() == 100 );
    REQUIRE( r.slopes.size() == 2 );
    REQUIRE( r.slopes[0] == Approx( 1.0 ).margin( 1e-9 ) );
    REQUIRE( r.slopes[1] == Approx( -1.0 ).margin( 1e-9 ) );
    REQUIRE( r.rmse == Approx( 0.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Piecewise linear trend rejects flat series improvements below threshold",
           "[temporal][breakpoints]" )
{
    ensureApp();
    const std::vector<float> y = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    std::vector<double> t( 12 );
    for ( int i = 0; i < 12; ++i )
        t[i] = i * 3.0;
    const BreakpointResult r = piecewiseLinearTrend( y, t, 3, 3, 0.5 );
    // A perfectly linear series has no RSS gain from splitting.
    REQUIRE( r.breakIndices.empty() );
    REQUIRE( r.slopes.size() == 1 );
    // Slope per day: series rises 1 per sample; samples are 3 days apart.
    REQUIRE( r.slopes.front() == Approx( 1.0 / 3.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Piecewise linear trend RMSE divides by valid observations only (#759)",
           "[temporal][breakpoints]" )
{
    ensureApp();
    // 10 samples, NaN gaps at indices 4 and 5 → 8 valid observations.
    // Valid values: y[3] = y[6] = 4, all other valid samples 0, t = 0..9.
    // Mean t over the valid set {0,1,2,3,6,7,8,9} is 4.5 and both outliers sit
    // at distance 1.5 with the same deviation, so the OLS slope stays 0 and the
    // fitted value is the mean 1 everywhere. Hand-derived SSE:
    //   6*(0-1)^2 + 2*(4-1)^2 = 24 → RMSE = sqrt(24/8) = sqrt(3) ≈ 1.7320508.
    // The pre-#759 denominator counted all 10 slots → sqrt(24/10) ≈ 1.5491933.
    std::vector<float> y( 10, 0.0f );
    y[3] = 4.f;
    y[6] = 4.f;
    y[4] = kNan;
    y[5] = kNan;
    std::vector<double> t( 10 );
    for ( int i = 0; i < 10; ++i )
        t[i] = i;
    const BreakpointResult r = piecewiseLinearTrend( y, t, 0, 3, 0.5 );
    REQUIRE( r.validCount == 8 );
    REQUIRE( r.rmse == Approx( std::sqrt( 3.0 ) ).epsilon( 1e-12 ) );
}

TEST_CASE( "Piecewise linear trend reports undefined RMSE for an all-NaN series",
           "[temporal][breakpoints]" )
{
    ensureApp();
    std::vector<float> y( 12, kNan );
    std::vector<double> t( 12 );
    for ( int i = 0; i < 12; ++i )
        t[i] = i * 16.0;
    const BreakpointResult r = piecewiseLinearTrend( y, t, 2, 3, 0.5 );
    REQUIRE( r.validCount == 0 );
    REQUIRE( std::isnan( r.rmse ) );
}

TEST_CASE( "Piecewise linear trend keeps exact zero RMSE on gapped perfect lines",
           "[temporal][breakpoints]" )
{
    ensureApp();
    // y = 2t with two NaN gaps: the SSE over the 8 valid samples is exactly 0,
    // so the corrected denominator must not turn a perfect fit into nonzero.
    std::vector<float> y( 10 );
    std::vector<double> t( 10 );
    for ( int i = 0; i < 10; ++i )
    {
        t[i] = i;
        y[i] = static_cast<float>( 2 * i );
    }
    y[4] = kNan;
    y[5] = kNan;
    const BreakpointResult r = piecewiseLinearTrend( y, t, 0, 3, 0.5 );
    REQUIRE( r.validCount == 8 );
    REQUIRE( r.rmse == Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Mann-Kendall/Sen recovers a perfect monotonic trend", "[temporal][sen]" )
{
    ensureApp();
    const std::vector<float> y = { 1, 2, 3, 4, 5 };
    const std::vector<double> t = { 0, 1, 2, 3, 4 };
    const SenTrendResult r = mannKendallSenSlope( y, t );
    REQUIRE( r.validCount == 5 );
    // All 10 pairwise slopes are 1; the median residual set is exactly 1.
    REQUIRE( r.slope == Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( r.intercept == Approx( 1.0 ).margin( 1e-12 ) );
    // S = 10, var(S) = n(n-1)(2n+5)/18 = 5·4·15/18, z = (S-1)/sqrt(var).
    const double expectedVar = 5.0 * 4.0 * 15.0 / 18.0;
    REQUIRE( r.variance == Approx( expectedVar ).epsilon( 1e-12 ) );
    const double expectedZ = ( 10.0 - 1.0 ) / std::sqrt( expectedVar );
    REQUIRE( r.z == Approx( expectedZ ).epsilon( 1e-12 ) );
    REQUIRE( r.pValue == Approx( std::erfc( expectedZ / std::sqrt( 2.0 ) ) )
                 .epsilon( 1e-12 ) );
    REQUIRE( r.pValue < 0.05 );
}

TEST_CASE( "Mann-Kendall handles ties with the corrected variance", "[temporal][sen]" )
{
    ensureApp();
    // y = [1,1,2]: S = 0 + 1 + 1 = 2; tie group {1,1} removes 2·1·9 from the
    // raw 3·2·11, so var = (66 − 18)/18; pairwise slopes {0, 1, 0.5} → 0.5.
    const std::vector<float> y = { 1, 1, 2 };
    const std::vector<double> t = { 0, 1, 2 };
    const SenTrendResult r = mannKendallSenSlope( y, t );
    REQUIRE( r.validCount == 3 );
    REQUIRE( r.slope == Approx( 0.5 ).margin( 1e-12 ) );
    // Residuals y − 0.5·t = {1, 0.5, 1} → median 1.
    REQUIRE( r.intercept == Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( r.variance == Approx( 48.0 / 18.0 ).epsilon( 1e-12 ) );
    REQUIRE( r.z == Approx( 1.0 / std::sqrt( 48.0 / 18.0 ) ).epsilon( 1e-12 ) );
}

TEST_CASE( "Sen slope is robust to an extreme outlier that drags OLS", "[temporal][sen]" )
{
    ensureApp();
    // y = [1,2,3,-100]: pairwise slopes sorted are {-103, -51, -101/3, 1, 1, 1},
    // median (−101/3 + 1)/2 = −49/3. S = 0 → z = 0, p = 1 (no significant
    // monotonic trend), while an OLS slope would be dragged strongly negative.
    const std::vector<float> y = { 1, 2, 3, -100 };
    const std::vector<double> t = { 0, 1, 2, 3 };
    const SenTrendResult r = mannKendallSenSlope( y, t );
    REQUIRE( r.validCount == 4 );
    REQUIRE( r.slope == Approx( -49.0 / 3.0 ).epsilon( 1e-12 ) );
    REQUIRE( r.z == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( r.pValue == 1.0 );
}

TEST_CASE( "Mann-Kendall/Sen NaN and duplicate-time contracts", "[temporal][sen]" )
{
    ensureApp();
    const double qnan = std::numeric_limits<double>::quiet_NaN();

    // Fewer than 3 valid observations → undefined test, NaN everywhere.
    SenTrendResult r = mannKendallSenSlope( { 1, kNan, 3 }, { 0, 1, 2 } );
    REQUIRE( r.validCount == 2 );
    REQUIRE( std::isnan( r.slope ) );
    REQUIRE( std::isnan( r.pValue ) );

    // Duplicate instants: strictly-ordered-time pairs only. t = [0,0,1,2],
    // y = [1,2,3,4] keeps 5 ordered pairs: slopes {1,1,1,1.5,2} → 1;
    // S = 5 over those pairs; var = 4·3·13/18.
    r = mannKendallSenSlope( { 1, 2, 3, 4 }, { 0, 0, 1, 2 } );
    REQUIRE( r.validCount == 4 );
    REQUIRE( r.slope == Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( r.intercept == Approx( 2.0 ).margin( 1e-12 ) );
    const double expectedVar = 4.0 * 3.0 * 13.0 / 18.0;
    REQUIRE( r.variance == Approx( expectedVar ).epsilon( 1e-12 ) );
    const double expectedZ = ( 5.0 - 1.0 ) / std::sqrt( expectedVar );
    REQUIRE( r.z == Approx( expectedZ ).epsilon( 1e-12 ) );
    REQUIRE( r.pValue == Approx( std::erfc( expectedZ / std::sqrt( 2.0 ) ) )
                 .epsilon( 1e-12 ) );
    (void)qnan;
}

TEST_CASE( "Seasonal decomposition recovers trend and climatology", "[temporal][decompose]" )
{
    ensureApp();
    // Three years of daily data: trend 0.01·t + seasonal sin(2π·doy/365.25).
    std::vector<float> y;
    std::vector<double> t;
    std::vector<int> doy;
    for ( int d = 0; d < 1095; ++d )
    {
        const double td = static_cast<double>( d );
        const int dayOfYear = ( d % 365 ) + 1;
        const double value = 0.01 * td + std::sin( 2.0 * M_PI * dayOfYear / 365.25 );
        y.push_back( static_cast<float>( value ) );
        t.push_back( td );
        doy.push_back( dayOfYear );
    }
    // Separation quality is λ-dependent and PARTIAL (verified numerically
    // against a dense solve): λ=1e8 on daily samples keeps the trend within
    // ~0.02 of the ramp while the doy climatology absorbs ~60% of the true
    // seasonal amplitude (the rest rides the trend's small wiggle). Assert
    // the honest contract: trend ≈ ramp, seasonal PROPORTIONAL to the true
    // sine, remainder ≈ 0.
    const DecompositionResult r = seasonalDecompose( y, t, doy, 1e8, 15 );
    REQUIRE( r.trend.size() == 1095 );
    // The series ENDPOINT carries a documented one-sided boundary bias of the
    // second-difference penalty, so endpoints are not asserted.
    REQUIRE( r.trend[200] == Approx( 2.00 ).margin( 0.12 ) );
    REQUIRE( r.trend[547] == Approx( 5.47 ).margin( 0.12 ) );
    REQUIRE( r.trend[900] == Approx( 9.00 ).margin( 0.12 ) );
    // Seasonal is proportional to the true sine at the quarter points
    // (amplitude ratio in [0.5, 1.2], sign/phase preserved).
    for ( int d : { 91, 273 } )
    {
        const double expected = std::sin( 2.0 * M_PI * d / 365.25 );
        const double ratio = r.seasonal[d] / expected;
        REQUIRE( ratio > 0.5 );
        REQUIRE( ratio < 1.2 );
    }
    REQUIRE( std::fabs( r.seasonal[182] ) < 0.15 ); // sine ≈ 0 at the half point
    // Remainder holds whatever the (partially leaking) trend does not absorb:
    // with λ=1e8 the leakage is bounded by ~0.3 here (trend wiggle + climatology
    // discretization), far below the seasonal amplitude itself.
    for ( int d : { 400, 800, 1000 } )
        REQUIRE( std::fabs( r.remainder[d] ) < 0.3 );
}

// ---------------------------------------------------------------------------
// Gap-fill kernel (Temporal 7.0): the interpolation math extracted from
// rs:temporal_gap_fill — previously inlined in the operator with zero
// kernel-level coverage.
// ---------------------------------------------------------------------------

using sicnu::temporal::GapFillMethod;
using sicnu::temporal::gapFillSeries;

TEST_CASE( "Gap-fill linear: time-weighted interpolation over real day offsets", "[temporal][gapfill]" )
{
    // t = {0, 10, 20, 30}; value 10 at t=0, gap, 30 at t=30 → t=10 fills 20
    // by weight 10/30... (linear in time, NOT by index), t=20 fills 30.
    std::vector<float> y = { 10.0f, kNan, kNan, 40.0f };
    const std::vector<double> t = { 0.0, 10.0, 20.0, 30.0 };
    const auto out = gapFillSeries( y, t, GapFillMethod::Linear, 90.0 );
    REQUIRE( out[1] == Approx( 20.0 ).margin( 1e-6 ) );  // 10 + (40-10)*10/30
    REQUIRE( out[2] == Approx( 30.0 ).margin( 1e-6 ) );  // 10 + (40-10)*20/30
    // Index-linear would wrongly give 20/30 with equal index spacing —
    // stretch the middle gap: same values, t = {0, 5, 25, 30} shifts weights.
    const std::vector<double> t2 = { 0.0, 5.0, 25.0, 30.0 };
    const auto out2 = gapFillSeries( y, t2, GapFillMethod::Linear, 90.0 );
    REQUIRE( out2[1] == Approx( 10.0 + 30.0 * 5.0 / 30.0 ).margin( 1e-6 ) );
    REQUIRE( out2[2] == Approx( 10.0 + 30.0 * 25.0 / 30.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Gap-fill linear: maxGapDays and the no-extrapolation contract", "[temporal][gapfill]" )
{
    std::vector<float> y = { kNan, 10.0f, kNan, 40.0f };
    const std::vector<double> t = { 0.0, 10.0, 20.0, 30.0 };
    // Span 20 days exceeds maxGapDays 15 → interior gap stays NaN.
    const auto out = gapFillSeries( y, t, GapFillMethod::Linear, 15.0 );
    REQUIRE( std::isnan( out[2] ) );
    // Leading one-sided gap stays NaN even with a huge budget (never
    // extrapolate).
    const auto out2 = gapFillSeries( y, t, GapFillMethod::Linear, 1e9 );
    REQUIRE( std::isnan( out2[0] ) );
    // Isolated sample with no anchors at all stays NaN.
    std::vector<float> lone = { kNan, kNan };
    const auto out3 = gapFillSeries( lone, { 0.0, 1.0 }, GapFillMethod::Linear, 1e9 );
    REQUIRE( std::isnan( out3[0] ) );
    REQUIRE( std::isnan( out3[1] ) );
}

TEST_CASE( "Gap-fill duplicate instants average instead of 0/0 weight", "[temporal][gapfill]" )
{
    // keep_all collections can carry identical timestamps: span 0 must not
    // produce a 0/0 weight — the deterministic answer is the anchors' mean.
    std::vector<float> y = { 4.0f, kNan, 8.0f };
    const std::vector<double> t = { 10.0, 10.0, 10.0 };
    const auto out = gapFillSeries( y, t, GapFillMethod::Linear, 90.0 );
    REQUIRE( out[1] == Approx( 6.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Gap-fill nearest: closer anchor wins, ties go to the earlier scene", "[temporal][gapfill]" )
{
    std::vector<float> y = { 1.0f, kNan, kNan, kNan, 2.0f };
    const std::vector<double> t = { 0.0, 10.0, 20.0, 30.0, 40.0 };
    const auto out = gapFillSeries( y, t, GapFillMethod::Nearest, 90.0 );
    REQUIRE( out[1] == Approx( 1.0 ).margin( 1e-6 ) );  // closer to t=0
    REQUIRE( out[2] == Approx( 1.0 ).margin( 1e-6 ) );  // tie 10/10 -> earlier
    REQUIRE( out[3] == Approx( 2.0 ).margin( 1e-6 ) );  // closer to t=40
    // The 15-day budget: position t=20 is 20 days from BOTH anchors — over
    // budget → NaN (its 20/20 tie would have chosen the earlier scene, as
    // the unlimited run above shows at index 2). Position t=30 still reaches
    // its right anchor (40−30=10 ≤ 15).
    const auto out2 = gapFillSeries( y, t, GapFillMethod::Nearest, 15.0 );
    REQUIRE( out2[3] == Approx( 2.0 ).margin( 1e-6 ) );
    REQUIRE( std::isnan( out2[2] ) );
}

TEST_CASE( "Gap-fill counts: filled vs fillable accounting matches the operator metric", "[temporal][gapfill]" )
{
    // Series with two interior gaps (anchored both sides) and a trailing
    // one-sided gap (anchored on the left only — fillable but linear must
    // never extrapolate).
    std::vector<float> y = { 10.0f, kNan, kNan, 40.0f, kNan };
    const std::vector<double> t = { 0.0, 10.0, 20.0, 30.0, 40.0 };
    sicnu::temporal::GapFillCounts counts;
    std::vector<float> out( 5 );
    // Budget 0: every gap is anchored (fillable) but nothing may be filled.
    gapFillSeries( y.data(), 5, t.data(), GapFillMethod::Linear, 0.0, out.data(), &counts );
    REQUIRE( counts.fillable == 3 );
    REQUIRE( counts.filled == 0 );
    // Unlimited budget: the two interior gaps fill; the trailing gap stays NaN.
    gapFillSeries( y.data(), 5, t.data(), GapFillMethod::Linear, 1e9, out.data(), &counts );
    REQUIRE( counts.fillable == 3 );
    REQUIRE( counts.filled == 2 );
    REQUIRE( std::isnan( out[4] ) );
}
