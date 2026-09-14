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

// ---------------------------------------------------------------------------
// Slice 2: asymmetric double-logistic fit (Levenberg–Marquardt).
// ---------------------------------------------------------------------------

#include "processing/algorithms/phenology_metrics.h"

namespace
{
/// The documented model, rebuilt here from the paper definition
/// f = base + amp·[σ(k1(t−τ1)) + σ(−k2(t−τ2)) − 1] — algebraically identical
/// to σ(k1(t−τ1)) − σ(k2(t−τ2)) but derived independently in the test.
double logistic( double x ) { return 1.0 / ( 1.0 + std::exp( -x ) ); }
double modelValue( const sicnu::temporal::DoubleLogisticParams &p, double t )
{
    const double rising = logistic( p.sosRate * ( t - p.sosInflection ) );
    const double falling = 1.0 / ( 1.0 + std::exp( p.eosRate * ( t - p.eosInflection ) ) );
    return p.baseVal + p.amplitude * ( rising + falling - 1.0 );
}
} // namespace

TEST_CASE( "Double-logistic fit recovers injected parameters and phenology",
           "[d16][phenology]" )
{
    sicnu::temporal::DoubleLogisticParams injected;
    injected.baseVal = 0.2;
    injected.amplitude = 0.6;
    injected.sosInflection = 100.0;
    injected.sosRate = 0.15;
    injected.eosInflection = 250.0;
    injected.eosRate = 0.12;

    // Sample the asymmetric curve every 8 days across one year.
    std::vector<float> y;
    std::vector<double> t;
    for ( int i = 0; i < 365; i += 8 )
    {
        t.push_back( static_cast<double>( i ) );
        y.push_back( static_cast<float>( modelValue( injected, static_cast<double>( i ) ) ) );
    }
    REQUIRE( y.size() == 46 );

    const auto [params, metrics] = PhenologyExtractor::fitDoubleLogistic( y, t );

    REQUIRE( metrics.valid );
    REQUIRE( params.baseVal == Approx( injected.baseVal ).margin( 0.02 ) );
    REQUIRE( params.amplitude == Approx( injected.amplitude ).margin( 0.02 * injected.amplitude ) );
    REQUIRE( params.sosInflection == Approx( injected.sosInflection ).margin( 1.5 ) );
    REQUIRE( params.sosRate == Approx( injected.sosRate ).margin( 0.1 * injected.sosRate ) );
    REQUIRE( params.eosInflection == Approx( injected.eosInflection ).margin( 1.5 ) );
    REQUIRE( params.eosRate == Approx( injected.eosRate ).margin( 0.1 * injected.eosRate ) );

    // Phenology from curvature extremes: sos = τ1, eos = τ2.
    REQUIRE( metrics.sos == Approx( injected.sosInflection ).margin( 1.5 ) );
    REQUIRE( metrics.eos == Approx( injected.eosInflection ).margin( 1.5 ) );

    // POS: fine-grid argmax of the analytic model (independent reference).
    double posRef = 0.0;
    double posVal = -1.0;
    for ( double tt = 0.0; tt <= 364.0; tt += 0.05 )
    {
        const double v = modelValue( injected, tt );
        if ( v > posVal )
        {
            posVal = v;
            posRef = tt;
        }
    }
    REQUIRE( metrics.pos == Approx( posRef ).margin( 4.0 ) ); // 8-day sampling axis
    REQUIRE( metrics.sos < metrics.pos );
    REQUIRE( metrics.pos < metrics.eos );

    // Integral: fitted-curve trapezoid vs the analytic integral (Simpson on
    // a 0.05-day grid) within 2%.
    double integralRef = 0.0;
    {
        const double h = 0.05;
        double s = 0.0;
        for ( double x = injected.sosInflection; x < injected.eosInflection; x += h )
        {
            const double f0 = modelValue( injected, x );
            const double f1 = modelValue( injected, x + h );
            const double fm = modelValue( injected, x + h / 2.0 );
            s += ( f0 + 4.0 * fm + f1 ) * h / 6.0;
        }
        integralRef = s;
    }
    REQUIRE( metrics.integral == Approx( integralRef ).margin( 0.02 * integralRef ) );
}

// ---------------------------------------------------------------------------
// Slice 3: multi-cycle (double-crop) segmentation + monotonicity guards.
// ---------------------------------------------------------------------------

TEST_CASE( "Multi-cycle extraction separates a double-crop paddy season",
           "[d16][phenology]" )
{
    // Two well-separated Gaussian seasons: cycle 1 peaks at doy 140,
    // cycle 2 at doy 265 (both on the daily sample grid).
    const GaussianSeason first{ 0.1, 0.7, 140.0, 13.0 };
    const GaussianSeason second{ 0.1, 0.55, 265.0, 18.0 };
    const auto t = dailyAxis();
    std::vector<float> y( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( first.value( t[i] ) + second.value( t[i] ) - first.base );

    const auto results = PhenologyExtractor::extractMultiCycle( y, t, 2, 0.2 );
    REQUIRE( results.size() == 2 );

    // Independent closed-form truths per season (same Gaussian algebra as
    // the single-season case; each segment's range is its own peak/base).
    const ThresholdTruth truth1( first, 0.2 );
    const ThresholdTruth truth2( second, 0.2 );
    const double c2 = second.sigma * std::sqrt( -2.0 * std::log( 0.2 ) );

    INFO( "c1: sos=" << results[0].sos << " pos=" << results[0].pos
          << " eos=" << results[0].eos );
    INFO( "c2: sos=" << results[1].sos << " pos=" << results[1].pos
          << " eos=" << results[1].eos );
    REQUIRE( results[0].sos == Approx( truth1.sos ).margin( 1.0 ) );
    REQUIRE( results[0].pos == Approx( 140.0 ).margin( 0.5 ) );
    REQUIRE( results[0].eos == Approx( truth1.eos ).margin( 1.0 ) );
    REQUIRE( results[1].sos == Approx( second.mu - c2 ).margin( 1.0 ) );
    REQUIRE( results[1].pos == Approx( 265.0 ).margin( 0.5 ) );
    REQUIRE( results[1].eos == Approx( second.mu + c2 ).margin( 1.0 ) );

    // Biology monotonicity inside every cycle, and strict cycle ordering.
    for ( const auto &m : results )
    {
        REQUIRE( m.valid );
        REQUIRE( m.sos < m.pos );
        REQUIRE( m.pos < m.eos );
        REQUIRE( m.los == Approx( m.eos - m.sos ).margin( 1e-6 ) );
    }
    REQUIRE( results[0].eos < results[1].sos );
}

TEST_CASE( "Multi-cycle refuses to fabricate cycles that are not in the data",
           "[d16][phenology]" )
{
    const GaussianSeason g;
    const auto t = dailyAxis();
    std::vector<float> single( t.size() );
    for ( std::size_t i = 0; i < t.size(); ++i )
        single[i] = static_cast<float>( g.value( t[i] ) );

    // One season only: asking for 2 cycles returns what exists (1), never a
    // padded or merged duplicate.
    const auto results = PhenologyExtractor::extractMultiCycle( single, t, 2, 0.2 );
    REQUIRE( results.size() == 1 );
    REQUIRE( results[0].valid );

    // Flat data: no cycles at all.
    std::vector<float> flat( t.size(), 0.3f );
    REQUIRE( PhenologyExtractor::extractMultiCycle( flat, t, 2, 0.2 ).empty() );
}
