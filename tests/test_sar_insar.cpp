// tests/test_sar_insar.cpp — InSAR base-chain kernels
// (Advanced SAR / PolSAR / InSAR 10.0, package C).
//
// Exact anchors: interferogram phase from known phasors; window coherence
// closed forms; Goldstein filter invariances; linear/quadratic ramp
// recovery (with outlier clipping); quality-guided unwrap exactness under
// the Itoh condition; closed-form displacement; coregistration shift on a
// synthetically displaced deterministic field; bilinear shift exactness.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <complex>
#include <vector>

#include "processing/algorithms/sar/sar_insar.h"

using namespace sicnu::sar;
using Catch::Approx;

namespace
{

using cd = std::complex<double>; // scalar phase/sample kernels stay double
using cf = std::complex<float>;

/// Deterministic complex test field: spatial phase ramps + amplitude
/// modulation, generated from a fixed LCG so the coregistration fixture is
/// reproducible without a random library.
std::vector<cf> synthField( int w, int h, double phaseSlopeX, double phaseSlopeY )
{
    std::vector<cf> field( static_cast<size_t>( w ) * h );
    unsigned lcg = 123456789u;
    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            lcg = lcg * 1103515245u + 12345u;
            const double amp = 0.75 + 0.5 * ( ( lcg >> 16 ) % 1024 ) / 1024.0;
            field[static_cast<size_t>( y ) * w + x] =
                static_cast<cf>( amp * std::exp( cd( 0, phaseSlopeX * x + phaseSlopeY * y ) ) );
        }
    }
    return field;
}

double wrapPi( double phase )
{
    return std::arg( std::exp( cd( 0, phase ) ) );
}

} // anonymous namespace

TEST_CASE( "interferogram phase and sample — closed forms", "[sar][insar]" )
{
    const cd s1 = std::exp( cd( 0, 1.2 ) );
    const cd s2 = std::exp( cd( 0, -0.4 ) );
    REQUIRE( interferogramPhase( s1, s2 ) == Approx( 1.6 ).margin( 1e-12 ) );
    REQUIRE( interferogramSample( s1, s2 ).real() == Approx( std::cos( 1.6 ) ).margin( 1e-12 ) );
    REQUIRE( interferogramSample( s1, s2 ).imag() == Approx( std::sin( 1.6 ) ).margin( 1e-12 ) );

    // Zero / invalid operands are NaN (never fabricated).
    REQUIRE( std::isnan( interferogramPhase( { 0, 0 }, s2 ) ) );
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE( std::isnan( interferogramPhase( { nan, 0 }, s2 ) ) );
}

TEST_CASE( "windowCoherence — exact values", "[sar][insar]" )
{
    SECTION( "identical fields: coherence 1" )
    {
        const std::vector<cf> f = synthField( 6, 5, 0.2, 0.1 );
        const double c = windowCoherence( f.data(), f.data(), 6, 5, 3, 2, 2 );
        REQUIRE( c == Approx( 1.0 ).margin( 1e-12 ) );
    }

    SECTION( "constructed partial coherence" )
    {
        // s1 = [1, 1], s2 = [1, j]: |Σ s1 conj(s2)|/sqrt(Σ|s1|²Σ|s2|²) = √2/2.
        const std::vector<cf> s1 = { { 1, 0 }, { 1, 0 } };
        const std::vector<cf> s2 = { { 1, 0 }, { 0, 1 } };
        const double c = windowCoherence( s1.data(), s2.data(), 2, 1, 0, 0, 1 );
        REQUIRE( c == Approx( std::sqrt( 2.0 ) / 2.0 ).margin( 1e-12 ) );
    }

    SECTION( "NaN pairs are skipped; empty window is NaN" )
    {
        std::vector<cf> s1( 4, cf( 1, 0 ) );
        std::vector<cf> s2( 4, cf( 1, 0 ) );
        s1[0] = { std::numeric_limits<float>::quiet_NaN(), 0 };
        s2[1] = { 0, std::numeric_limits<float>::quiet_NaN() };
        // Only 2 jointly valid pairs remain (indices 2, 3).
        const double c = windowCoherence( s1.data(), s2.data(), 2, 2, 0, 0, 1 );
        REQUIRE( c == Approx( 1.0 ).margin( 1e-12 ) );

        std::vector<cf> nan1( 4, cf( std::numeric_limits<float>::quiet_NaN(), 0 ) );
        REQUIRE( std::isnan( windowCoherence( nan1.data(), s2.data(), 2, 2, 0, 0, 1 ) ) );
    }
}

