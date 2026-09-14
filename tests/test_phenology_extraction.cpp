// tests/test_phenology_extraction.cpp — D16 Package C: phenology metrics.
// Expected values come from closed-form crossing solutions of the analytic
// season curves and fine-grid reference maxima — never from the code under
// test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/phenology_metrics.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using sicnu::temporal::PhenologyExtractor;
using sicnu::temporal::PhenologyMetrics;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Gaussian season over a baseline: y = base + amp·exp(−(t−μ)²/2σ²).
struct GaussianSeason
{
    double base = 0.1;
    double amp = 0.7;
    double mu = 140.0;
    double sigma = 13.0;

    double value( double t ) const
    {
        const double u = ( t - mu ) / sigma;
        return base + amp * std::exp( -0.5 * u * u );
    }
};

/// Closed-form dynamic-threshold crossings of the Gaussian season:
/// ratio f is reached at t = μ ∓ σ·√(−2·ln f) (ratio = (y−base)/amp here,
/// because z_min = base and z_max = base + amp).
struct ThresholdTruth
{
    double sos = 0.0;
    double eos = 0.0;
    explicit ThresholdTruth( const GaussianSeason &g, double f )
    {
        const double c = g.sigma * std::sqrt( -2.0 * std::log( f ) );
        sos = g.mu - c;
        eos = g.mu + c;
    }
};

std::vector<double> dailyAxis( int n = 365 )
{
    std::vector<double> t( static_cast<std::size_t>( n ) );
    for ( int i = 0; i < n; ++i )
        t[static_cast<std::size_t>( i )] = static_cast<double>( i );
    return t;
}

} // namespace

TEST_CASE( "Dynamic threshold hits closed-form Gaussian crossings within one day",
           "[d16][phenology]" )
{
    const GaussianSeason g;
    const auto t = dailyAxis();
    std::vector<float> y( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( g.value( t[i] ) );

    const double f = 0.2;
    const ThresholdTruth truth( g, f );
    const auto m = PhenologyExtractor::extractDynamicThreshold( y, t, f, 1, 365 );

    REQUIRE( m.valid );
    INFO( "sos=" << m.sos << " pos=" << m.pos << " eos=" << m.eos );
    REQUIRE( m.sos == Approx( truth.sos ).margin( 1.0 ) );
    REQUIRE( m.eos == Approx( truth.eos ).margin( 1.0 ) );
    REQUIRE( m.pos == Approx( g.mu ).margin( 0.5 ) ); // sample grid includes 140
    REQUIRE( m.baseVal == Approx( g.base ).margin( 1e-3 ) );
    REQUIRE( m.peakVal == Approx( g.base + g.amp ).margin( 1e-3 ) );
    REQUIRE( m.los == Approx( truth.eos - truth.sos ).margin( 1.0 ) );

    // Biology guard: strictly ordered season.
    REQUIRE( m.sos < m.pos );
    REQUIRE( m.pos < m.eos );

    // Integral: analytic ∫ over [sos, eos] of the Gaussian season (erf),
    // matched by the trapezoid over the daily grid within 0.5%.
    const double w = truth.eos - truth.sos;
    const double c = w / 2.0;
    const double analytic =
        g.base * w + g.amp * g.sigma * std::sqrt( 2.0 * M_PI ) *
                         std::erf( c / ( g.sigma * std::sqrt( 2.0 ) ) );
    INFO( "integral=" << m.integral << " analytic=" << analytic );
    REQUIRE( m.integral == Approx( analytic ).margin( 0.005 * analytic ) );
}

TEST_CASE( "Dynamic threshold refuses degenerate or biology-reversed seasons",
           "[d16][phenology]" )
{
    const auto t = dailyAxis( 60 );

    // Flat window: no range, no ratio.
    std::vector<float> flat( t.size(), 0.4f );
    REQUIRE( !PhenologyExtractor::extractDynamicThreshold( flat, t, 0.2, 1, 365 ).valid );

    // Rising-only: no falling limb, EOS undefined.
    std::vector<float> rising( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        rising[i] = static_cast<float>( 0.1 + 0.005 * t[i] );
    REQUIRE( !PhenologyExtractor::extractDynamicThreshold( rising, t, 0.2, 1, 365 ).valid );

    // Too few finite samples.
    std::vector<float> tiny = { 0.1f, kNan, 0.5f };
    std::vector<double> tTiny = { 0.0, 1.0, 2.0 };
    REQUIRE( !PhenologyExtractor::extractDynamicThreshold( tiny, tTiny, 0.2, 1, 365 ).valid );
}
