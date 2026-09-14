// tests/test_whittaker_smooth.cpp — D16 Package B: robust time-series
// smoothing. Expected values come from the documented normal equations
// (independent construction of W + λDᵀD in this file), analytic sine
// truths, and polynomial reproduction identities — never from the code
// under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal_smoothing.h"

#include <cmath>
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