TEST_CASE( "goldsteinPhase — invariances", "[sar][insar]" )
{
    SECTION( "constant phase field is invariant for any α" )
    {
        std::vector<cf> f( 25 );
        for ( auto &z : f )
            z = static_cast<cf>( 2.0 * std::exp( cd( 0, 0.7 ) ) );
        const double p1 = goldsteinPhase( f.data(), 5, 5, 2, 2, 2, 0.0 );
        const double p2 = goldsteinPhase( f.data(), 5, 5, 2, 2, 2, 1.0 );
        REQUIRE( p1 == Approx( 0.7 ).margin( 1e-12 ) );
        REQUIRE( p2 == Approx( 0.7 ).margin( 1e-12 ) );
    }

    SECTION( "α = 0: symmetric phasors average to the mid phase" )
    {
        std::vector<cf> f = { cf( std::cos( 0.3 ), std::sin( 0.3 ) ),
                              cf( std::cos( -0.3 ), std::sin( -0.3 ) ) };
        const double p = goldsteinPhase( f.data(), 2, 1, 0, 0, 1, 0.0 );
        REQUIRE( std::abs( p ) < 1e-12 );
    }

    SECTION( "α = 1: heavier sample dominates" )
    {
        std::vector<cf> f = { cf( std::cos( 0.9 ), std::sin( 0.9 ) ),
                              4.0f * cf( std::cos( 0.9 ), -std::sin( 0.9 ) ) };
        const double p = goldsteinPhase( f.data(), 2, 1, 0, 0, 1, 1.0 );
        // Magnitude-weighted result must be closer to the heavy phase.
        REQUIRE( p < 0.0 );
        REQUIRE( std::abs( p ) > 0.3 );
    }

    SECTION( "all-NaN window → NaN" )
    {
        std::vector<cf> f( 4, cf( std::numeric_limits<float>::quiet_NaN(), 0 ) );
        REQUIRE( std::isnan( goldsteinPhase( f.data(), 2, 2, 0, 0, 1, 0.5 ) ) );
    }
}

TEST_CASE( "fitPhaseRamp — exact recovery", "[sar][insar]" )
{
    const int w = 20;
    const int h = 15;

    SECTION( "linear plane" )
    {
        std::vector<double> phase( static_cast<size_t>( w ) * h );
        for ( int y = 0; y < h; ++y )
            for ( int x = 0; x < w; ++x )
                phase[static_cast<size_t>( y ) * w + x] = 0.01 * x - 0.003 * y + 0.5;

        PhaseRampModel m;
        REQUIRE( fitPhaseRamp( phase.data(), w, h, /*quadratic=*/false, &m ) );
        REQUIRE( m.coef[0] == Approx( 0.5 ).margin( 1e-9 ) );
        REQUIRE( m.coef[1] == Approx( 0.01 ).margin( 1e-12 ) );
        REQUIRE( m.coef[2] == Approx( -0.003 ).margin( 1e-12 ) );
    }

    SECTION( "quadratic surface (exact under quadratic fit)" )
    {
        std::vector<double> phase( static_cast<size_t>( w ) * h );
        for ( int y = 0; y < h; ++y )
            for ( int x = 0; x < w; ++x )
                phase[static_cast<size_t>( y ) * w + x] = 0.002 * x * x + 0.001 * x * y
                                                          + 0.0005 * y * y;
        PhaseRampModel m;
        REQUIRE( fitPhaseRamp( phase.data(), w, h, /*quadratic=*/true, &m ) );
        REQUIRE( m.coef[0] == Approx( 0.0 ).margin( 1e-9 ) );
        REQUIRE( m.coef[3] == Approx( 0.002 ).margin( 1e-12 ) );
        REQUIRE( m.coef[4] == Approx( 0.001 ).margin( 1e-12 ) );
        REQUIRE( m.coef[5] == Approx( 0.0005 ).margin( 1e-12 ) );
    }

    SECTION( "IQR clipping rejects 2π spikes" )
    {
        std::vector<double> phase( static_cast<size_t>( w ) * h );
        for ( int y = 0; y < h; ++y )
            for ( int x = 0; x < w; ++x )
                phase[static_cast<size_t>( y ) * w + x] = 0.01 * x - 0.003 * y;
        // Corrupt a scattered minority with large outliers.
        phase[7 * w + 3] += 6.0;
        phase[11 * w + 15] += -8.0;
        phase[2 * w + 17] += 10.0;

        PhaseRampModel m;
        REQUIRE( fitPhaseRamp( phase.data(), w, h, false, &m ) );
        REQUIRE( m.coef[1] == Approx( 0.01 ).margin( 1e-6 ) );
        REQUIRE( m.coef[2] == Approx( -0.003 ).margin( 1e-6 ) );
    }

    SECTION( "all-NaN plane fails (no fabrication)" )
    {
        std::vector<double> phase( static_cast<size_t>( w ) * h,
                                   std::numeric_limits<double>::quiet_NaN() );
        PhaseRampModel m;
        REQUIRE_FALSE( fitPhaseRamp( phase.data(), w, h, false, &m ) );
    }
}

