// tests/test_whittaker_smooth.cpp — D16 Package B: robust time-series
// smoothing. Expected values come from the documented normal equations
// (independent construction of W + λDᵀD in this file), analytic sine
// truths, and polynomial reproduction identities — never from the code
// under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal_smoothing.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using Catch::Approx;
using sicnu::temporal::savitzkyGolay;
using sicnu::temporal::whittakerSmooth;
using sicnu::temporal::whittakerSmoothRobust;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

double maxAbsDiff( const std::vector<float> &a, const std::vector<float> &b )
{
    double m = 0.0;
    for ( std::size_t i = 0; i < a.size() && i < b.size(); ++i )
        if ( std::isfinite( a[i] ) && std::isfinite( b[i] ) )
            m = std::max( m, static_cast<double>( std::abs( a[i] - b[i] ) ) );
    return m;
}

double mae( const std::vector<float> &fit, const std::vector<float> &truth )
{
    double sum = 0.0;
    std::size_t n = 0;
    for ( std::size_t i = 0; i < fit.size() && i < truth.size(); ++i )
    {
        if ( std::isfinite( fit[i] ) && std::isfinite( truth[i] ) )
        {
            sum += std::abs( static_cast<double>( fit[i] ) - truth[i] );
            ++n;
        }
    }
    return n ? sum / n : std::numeric_limits<double>::infinity();
}

/// Infinity-norm of || (W + λDᵀD) z − W y ||, with DᵀD built here from the
/// second-difference definition (rows (1,−2,1)) — the documented contract.
double normalEquationResidual( const std::vector<float> &y, const std::vector<float> &w,
                               double lambda, const std::vector<float> &z, int d )
{
    const std::size_t n = y.size();
    auto weight = [&]( std::size_t i ) {
        if ( !std::isfinite( y[i] ) )
            return 0.0;
        return w.empty() ? 1.0 : std::max( 0.0, static_cast<double>( w[i] ) );
    };
    auto diff2 = [&]( std::size_t i ) { // (DᵀD z)_i for d = 2
        double s = 0.0;
        for ( std::size_t k = 0; k + 2 < n; ++k )
        {
            double row = 0.0;
            if ( k == i ) row = 1.0;
            if ( k + 1 == i ) row = -2.0;
            if ( k + 2 == i ) row = 1.0;
            if ( row == 0.0 )
                continue;
            s += row * ( static_cast<double>( z[k + 2] ) - 2.0 * z[k + 1] + z[k] );
        }
        return s;
    };
    auto diff1 = [&]( std::size_t i ) { // (DᵀD z)_i for d = 1
        double s = 0.0;
        for ( std::size_t k = 0; k + 1 < n; ++k )
        {
            double row = 0.0;
            if ( k == i ) row = -1.0;
            if ( k + 1 == i ) row = 1.0;
            if ( row == 0.0 )
                continue;
            s += row * ( static_cast<double>( z[k + 1] ) - z[k] );
        }
        return s;
    };

    double worst = 0.0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        const double wi = weight( i );
        const double pen = ( d == 2 ? diff2( i ) : diff1( i ) );
        const double residual = wi * ( z[i] - y[i] ) + lambda * pen;
        worst = std::max( worst, std::abs( residual ) );
    }
    return worst;
}

} // namespace

TEST_CASE( "Pentadiagonal Whittaker solve satisfies the documented normal equations",
           "[d16][whittaker]" )
{
    // Unit pulse: the solve has no closed shortcut, so the residual against
    // the normal equations is the honest check.
    const std::vector<float> y = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
    const std::vector<float> z = whittakerSmooth( y, {}, 10.0, 2 );
    REQUIRE( z.size() == y.size() );
    for ( float v : z )
        REQUIRE( std::isfinite( v ) );
    REQUIRE( normalEquationResidual( y, {}, 10.0, z, 2 ) < 1e-4 );

    // Same with an explicit weight vector (double weight on the pulse).
    std::vector<float> w( y.size(), 1.0f );
    w[5] = 2.0f;
    const std::vector<float> z2 = whittakerSmooth( y, w, 10.0, 2 );
    REQUIRE( z2.size() == y.size() );
    REQUIRE( normalEquationResidual( y, w, 10.0, z2, 2 ) < 1e-4 );
}

