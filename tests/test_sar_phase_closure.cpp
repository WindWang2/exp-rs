// tests/test_sar_phase_closure.cpp — interferometric phase-closure QA
// (Advanced InSAR 11.0, package E).
//
// The oracle is the ALGEBRAIC INVARIANT itself: I_ab·I_bc·I_ca =
// a·conj b · b·conj c · c·conj a = |a||b||c| is positive real, so the
// wrapped closure of three interferograms formed from ONE consistent
// same-grid SLC stack is exactly 0 — INCLUDING with per-scene phase
// injections (deformation is common-mode and cancels). An INCONSISTENT
// stack breaks the identity: when the a–b interferogram pairs samples at
// different grid locations than the b–c one (misaligned resampling), the
// cancellation fails and the closure deviates.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_phase_closure.h"

#include <cmath>
#include <complex>
#include <vector>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr int kW = 24;
constexpr int kH = 16;

std::vector<std::complex<float>> makeSlc( unsigned seed )
{
    std::vector<std::complex<float>> slc( static_cast<size_t>( kW ) * kH );
    for ( auto &z : slc )
    {
        seed = seed * 1103515245u + 12345u;
        const double amp = 0.5 + ( ( seed >> 8 ) % 1000 ) / 1000.0;
        seed = seed * 1103515245u + 12345u;
        const double phase = -M_PI + 2.0 * M_PI * ( ( seed >> 8 ) % 1000 ) / 1000.0;
        z = static_cast<std::complex<float>>( std::polar( amp, phase ) );
    }
    return slc;
}

/// Multiplies every sample by e^{i·δ} — a per-scene phase injection
/// (deformation/atmosphere stand-in). Common-mode per scene: the closure
/// MUST stay at zero.
void injectScenePhase( std::vector<std::complex<float>> &slc, double deltaRad )
{
    for ( auto &z : slc )
        z *= std::polar( 1.0f, static_cast<float>( deltaRad ) );
}

/// Pairwise interferogram products of a consistent stack.
void makeInterferograms( const std::vector<std::complex<float>> &a,
                         const std::vector<std::complex<float>> &b,
                         const std::vector<std::complex<float>> &c,
                         std::vector<std::complex<float>> &iab,
                         std::vector<std::complex<float>> &ibc,
                         std::vector<std::complex<float>> &ica )
{
    const size_t n = a.size();
    iab.resize( n );
    ibc.resize( n );
    ica.resize( n );
    for ( size_t i = 0; i < n; ++i )
    {
        iab[i] = a[i] * std::conj( b[i] );
        ibc[i] = b[i] * std::conj( c[i] );
        ica[i] = c[i] * std::conj( a[i] );
    }
}
} // namespace

TEST_CASE( "Closure of a consistent stack is identically zero, even with "
           "per-scene phase", "[sar][phase-closure][insar11]" )
{
    const auto a = makeSlc( 123456789u );
    auto b = makeSlc( 987654321u );
    const auto c = makeSlc( 555555555u );

    // Per-scene phase injections (deformation stand-in): common-mode.
    injectScenePhase( b, 0.9 );

    std::vector<std::complex<float>> iab, ibc, ica;
    makeInterferograms( a, b, c, iab, ibc, ica );

    PhaseClosureStats stats;
    std::vector<double> closure( static_cast<size_t>( kW ) * kH, 0.0 );
    phaseClosurePlane( iab.data(), ibc.data(), ica.data(), kW, kH, closure.data(),
                       &stats );

    REQUIRE( stats.evaluated == static_cast<long long>( kW ) * kH );
    REQUIRE( stats.validCount == stats.evaluated );
    REQUIRE( stats.invalidCount == 0 );
    REQUIRE( stats.maxAbsClosureRad < 1e-5 );
    REQUIRE( stats.rmsClosureRad < 1e-5 );
}

TEST_CASE( "An inconsistently formed stack breaks the closure identity",
           "[sar][phase-closure][insar11]" )
{
    const auto a = makeSlc( 1111u );
    const auto b = makeSlc( 2222u );
    const auto c = makeSlc( 3333u );

    // The a–b interferogram was formed from a MISALIGNED resampling of b
    // (sample x+1 while the b–c product uses sample x): the three products
    // no longer share one consistent sampling.
    std::vector<std::complex<float>> iab( static_cast<size_t>( kW ) * kH );
    std::vector<std::complex<float>> ibc( static_cast<size_t>( kW ) * kH );
    std::vector<std::complex<float>> ica( static_cast<size_t>( kW ) * kH );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * kW + x;
            const size_t j = static_cast<size_t>( y ) * kW
                             + std::min( x + 1, kW - 1 );
            iab[i] = a[i] * std::conj( b[j] );
            ibc[i] = b[i] * std::conj( c[i] );
            ica[i] = c[i] * std::conj( a[i] );
        }

    PhaseClosureStats stats;
    phaseClosurePlane( iab.data(), ibc.data(), ica.data(), kW, kH, nullptr, &stats );
    REQUIRE( stats.rmsClosureRad > 0.1 );
}

TEST_CASE( "Invalid operands stay NaN and are counted", "[sar][phase-closure][insar11]" )
{
    const auto a = makeSlc( 777u );
    const auto b = makeSlc( 888u );
    const auto c = makeSlc( 999u );
    std::vector<std::complex<float>> iab, ibc, ica;
    makeInterferograms( a, b, c, iab, ibc, ica );
    const size_t holes[] = { 5, 100, 200 };
    for ( const size_t i : holes )
        iab[i] = { std::numeric_limits<float>::quiet_NaN(),
                   std::numeric_limits<float>::quiet_NaN() };

    PhaseClosureStats stats;
    std::vector<double> closure( static_cast<size_t>( kW ) * kH, 0.0 );
    phaseClosurePlane( iab.data(), ibc.data(), ica.data(), kW, kH, closure.data(),
                       &stats );
    REQUIRE( stats.validCount == stats.evaluated - 3 );
    REQUIRE( stats.invalidCount == 3 );
    for ( const size_t i : holes )
        REQUIRE( std::isnan( closure[i] ) );
}