TEST_CASE( "qualityGuidedUnwrap — exact under the Itoh condition", "[sar][insar]" )
{
    const int w = 12;
    const int h = 9;
    const double slopeX = 0.4;
    const double slopeY = 0.25;

    std::vector<double> truth( static_cast<size_t>( w ) * h );
    std::vector<double> wrapped( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const double phi = slopeX * x + slopeY * y;
            truth[static_cast<size_t>( y ) * w + x] = phi;
            wrapped[static_cast<size_t>( y ) * w + x] = wrapPi( phi );
        }

    UnwrapResult r;
    REQUIRE( qualityGuidedUnwrap( wrapped.data(), nullptr, w, h, &r ) );
    REQUIRE( r.unwrappedCount == static_cast<long>( w ) * h );
    REQUIRE( r.seeds == 1 );

    // Uniform quality → the first valid pixel (index 0) is the seed; the
    // unwrapped field is exact modulo that seed's absolute value.
    const int seedIdx = 0;
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * w + x;
            const double rel = r.unwrapped[i] - r.unwrapped[static_cast<size_t>( seedIdx )];
            const double truthRel = truth[i] - truth[static_cast<size_t>( seedIdx )];
            REQUIRE( rel == Approx( truthRel ).margin( 1e-9 ) );
        }
}

TEST_CASE( "qualityGuidedUnwrap — holes, islands, empty", "[sar][insar]" )
{
    SECTION( "NaN hole splits the field; only the seed component unwraps" )
    {
        const int w = 6;
        const int h = 2;
        std::vector<double> wrapped( static_cast<size_t>( w ) * h );
        for ( int y = 0; y < h; ++y )
            for ( int x = 0; x < w; ++x )
                wrapped[static_cast<size_t>( y ) * w + x] = wrapPi( 0.5 * x );
        // Wall of NaN at x = 3 splits left/right components.
        for ( int y = 0; y < h; ++y )
            wrapped[static_cast<size_t>( y ) * w + 3]
                = std::numeric_limits<double>::quiet_NaN();

        UnwrapResult r;
        REQUIRE( qualityGuidedUnwrap( wrapped.data(), nullptr, w, h, &r ) );
        REQUIRE( r.unwrappedCount == 10 ); // 12 − 2 NaN
        for ( int y = 0; y < h; ++y )
            REQUIRE( std::isnan( r.unwrapped[static_cast<size_t>( y ) * w + 3] ) );
    }

    SECTION( "all-NaN: empty honest result" )
    {
        const int w = 3;
        const int h = 3;
        std::vector<double> wrapped( static_cast<size_t>( w ) * h,
                                     std::numeric_limits<double>::quiet_NaN() );
        UnwrapResult r;
        REQUIRE( qualityGuidedUnwrap( wrapped.data(), nullptr, w, h, &r ) );
        REQUIRE( r.seeds == 0 );
        REQUIRE( r.unwrappedCount == 0 );
    }

    SECTION( "quality input guides the seed (deterministic)" )
    {
        const int w = 4;
        const int h = 4;
        std::vector<double> wrapped( static_cast<size_t>( w ) * h, 0.1 );
        std::vector<double> quality( static_cast<size_t>( w ) * h, 0.5 );
        quality[2 * w + 3] = 0.95; // highest quality → must be the seed
        UnwrapResult r1;
        REQUIRE( qualityGuidedUnwrap( wrapped.data(), quality.data(), w, h, &r1 ) );
        REQUIRE( r1.unwrapped[2 * w + 3] == Approx( 0.1 ) );
        UnwrapResult r2;
        REQUIRE( qualityGuidedUnwrap( wrapped.data(), quality.data(), w, h, &r2 ) );
        REQUIRE( r1.unwrapped == r2.unwrapped );
    }
}

TEST_CASE( "displacement and Itoh diagnostics", "[sar][insar]" )
{
    REQUIRE( losDisplacementM( M_PI, 0.056 ) == Approx( -0.056 / 4.0 ).margin( 1e-15 ) );
    REQUIRE( std::isnan( losDisplacementM( M_PI, 0.0 ) ) );
    REQUIRE( std::isnan(
        losDisplacementM( std::numeric_limits<double>::quiet_NaN(), 0.056 ) ) );

    SECTION( "smooth ramp: zero discontinuity" )
    {
        std::vector<double> phase;
        for ( int i = 0; i < 10; ++i )
            phase.push_back( 0.1 * i );
        REQUIRE( phaseDiscontinuityRatio( phase.data(), 10, 1 )
                 == Approx( 0.0 ).margin( 1e-15 ) );
    }

    SECTION( "wrapped field: jumps detected" )
    {
        std::vector<double> wrapped;
        for ( int i = 0; i < 10; ++i )
            wrapped.push_back( wrapPi( 1.0 * i ) ); // 1 rad steps wrap at π
        const double ratio = phaseDiscontinuityRatio( wrapped.data(), 10, 1 );
        REQUIRE( ratio > 0.0 );
        REQUIRE( ratio < 1.0 );
    }

    SECTION( "all-NaN: NaN ratio" )
    {
        std::vector<double> phase( 4, std::numeric_limits<double>::quiet_NaN() );
        REQUIRE( std::isnan( phaseDiscontinuityRatio( phase.data(), 2, 2 ) ) );
    }
}

