/***************************************************************************
  processing/algorithms/phenology_metrics.cpp
  Temporal Phenology Timeline Studio (D16) — phenology extraction kernels.
  ---------------------------
  See phenology_metrics.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "processing/algorithms/phenology_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{

constexpr double kTiny = 1e-12;
constexpr double kYearDays = 365.0;

/// Day-of-year view of a tDays value: doy(0) = 1 (epoch-aligned seasons).
double doyOf( double t )
{
    double d = std::fmod( t, kYearDays );
    if ( d < 0.0 )
        d += kYearDays;
    return d + 1.0;
}

bool inSeason( double doy, int startDoy, int endDoy )
{
    return startDoy <= endDoy ? ( doy >= startDoy && doy <= endDoy )
                              : ( doy >= startDoy || doy <= endDoy );
}

/// Linear interpolation of the crossing of @a level between samples
/// (t0, v0) and (t1, v1) with (v0 − level) and (v1 − level) of opposite sign.
double crossingTime( double t0, double v0, double t1, double v1, double level )
{
    const double span = v1 - v0;
    if ( std::abs( span ) < kTiny )
        return t0;
    return t0 + ( level - v0 ) / span * ( t1 - t0 );
}

} // namespace

PhenologyMetrics PhenologyExtractor::extractDynamicThreshold( const std::vector<float> &y,
                                                              const std::vector<double> &tDays,
                                                              double thresholdFraction,
                                                              int seasonStartDoy,
                                                              int seasonEndDoy )
{
    PhenologyMetrics invalid;
    if ( y.size() != tDays.size() || y.empty() )
        return invalid;
    if ( !( thresholdFraction > 0.0 && thresholdFraction <= 1.0 ) )
        return invalid;

    // Season-window finite samples, ascending time (input order is the
    // operator contract; sort a copy to be robust to caller ordering).
    std::vector<std::size_t> idx;
    idx.reserve( y.size() );
    for ( std::size_t i = 0; i < y.size(); ++i )
        if ( std::isfinite( y[i] ) && std::isfinite( tDays[i] ) &&
             inSeason( doyOf( tDays[i] ), seasonStartDoy, seasonEndDoy ) )
            idx.push_back( i );
    std::sort( idx.begin(), idx.end(),
               [&]( std::size_t a, std::size_t b ) { return tDays[a] < tDays[b]; } );
    if ( idx.size() < 3 )
        return invalid;

    double zMin = std::numeric_limits<double>::infinity();
    double zMax = -std::numeric_limits<double>::infinity();
    std::size_t posIdx = idx.front();
    for ( std::size_t i : idx )
    {
        const double v = y[i];
        if ( v < zMin )
            zMin = v;
        if ( v > zMax )
        {
            zMax = v;
            posIdx = i; // first maximum wins (deterministic ties)
        }
    }
    if ( zMax - zMin < kTiny )
        return invalid; // flat window, no season

    const auto ratio = [&]( std::size_t i ) {
        return ( static_cast<double>( y[i] ) - zMin ) / ( zMax - zMin );
    };
    // SOS: first rising crossing of the threshold before the peak.
    double sos = -1.0;
    {
        bool crossed = false;
        std::size_t prev = idx.front();
        for ( std::size_t i : idx )
        {
            if ( tDays[i] > tDays[posIdx] )
                break;
            if ( crossed )
                break;
            if ( i != prev && ratio( i ) >= thresholdFraction && ratio( prev ) < thresholdFraction )
            {
                sos = crossingTime( tDays[prev], ratio( prev ), tDays[i], ratio( i ),
                                    thresholdFraction );
                crossed = true;
                break;
            }
            prev = i;
        }
        if ( !crossed )
            return invalid; // no rising limb inside the window
    }

    // EOS: first falling crossing of the threshold after the peak.
    double eos = -1.0;
    {
        bool crossed = false;
        std::size_t prev = posIdx;
        bool havePrev = false;
        for ( std::size_t i : idx )
        {
            if ( tDays[i] < tDays[posIdx] )
                continue;
            if ( !havePrev )
            {
                prev = i;
                havePrev = true;
                continue;
            }
            if ( ratio( i ) < thresholdFraction && ratio( prev ) >= thresholdFraction )
            {
                eos = crossingTime( tDays[prev], ratio( prev ), tDays[i], ratio( i ),
                                    thresholdFraction );
                crossed = true;
                break;
            }
            prev = i;
        }
        if ( !crossed )
            return invalid; // no falling limb: EOS undefined (biology guard)
    }
    if ( !( eos > sos ) )
        return invalid;

    PhenologyMetrics m;
    m.sos = sos;
    m.pos = tDays[posIdx];
    m.eos = eos;
    // Year-wrapping seasons (winter wheat, southern hemisphere) gain a year.
    m.los = ( doyOf( eos ) >= doyOf( sos ) ) ? ( eos - sos ) : ( eos + kYearDays - sos );
    m.baseVal = zMin;
    m.peakVal = zMax;

    // Trapezoid integral over [sos, eos]: the boundary values are known
    // exactly (ratio == thresholdFraction at both crossings), so the partial
    // end segments are included at full accuracy.
    const double boundaryVal = zMin + thresholdFraction * ( zMax - zMin );
    double integral = 0.0;
    double leftT = sos;
    double leftV = boundaryVal;
    bool haveLeft = true;
    for ( std::size_t i : idx )
    {
        if ( tDays[i] < sos || tDays[i] > eos )
            continue;
        integral += 0.5 * ( static_cast<double>( y[i] ) + leftV ) * ( tDays[i] - leftT );
        leftT = tDays[i];
        leftV = y[i];
        haveLeft = true;
    }
    if ( haveLeft && leftT < eos )
        integral += 0.5 * ( boundaryVal + leftV ) * ( eos - leftT );
    m.integral = integral;
    m.valid = m.sos < m.pos && m.pos < m.eos;
    return m;
}

std::pair<DoubleLogisticParams, PhenologyMetrics>
PhenologyExtractor::fitDoubleLogistic( const std::vector<float> &, const std::vector<double> & )
{
    return { DoubleLogisticParams{}, PhenologyMetrics{} };
}

std::vector<PhenologyMetrics> PhenologyExtractor::extractMultiCycle( const std::vector<float> &,
                                                                     const std::vector<double> &,
                                                                     int, double )
{
    return {};
}

} // namespace sicnu::temporal