TEST_CASE( "Whittaker preserves constants and interpolates as lambda vanishes",
           "[d16][whittaker]" )
{
    // Degree-0 polynomial is in the null space of D² → preserved exactly.
    const std::vector<float> flat( 25, 3.5f );
    const auto zFlat = whittakerSmooth( flat, {}, 1e3, 2 );
    REQUIRE( maxAbsDiff( zFlat, flat ) < 1e-5 );

    // Degree-1 polynomial likewise (D² linear = 0).
    std::vector<float> ramp( 25 );
    for ( std::size_t i = 0; i < ramp.size(); ++i )
        ramp[i] = static_cast<float>( 0.25 * i - 1.0 );
    const auto zRamp = whittakerSmooth( ramp, {}, 1e3, 2 );
    REQUIRE( maxAbsDiff( zRamp, ramp ) < 1e-5 );

    // λ → 0: the finite samples are interpolated (max |Δ| < 1e-5, D16 §B).
    const auto zInterp = whittakerSmooth( flat, {}, 1e-6, 2 );
    REQUIRE( maxAbsDiff( zInterp, flat ) < 1e-5 );
}

TEST_CASE( "Whittaker d=1 tridiagonal solve matches the first-difference system",
           "[d16][whittaker]" )
{
    const std::vector<float> y = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 2 };
    const std::vector<float> z = whittakerSmooth( y, {}, 5.0, 1 );
    REQUIRE( z.size() == y.size() );
    for ( float v : z )
        REQUIRE( std::isfinite( v ) );
    REQUIRE( normalEquationResidual( y, {}, 5.0, z, 1 ) < 1e-4 );
}

TEST_CASE( "Whittaker treats NaN samples as absent and never as zero",
           "[d16][whittaker]" )
{
    std::vector<float> y = { 1, 2, kNan, 4, 5, 6, 7, 8 };
    const auto z = whittakerSmooth( y, {}, 1e-6, 2 );
    // λ ≈ 0 interpolates finite samples; the NaN position gets the bridge.
    REQUIRE( z.size() == y.size() );
    REQUIRE( z[0] == Approx( 1.0 ).margin( 1e-5 ) );
    REQUIRE( z[1] == Approx( 2.0 ).margin( 1e-5 ) );
    REQUIRE( z[3] == Approx( 4.0 ).margin( 1e-5 ) );
    // A NaN output would mean "no fit here" — but the λ-penalized system
    // bridges gaps; either way it must be finite and near 3 (linearity).
    REQUIRE( z[2] == Approx( 3.0 ).margin( 0.1 ) );

    // Zero weight is equivalent to absence.
    const std::vector<float> w = { 1, 1, 0, 1, 1, 1, 1, 1 };
    const auto z2 = whittakerSmooth( y, w, 1e-6, 2 );
    REQUIRE( z2[2] == Approx( z[2] ).margin( 1e-6 ) );
}

// ---------------------------------------------------------------------------
// Slice 2: robust IRLS — asymmetric negative cloud spikes must not pull the
// fit down; the analytic sine is the independent truth (D16 §B).
// ---------------------------------------------------------------------------

TEST_CASE( "Robust Whittaker hugs the upper envelope under 30% negative spikes",
           "[d16][whittaker]" )
{
    const int n = 365;
    std::vector<float> truth( n );
    std::vector<float> corrupted( n );
    int spikeCount = 0;
    for ( int i = 0; i < n; ++i )
    {
        const double t = static_cast<double>( i );
        truth[i] = static_cast<float>( 0.5 + 0.3 * std::sin( 2.0 * M_PI * t / 365.0 ) );
        // Deterministic ~30% spike mask with spike depth -0.3 .. -0.5
        // (undetected cloud / shadow signature: only downward).
        const std::uint32_t h = static_cast<std::uint32_t>( i ) * 2654435761u;
        if ( h % 10u < 3u )
        {
            const double depth = 0.3 + 0.2 * static_cast<double>( ( h >> 8 ) % 5u ) / 4.0;
            corrupted[i] = static_cast<float>( truth[i] - depth );
            ++spikeCount;
        }
        else
        {
            corrupted[i] = truth[i];
        }
    }
    REQUIRE( spikeCount > 100 ); // the corruption is really there

    const auto robust = whittakerSmoothRobust( corrupted, {}, 100.0, 4 );
    REQUIRE( robust.size() == corrupted.size() );
    const double robustMae = mae( robust, truth );
    INFO( "robust MAE = " << robustMae );
    REQUIRE( robustMae < 0.02 );

    // The plain smoother demonstrably suffers on the same data.
    const auto plain = whittakerSmooth( corrupted, {}, 100.0, 2 );
    const double plainMae = mae( plain, truth );
    INFO( "plain MAE = " << plainMae );
    REQUIRE( plainMae > robustMae );

    // On clean data the robust iteration must not degrade the fit.
    const auto cleanFit = whittakerSmoothRobust( truth, {}, 100.0, 4 );
    REQUIRE( mae( cleanFit, truth ) < 0.005 );
}

