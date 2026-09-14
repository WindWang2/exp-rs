// tests/test_d16_temporal_trend.cpp — D16 Package E: Theil-Sen / Mann-Kendall.
// Truth: hand-derived S statistics, Gilbert's closed-form variance, and
// definitional median-of-pairwise-slopes computed independently here.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/trend_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using sicnu::temporal::MannKendallResult;
using sicnu::temporal::TrendAnalyzer;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Independent reference: S, tie-corrected var(S) and z per Gilbert 1987.
struct GilbertReference
{
    long S = 0;
    double varS = 0.0;
    double z = 0.0;

    GilbertReference( const std::vector<float> &y, const std::vector<double> &t )
    {
        const std::size_t n = y.size();
        for ( std::size_t i = 0; i < n; ++i )
            for ( std::size_t j = i + 1; j < n; ++j )
            {
                if ( !( t[j] > t[i] ) )
                    continue;
                if ( y[j] > y[i] )
                    ++S;
                else if ( y[j] < y[i] )
                    --S;
            }
        // Tie groups on the values.
        std::vector<float> sorted( y );
        std::sort( sorted.begin(), sorted.end() );
        double tieTerm = 0.0;
        for ( std::size_t i = 0; i < n; )
        {
            std::size_t j = i;
            while ( j < n && sorted[j] == sorted[i] )
                ++j;
            const double tp = static_cast<double>( j - i );
            if ( tp > 1.0 )
                tieTerm += tp * ( tp - 1.0 ) * ( 2.0 * tp + 5.0 );
            i = j;
        }
        const double nn = static_cast<double>( n );
        varS = ( nn * ( nn - 1.0 ) * ( 2.0 * nn + 5.0 ) - tieTerm ) / 18.0;
        if ( S > 0 )
            z = ( static_cast<double>( S ) - 1.0 ) / std::sqrt( varS );
        else if ( S < 0 )
            z = ( static_cast<double>( S ) + 1.0 ) / std::sqrt( varS );
    }
};

/// Independent Sen-slope reference: median of pairwise slopes (t_i < t_j).
double senSlopeReference( const std::vector<float> &y, const std::vector<double> &t )
{
    std::vector<double> slopes;
    for ( std::size_t i = 0; i < y.size(); ++i )
        for ( std::size_t j = i + 1; j < y.size(); ++j )
            if ( t[j] > t[i] )
                slopes.push_back( ( static_cast<double>( y[j] ) - y[i] ) / ( t[j] - t[i] ) );
    if ( slopes.empty() )
        return kNan;
    std::sort( slopes.begin(), slopes.end() );
    const std::size_t m = slopes.size();
    return m % 2 == 1 ? slopes[m / 2] : 0.5 * ( slopes[m / 2 - 1] + slopes[m / 2] );
}

} // namespace

TEST_CASE( "Strictly monotonic series yields the exact slope and perfect tau",
           "[d16][trend]" )
{
    // y = 3·t + 1 over 5 points: every pairwise slope is exactly 3.
    std::vector<double> t = { 1, 2, 3, 4, 5 };
    std::vector<float> y = { 4, 7, 10, 13, 16 };

    const auto r = TrendAnalyzer::computeMannKendall( y, t );
    REQUIRE( r.valid );
    REQUIRE( r.sampleCount == 5 );
    REQUIRE( r.senSlope == Approx( 3.0 ).margin( 1e-12 ) );
    REQUIRE( r.intercept == Approx( 1.0 ).margin( 1e-9 ) );
    // S = +10 for 5 strictly increasing points; tau-a = S/10 = 1.
    REQUIRE( r.tau == Approx( 1.0 ).margin( 1e-12 ) );
    const GilbertReference ref( y, t );
    REQUIRE( r.tauVariance == Approx( ref.varS ).margin( 1e-9 ) );
    REQUIRE( r.zScore == Approx( ref.z ).margin( 1e-9 ) );
    // n = 5 caps the test at z = 9/sqrt(16.667) = 2.204 -> p = 0.027:
    // significant at 0.05, provably not at 0.01 (Gilbert case follows).
    REQUIRE( r.pValue == Approx( std::erfc( ref.z / std::sqrt( 2.0 ) ) ).margin( 1e-9 ) );
    REQUIRE( r.isSignificant( 0.05 ) );
    REQUIRE( !r.isSignificant( 0.01 ) );

    // The decreasing series negates S, tau and z on the same time axis.
    std::vector<float> yDown( y.rbegin(), y.rend() );
    const auto d = TrendAnalyzer::computeMannKendall( yDown, t );
    REQUIRE( d.valid );
    REQUIRE( d.senSlope == Approx( -3.0 ).margin( 1e-12 ) );
    REQUIRE( d.tau == Approx( -1.0 ).margin( 1e-12 ) );
    REQUIRE( d.zScore < 0.0 );
}

// ---------------------------------------------------------------------------
// Slice 2: Gilbert (1987) textbook benchmark with ties.
// ---------------------------------------------------------------------------

TEST_CASE( "Gilbert benchmark: tie-corrected variance, z and significance",
           "[d16][trend]" )
{
    // Gilbert (1987) case study series: one tie group (25.0 twice), one
    // negative pair (15 -> 14). From the definition:
    //   S      = 42   (44 signed pairs, one negative, one zero)
    //   var(S) = [10·9·25 − 2·1·9]/18 = 124 exactly
    //   z      = (S−1)/sqrt(var) = 41/sqrt(124) = 3.6818
    // (The D16 spec text's S=43 / var=124.6667 / z=3.7616 is a hand-
    // arithmetic erratum chain; the formula is the authority — D-160-5.)
    const std::vector<float> y = { 10, 15, 14, 20, 25, 25, 27, 30, 32, 35 };
    const std::vector<double> t = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    const GilbertReference ref( y, t );
    REQUIRE( ref.S == 42 );
    REQUIRE( ref.varS == Approx( 124.0 ).margin( 1e-9 ) );

    const auto r = TrendAnalyzer::computeMannKendall( y, t );
    REQUIRE( r.valid );
    REQUIRE( r.sampleCount == 10 );
    REQUIRE( r.tauVariance == Approx( 124.0 ).margin( 1e-9 ) );
    REQUIRE( r.tau == Approx( 42.0 / 45.0 ).margin( 1e-12 ) );     // tau-a = S/(n(n−1)/2)
    REQUIRE( r.zScore == Approx( 41.0 / std::sqrt( 124.0 ) ).margin( 1e-12 ) ); // 3.6818
    REQUIRE( r.pValue == Approx( std::erfc( r.zScore / std::sqrt( 2.0 ) ) ).margin( 1e-12 ) );
    REQUIRE( r.pValue < 0.001 );
    REQUIRE( r.isSignificant( 0.01 ) );
    REQUIRE( r.senSlope == Approx( senSlopeReference( y, t ) ).margin( 1e-12 ) );
    // Sen slope median for this series: 2.5 units/day (pairwise-slope set median).
    REQUIRE( r.senSlope == Approx( 2.5 ).margin( 1e-9 ) );

    // Underpowered input: fewer than 3 finite samples is invalid, 3 exactly works.
    const std::vector<float> two = { 1, 2 };
    const auto r2 = TrendAnalyzer::computeMannKendall( two, { 0, 1 } );
    REQUIRE( !r2.valid );
    REQUIRE( r2.sampleCount == 2 );
    const auto r3 = TrendAnalyzer::computeMannKendall( { 1, 2, 3 }, { 0, 1, 2 } );
    REQUIRE( r3.valid );
}
