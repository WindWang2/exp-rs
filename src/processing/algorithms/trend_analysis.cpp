/***************************************************************************
  processing/algorithms/trend_analysis.cpp
  Temporal Phenology Timeline Studio (D16) — trend analysis kernels.
  ---------------------------
  See trend_analysis.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "processing/algorithms/trend_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::temporal
{

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Shared core: finite, time-sorted pairs in, MannKendallResult out.
MannKendallResult mannKendallImpl( const std::vector<double> &ts,
                                   const std::vector<float> &ys )
{
    MannKendallResult r;
    const std::size_t n = ts.size();
    r.sampleCount = static_cast<int>( n );
    if ( n < 3 )
    {
        // Documented contract: an underpowered test is NaN-filled, never 0.
        r.senSlope = std::numeric_limits<double>::quiet_NaN();
        r.intercept = std::numeric_limits<double>::quiet_NaN();
        r.tau = std::numeric_limits<double>::quiet_NaN();
        r.zScore = std::numeric_limits<double>::quiet_NaN();
        r.pValue = std::numeric_limits<double>::quiet_NaN();
        r.tauVariance = std::numeric_limits<double>::quiet_NaN();
        return r;
    }

    // S over strictly time-ordered pairs (ties contribute 0).
    long S = 0;
    for ( std::size_t i = 0; i < n; ++i )
        for ( std::size_t j = i + 1; j < n; ++j )
        {
            if ( !( ts[j] > ts[i] ) )
                continue;
            if ( ys[j] > ys[i] )
                ++S;
            else if ( ys[j] < ys[i] )
                --S;
        }

    // Tie-corrected variance (Gilbert 1987 eq. 16.5 style).
    std::vector<float> sorted( ys );
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
    const double varS = ( nn * ( nn - 1.0 ) * ( 2.0 * nn + 5.0 ) - tieTerm ) / 18.0;
    r.tauVariance = varS;
    r.tau = S / ( 0.5 * nn * ( nn - 1.0 ) );

    // Continuity-corrected z and two-sided analytic p (erfc form).
    if ( varS > 0.0 )
    {
        if ( S > 0 )
            r.zScore = ( static_cast<double>( S ) - 1.0 ) / std::sqrt( varS );
        else if ( S < 0 )
            r.zScore = ( static_cast<double>( S ) + 1.0 ) / std::sqrt( varS );
        else
            r.zScore = 0.0;
        r.pValue = std::erfc( std::abs( r.zScore ) / std::sqrt( 2.0 ) );
    }
    else
    {
        r.zScore = 0.0;
        r.pValue = 1.0;
    }

    // Theil-Sen: median of pairwise slopes over strictly ordered times.
    std::vector<double> slopes;
    slopes.reserve( n * ( n - 1 ) / 2 );
    for ( std::size_t i = 0; i < n; ++i )
        for ( std::size_t j = i + 1; j < n; ++j )
            if ( ts[j] > ts[i] )
                slopes.push_back( ( static_cast<double>( ys[j] ) - ys[i] ) /
                                  ( ts[j] - ts[i] ) );
    if ( slopes.empty() )
    {
        r.senSlope = std::numeric_limits<double>::quiet_NaN();
        r.intercept = std::numeric_limits<double>::quiet_NaN();
        r.valid = false;
        return r; // no time-ordered pair: slope undefined
    }
    std::sort( slopes.begin(), slopes.end() );
    const std::size_t m = slopes.size();
    r.senSlope = m % 2 == 1 ? slopes[m / 2] : 0.5 * ( slopes[m / 2 - 1] + slopes[m / 2] );

    // Intercept: median of (y_i − slope·t_i).
    std::vector<double> intercepts( n );
    for ( std::size_t i = 0; i < n; ++i )
        intercepts[i] = static_cast<double>( ys[i] ) - r.senSlope * ts[i];
    std::sort( intercepts.begin(), intercepts.end() );
    r.intercept = n % 2 == 1 ? intercepts[n / 2]
                             : 0.5 * ( intercepts[n / 2 - 1] + intercepts[n / 2] );

    r.valid = true;
    return r;
}

} // namespace

MannKendallResult TrendAnalyzer::computeMannKendall( const std::vector<float> &y,
                                                     const std::vector<double> &tDays )
{
    if ( y.size() != tDays.size() )
        return {};
    std::vector<std::size_t> idx;
    idx.reserve( y.size() );
    for ( std::size_t i = 0; i < y.size(); ++i )
        if ( std::isfinite( y[i] ) && std::isfinite( tDays[i] ) )
            idx.push_back( i );
    std::sort( idx.begin(), idx.end(),
               [&]( std::size_t a, std::size_t b ) { return tDays[a] < tDays[b]; } );
    std::vector<double> ts( idx.size() );
    std::vector<float> ys( idx.size() );
    for ( std::size_t i = 0; i < idx.size(); ++i )
    {
        ts[i] = tDays[idx[i]];
        ys[i] = y[idx[i]];
    }
    return mannKendallImpl( ts, ys );
}

void TrendAnalyzer::computeRasterTrend( const float *inSeries, int width, int height,
                                        int timeSteps, const double *tDays, float *outSlope,
                                        float *outPValue, float *outZScore )
{
    const std::size_t pixels = static_cast<std::size_t>( width ) * height;
    if ( !inSeries || !tDays || !outSlope || !outPValue || !outZScore || pixels == 0 ||
         timeSteps < 1 )
        return;

    // Sort the (usually regular) time axis once; NaN time steps excluded.
    std::vector<std::size_t> timeOrder;
    timeOrder.reserve( timeSteps );
    for ( int k = 0; k < timeSteps; ++k )
        if ( std::isfinite( tDays[k] ) )
            timeOrder.push_back( static_cast<std::size_t>( k ) );
    std::sort( timeOrder.begin(), timeOrder.end(),
               [&]( std::size_t a, std::size_t b ) { return tDays[a] < tDays[b]; } );

    // The time axis is shared by all pixels and compacted per-pixel values
    // land in reused buffers; the O(n²) kernel internals still allocate
    // their scratch per pixel (documented, deterministic — a full arena
    // pass is a follow-up optimization, not a correctness item).
    std::vector<double> ts( timeOrder.size() );
    std::vector<float> ys( timeOrder.size() );
    for ( std::size_t i = 0; i < timeOrder.size(); ++i )
        ts[i] = tDays[timeOrder[i]];

    for ( std::size_t px = 0; px < pixels; ++px )
    {
        std::size_t used = 0;
        for ( std::size_t k = 0; k < timeOrder.size(); ++k )
        {
            const float value = inSeries[timeOrder[k] * pixels + px];
            if ( std::isfinite( value ) )
                ys[used++] = value;
        }
        MannKendallResult r;
        if ( used >= 3 )
        {
            if ( used == timeOrder.size() )
                r = mannKendallImpl( ts, ys );
            else
                r = mannKendallImpl( std::vector<double>( ts.begin(), ts.begin() + used ),
                                     std::vector<float>( ys.begin(), ys.begin() + used ) );
        }
        outSlope[px] = r.valid ? static_cast<float>( r.senSlope ) : kNan;
        outPValue[px] = r.valid ? static_cast<float>( r.pValue ) : kNan;
        outZScore[px] = r.valid ? static_cast<float>( r.zScore ) : kNan;
    }
}

} // namespace sicnu::temporal