// ---------------------------------------------------------------------------
// Slice 3: Savitzky-Golay + degenerate-input guards.
// ---------------------------------------------------------------------------

TEST_CASE( "Savitzky-Golay reproduces polynomials up to the fit degree",
           "[d16][whittaker]" )
{
    std::vector<float> linear( 15 );
    for ( int i = 0; i < 15; ++i )
        linear[i] = static_cast<float>( 2.0 * i - 7.0 );
    const auto z1 = savitzkyGolay( linear, 5, 2 );
    REQUIRE( z1.size() == linear.size() );
    REQUIRE( maxAbsDiff( z1, linear ) < 1e-4 );

    std::vector<float> quad( 15 );
    for ( int i = 0; i < 15; ++i )
        quad[i] = static_cast<float>( 0.5 * i * i - 3.0 * i + 1.0 );
    const auto z2 = savitzkyGolay( quad, 7, 2 );
    REQUIRE( maxAbsDiff( z2, quad ) < 1e-3 );

    // Cubic needs degree >= 3.
    std::vector<float> cubic( 15 );
    for ( int i = 0; i < 15; ++i )
        cubic[i] = static_cast<float>( ( i - 7 ) * ( i - 7 ) * ( i - 7 ) * 0.05 );
    const auto z3 = savitzkyGolay( cubic, 7, 3 );
    REQUIRE( maxAbsDiff( z3, cubic ) < 1e-3 );

    // Constant is invariant.
    const std::vector<float> flat( 11, 4.2f );
    REQUIRE( maxAbsDiff( savitzkyGolay( flat, 5, 3 ), flat ) < 1e-6 );
}

TEST_CASE( "Savitzky-Golay leaves positions without support as NaN",
           "[d16][whittaker]" )
{
    std::vector<float> y( 9, 1.0f );
    y[0] = kNan;
    y[1] = kNan;
    y[2] = kNan;
    y[3] = kNan;
    // Window 9, degree 4: position 0 has only 5 finite neighbors around it
    // inside its shrunk window -> NaN. Interior stays 1.
    const auto z = savitzkyGolay( y, 9, 4 );
    REQUIRE( z.size() == y.size() );
    REQUIRE( std::isnan( z[0] ) );
    REQUIRE( z[8] == Approx( 1.0 ).margin( 1e-6 ) );
    REQUIRE( z[5] == Approx( 1.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Degenerate inputs are refused or NaN-filled without crashing",
           "[d16][whittaker]" )
{
    const std::vector<float> y = { 1, 2, 3, 4, 5 };

    // Unsupported difference order and bad lambda -> empty (documented).
    REQUIRE( whittakerSmooth( y, {}, 10.0, 3 ).empty() );
    REQUIRE( whittakerSmooth( y, {}, 10.0, 0 ).empty() );
    REQUIRE( whittakerSmooth( y, {}, -1.0, 2 ).empty() );

    // All-NaN series -> all-NaN, same size (no data, no fit).
    const std::vector<float> holes( 9, kNan );
    const auto zHoles = whittakerSmooth( holes, {}, 10.0, 2 );
    REQUIRE( zHoles.size() == holes.size() );
    for ( float v : zHoles )
        REQUIRE( std::isnan( v ) );
    const auto zRobustHoles = whittakerSmoothRobust( holes, {}, 10.0, 3 );
    REQUIRE( zRobustHoles.size() == holes.size() );

    // Whittaker on a flat series with huge lambda: exact, no blow-up.
    const std::vector<float> flat( 21, 2.5f );
    const auto zFlat = whittakerSmooth( flat, {}, 1e9, 2 );
    REQUIRE( maxAbsDiff( zFlat, flat ) < 1e-4 );

    // SG: even window / degree out of range / empty -> empty (documented).
    REQUIRE( savitzkyGolay( y, 4, 2 ).empty() );
    REQUIRE( savitzkyGolay( y, 5, 0 ).empty() );
    REQUIRE( savitzkyGolay( y, 5, 5 ).empty() );
    REQUIRE( savitzkyGolay( {}, 5, 2 ).empty() );

    // SG on an all-flat singular-window series: no crash, finite output.
    const auto zFlatSg = savitzkyGolay( flat, 5, 3 );
    REQUIRE( zFlatSg.size() == flat.size() );
}