TEST_CASE( "coregistrationShift — recovers a synthetic global shift", "[sar][insar]" )
{
    const int w = 48;
    const int h = 40;
    const std::vector<cf> master = synthField( w, h, 0.11, 0.07 );

    // Slave = master displaced by (+2, −1) (slave(x, y) = master(x − 2, y + 1)).
    const double trueDx = 2.0;
    const double trueDy = -1.0;
    std::vector<cf> slave( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const int sx = x - static_cast<int>( trueDx );
            const int sy = y - static_cast<int>( trueDy );
            if ( sx >= 0 && sx < w && sy >= 0 && sy < h )
                slave[static_cast<size_t>( y ) * w + x] =
                    master[static_cast<size_t>( sy ) * w + sx];
            else
                slave[static_cast<size_t>( y ) * w + x] =
                    cf( std::numeric_limits<float>::quiet_NaN(), 0 );
        }

    CoregisterShift shift;
    REQUIRE( coregistrationShift( master.data(), slave.data(), w, h,
                                  /*searchRadius=*/6, /*patchSize=*/16, /*patchStride=*/8,
                                  /*minPeakRatio=*/0.9, &shift ) );
    REQUIRE( shift.confidentPatches >= 3 );
    REQUIRE( shift.dx == Approx( trueDx ).margin( 0.25 ) );
    REQUIRE( shift.dy == Approx( trueDy ).margin( 0.25 ) );
}

TEST_CASE( "coregistrationShift — degenerate fields refuse", "[sar][insar]" )
{
    const int w = 24;
    const int h = 24;
    std::vector<cf> uniform( static_cast<size_t>( w ) * h, cf( 1, 0 ) );
    CoregisterShift shift;
    // Uniform fields have no confident peak structure → refuse.
    REQUIRE_FALSE( coregistrationShift( uniform.data(), uniform.data(), w, h, 3, 8, 8, 0.9,
                                        &shift ) );
}

TEST_CASE( "shiftComplexBilinear — exact for integer and linear fields", "[sar][insar]" )
{
    const int w = 8;
    const int h = 6;
    std::vector<cf> src( static_cast<size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
            src[static_cast<size_t>( y ) * w + x] = cf( static_cast<float>( x ),
                                                         static_cast<float>( y ) );

    SECTION( "integer shift" )
    {
        std::vector<cf> dst( static_cast<size_t>( w ) * h );
        shiftComplexBilinear( src.data(), w, h, 2.0, -1.0, dst.data() );
        // dst(x, y) = src(x−2, y+1): valid for x ≤ w−3 and y ≤ h−2.
        for ( int y = 0; y < h - 1; ++y )
            for ( int x = 2; x < w; ++x )
            {
                const cd v = dst[static_cast<size_t>( y ) * w + x];
                REQUIRE( v.real() == Approx( x - 2.0 ).margin( 1e-12 ) );
                REQUIRE( v.imag() == Approx( y + 1.0 ).margin( 1e-12 ) );
            }
        // Out-of-range edge is NaN, never wrapped.
        REQUIRE( std::isnan( dst[0].real() ) );
        REQUIRE( std::isnan( dst[static_cast<size_t>( h - 1 ) * w].real() ) );
    }

    SECTION( "fractional shift of a linear field interpolates exactly" )
    {
        std::vector<cf> dst( static_cast<size_t>( w ) * h );
        shiftComplexBilinear( src.data(), w, h, 0.5, 0.0, dst.data() );
        if ( true )
            for ( int y = 0; y < 2; ++y )
            {
                for ( int x = 0; x < 5; ++x )
                    std::printf( "%6.2f", dst[static_cast<size_t>( y ) * w + x].real() );
                std::printf( "\n" );
            }
        for ( int y = 0; y < h; ++y )
            for ( int x = 1; x < w - 1; ++x )
                REQUIRE( dst[static_cast<size_t>( y ) * w + x].real()
                         == Approx( x - 0.5 ).margin( 1e-12 ) );
    }
}
