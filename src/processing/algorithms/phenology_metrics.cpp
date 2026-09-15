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

/// σ(x) = 1/(1+exp(−x)); overflow-safe for |x| beyond ±700.
double sigmoid( double x )
{
    if ( x >= 0.0 )
    {
        const double e = std::exp( -x );
        return 1.0 / ( 1.0 + e );
    }
    const double e = std::exp( x );
    return e / ( 1.0 + e );
}

/// The documented asymmetric double logistic (see phenology_metrics.h):
/// f = base + amp·[σ(k1(t−τ1)) + σ(−k2(t−τ2)) − 1].
double doubleLogisticModel( const DoubleLogisticParams &p, double t )
{
    return p.baseVal +
           p.amplitude *
               ( sigmoid( p.sosRate * ( t - p.sosInflection ) ) -
                 sigmoid( p.eosRate * ( t - p.eosInflection ) ) );
}

void paramsToArray( const DoubleLogisticParams &p, double *v )
{
    v[0] = p.baseVal;
    v[1] = p.amplitude;
    v[2] = p.sosInflection;
    v[3] = p.sosRate;
    v[4] = p.eosInflection;
    v[5] = p.eosRate;
}

DoubleLogisticParams paramsFromArray( const double *v )
{
    DoubleLogisticParams p;
    p.baseVal = v[0];
    p.amplitude = v[1];
    p.sosInflection = v[2];
    p.sosRate = v[3];
    p.eosInflection = v[4];
    p.eosRate = v[5];
    return p;
}

double modelCost( const double *v, const std::vector<double> &t, const std::vector<float> &y )
{
    DoubleLogisticParams p = paramsFromArray( v );
    double s = 0.0;
    for ( std::size_t i = 0; i < t.size(); ++i )
    {
        const double r = doubleLogisticModel( p, t[i] ) - y[i];
        s += r * r;
    }
    return s;
}

/// Solves the symmetric 6×6 system (a) x = b by Gaussian elimination with
/// partial pivoting; returns false on a singular system.
bool solveDense6( std::vector<double> &a, std::vector<double> &b )
{
    constexpr int n = 6;
    for ( int col = 0; col < n; ++col )
    {
        int pivot = col;
        for ( int r = col + 1; r < n; ++r )
            if ( std::abs( a[r * n + col] ) > std::abs( a[pivot * n + col] ) )
                pivot = r;
        if ( !( std::abs( a[pivot * n + col] ) > kTiny ) )
            return false;
        if ( pivot != col )
        {
            for ( int c = 0; c < n; ++c )
                std::swap( a[pivot * n + c], a[col * n + c] );
            std::swap( b[pivot], b[col] );
        }
        for ( int r = col + 1; r < n; ++r )
        {
            const double f = a[r * n + col] / a[col * n + col];
            for ( int c = col; c < n; ++c )
                a[r * n + c] -= f * a[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for ( int r = n; r-- > 0; )
    {
        double v = b[r];
        for ( int c = r + 1; c < n; ++c )
            v -= a[r * n + c] * b[c];
        b[r] = v / a[r * n + r];
    }
    return true;
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
    // On the absolute tDays axis sos < eos is enforced above, so the length
    // of season is the plain difference. (The +365 wrap is a property of the
    // day-of-year VIEW, not of this axis.)
    m.los = eos - sos;
    m.baseVal = zMin;
    m.peakVal = zMax;

    // Trapezoid integral over [sos, eos]: the boundary values are known
    // exactly (ratio == thresholdFraction at both crossings), so the partial
    // end segments are included at full accuracy.
    const double boundaryVal = zMin + thresholdFraction * ( zMax - zMin );
    double integral = 0.0;
    double leftT = sos;
    double leftV = boundaryVal;
    for ( std::size_t i : idx )
    {
        if ( tDays[i] < sos || tDays[i] > eos )
            continue;
        integral += 0.5 * ( static_cast<double>( y[i] ) + leftV ) * ( tDays[i] - leftT );
        leftT = tDays[i];
        leftV = y[i];
    }
    if ( leftT < eos )
        integral += 0.5 * ( boundaryVal + leftV ) * ( eos - leftT );
    m.integral = integral;
    m.valid = m.sos < m.pos && m.pos < m.eos;
    return m;
}

std::pair<DoubleLogisticParams, PhenologyMetrics>
PhenologyExtractor::fitDoubleLogistic( const std::vector<float> &y,
                                       const std::vector<double> &tDays )
{
    std::pair<DoubleLogisticParams, PhenologyMetrics> result;
    auto &params = result.first;
    auto &metrics = result.second;

    if ( y.size() != tDays.size() )
        return result;

    // Finite pairs, ascending time.
    std::vector<double> t;
    std::vector<float> v;
    for ( std::size_t i = 0; i < y.size(); ++i )
        if ( std::isfinite( y[i] ) && std::isfinite( tDays[i] ) )
        {
            t.push_back( tDays[i] );
            v.push_back( y[i] );
        }
    if ( t.size() < 8 )
        return result;
    std::vector<std::size_t> order( t.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
        order[i] = i;
    std::sort( order.begin(), order.end(),
               [&]( std::size_t a, std::size_t b ) { return t[a] < t[b]; } );
    std::vector<double> ts( t.size() );
    std::vector<float> ys( t.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
    {
        ts[i] = t[order[i]];
        ys[i] = v[order[i]];
    }

    // Initialize from the mid-level threshold crossings (τ sits exactly on
    // the 0.5 rising/falling crossings when the opposite flank is flat).
    DoubleLogisticParams init;
    const PhenologyMetrics mid = extractDynamicThreshold( y, tDays, 0.5, 1, 365 );
    double tMin = ts.front();
    double tMax = ts.back();
    double yMin = ys.front();
    double yMax = ys.front();
    for ( std::size_t i = 0; i < ys.size(); ++i )
    {
        tMin = std::min( tMin, ts[i] );
        tMax = std::max( tMax, ts[i] );
        yMin = std::min( yMin, static_cast<double>( ys[i] ) );
        yMax = std::max( yMax, static_cast<double>( ys[i] ) );
    }
    if ( mid.valid )
    {
        init.baseVal = mid.baseVal;
        init.amplitude = std::max( mid.peakVal - mid.baseVal, 1e-3 );
        init.sosInflection = mid.sos;
        init.eosInflection = mid.eos;
        init.sosRate = 6.0 / std::max( mid.pos - mid.sos, 1.0 );
        init.eosRate = 6.0 / std::max( mid.eos - mid.pos, 1.0 );
    }
    else
    {
        init.baseVal = yMin;
        init.amplitude = std::max( yMax - yMin, 1e-3 );
        init.sosInflection = tMin + 0.25 * ( tMax - tMin );
        init.eosInflection = tMin + 0.75 * ( tMax - tMin );
        init.sosRate = 0.1;
        init.eosRate = 0.1;
    }

    // Levenberg–Marquardt with a central-difference Jacobian.
    double p[6];
    paramsToArray( init, p );
    double cost = modelCost( p, ts, ys );
    double mu = 1e-3;
    constexpr int kMaxIters = 200;
    for ( int iter = 0; iter < kMaxIters; ++iter )
    {
        // Jacobian and gradient at the current point.
        double jtJ[36];
        double jtR[6];
        for ( int r = 0; r < 36; ++r )
            jtJ[r] = 0.0;
        for ( int c = 0; c < 6; ++c )
            jtR[c] = 0.0;
        for ( std::size_t i = 0; i < ts.size(); ++i )
        {
            double jac[6];
            for ( int c = 0; c < 6; ++c )
            {
                const double step = std::max( 1e-6, 1e-5 * std::abs( p[c] ) );
                double pp[6], pm[6];
                for ( int k = 0; k < 6; ++k )
                {
                    pp[k] = p[k];
                    pm[k] = p[k];
                }
                pp[c] += step;
                pm[c] -= step;
                jac[c] = ( doubleLogisticModel( paramsFromArray( pp ), ts[i] ) -
                           doubleLogisticModel( paramsFromArray( pm ), ts[i] ) ) /
                         ( 2.0 * step );
            }
            DoubleLogisticParams cur = paramsFromArray( p );
            const double residual = doubleLogisticModel( cur, ts[i] ) - ys[i];
            for ( int r = 0; r < 6; ++r )
            {
                jtR[r] += jac[r] * residual;
                for ( int c = 0; c < 6; ++c )
                    jtJ[r * 6 + c] += jac[r] * jac[c];
            }
        }

        bool improved = false;
        for ( int retry = 0; retry < 12 && !improved; ++retry )
        {
            std::vector<double> a( 36 );
            std::vector<double> b( 6 );
            for ( int r = 0; r < 36; ++r )
                a[r] = jtJ[r];
            for ( int c = 0; c < 6; ++c )
            {
                a[c * 6 + c] += mu * ( jtJ[c * 6 + c] > kTiny ? jtJ[c * 6 + c] : 1.0 );
                b[c] = -jtR[c];
            }
            if ( !solveDense6( a, b ) )
            {
                mu *= 10.0;
                continue;
            }
            double trial[6];
            for ( int c = 0; c < 6; ++c )
                trial[c] = p[c] + b[c];
            const double trialCost = modelCost( trial, ts, ys );
            if ( std::isfinite( trialCost ) && trialCost < cost )
            {
                const double improvement = cost - trialCost;
                std::copy( trial, trial + 6, p );
                cost = trialCost;
                mu = std::max( mu / 3.0, 1e-12 );
                improved = true;
                if ( improvement <= 1e-12 * std::max( cost, 1.0 ) )
                    iter = kMaxIters; // converged
            }
            else
            {
                mu *= 5.0;
            }
        }
        if ( !improved )
            break;
    }

    params = paramsFromArray( p );

    // Phenology from the curvature extremes: sos = τ1, eos = τ2, pos =
    // argmax of the fitted curve on a fine deterministic grid.
    metrics.sos = params.sosInflection;
    metrics.eos = params.eosInflection;
    const int fine = 2000;
    double bestT = tMin;
    double bestV = -std::numeric_limits<double>::infinity();
    for ( int i = 0; i <= fine; ++i )
    {
        const double tt = tMin + ( tMax - tMin ) * static_cast<double>( i ) / fine;
        const double val = doubleLogisticModel( params, tt );
        if ( val > bestV )
        {
            bestV = val;
            bestT = tt;
        }
    }
    metrics.pos = bestT;
    metrics.baseVal = params.baseVal;
    metrics.peakVal = bestV;
    metrics.los = metrics.eos - metrics.sos; // absolute axis: sos < eos enforced

    // Trapezoid integral of the fitted curve over [sos, eos] with exact
    // boundary values.
    double integral = 0.0;
    double leftT = metrics.sos;
    double leftV = doubleLogisticModel( params, metrics.sos );
    for ( std::size_t i = 0; i < ts.size(); ++i )
    {
        if ( ts[i] < metrics.sos || ts[i] > metrics.eos )
            continue;
        const double yi = doubleLogisticModel( params, ts[i] );
        integral += 0.5 * ( yi + leftV ) * ( ts[i] - leftT );
        leftT = ts[i];
        leftV = yi;
    }
    if ( leftT < metrics.eos )
    {
        const double yi = doubleLogisticModel( params, metrics.eos );
        integral += 0.5 * ( yi + leftV ) * ( metrics.eos - leftT );
    }
    metrics.integral = integral;
    metrics.valid = std::isfinite( cost ) && metrics.sos < metrics.pos && metrics.pos < metrics.eos;
    return result;
}

std::vector<PhenologyMetrics> PhenologyExtractor::extractMultiCycle(
    const std::vector<float> &y, const std::vector<double> &tDays, int cycles,
    double thresholdFraction )
{
    if ( cycles < 1 || y.size() != tDays.size() )
        return {};

    // Finite pairs, ascending time.
    std::vector<double> ts;
    std::vector<float> ys;
    for ( std::size_t i = 0; i < y.size(); ++i )
        if ( std::isfinite( y[i] ) && std::isfinite( tDays[i] ) )
        {
            ts.push_back( tDays[i] );
            ys.push_back( y[i] );
        }
    std::vector<std::size_t> order( ts.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
        order[i] = i;
    std::sort( order.begin(), order.end(),
               [&]( std::size_t a, std::size_t b ) { return ts[a] < ts[b]; } );
    std::vector<double> tSorted( ts.size() );
    std::vector<float> ySorted( ts.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
    {
        tSorted[i] = ts[order[i]];
        ySorted[i] = ys[order[i]];
    }
    const std::size_t n = tSorted.size();
    if ( n < 3 )
        return {};

    // Derivative zero structure: local maxima (rising→falling) and local
    // minima (falling→rising). First-wins on plateaus (>= left, > right).
    auto isMax = [&]( std::size_t i ) {
        return i > 0 && i + 1 < n && ySorted[i] >= ySorted[i - 1] && ySorted[i] > ySorted[i + 1];
    };
    auto isMin = [&]( std::size_t i ) {
        return i > 0 && i + 1 < n && ySorted[i] <= ySorted[i - 1] && ySorted[i] < ySorted[i + 1];
    };
    std::vector<std::size_t> peaks;
    std::vector<std::size_t> valleys;
    for ( std::size_t i = 1; i + 1 < n; ++i )
    {
        if ( isMax( i ) )
            peaks.push_back( i );
        if ( isMin( i ) )
            valleys.push_back( i );
    }
    if ( peaks.empty() )
        return {}; // no season structure: refuse, never invent cycles

    // Collapse same-structure duplicates: two maxima with no valley between
    // them are one season (float plateaus at the tails) — keep the higher.
    {
        std::vector<std::size_t> realPeaks;
        auto hasValleyBetween = [&]( std::size_t a, std::size_t b ) {
            for ( std::size_t v : valleys )
                if ( v > a && v < b )
                    return true;
            return false;
        };
        for ( std::size_t peak : peaks )
        {
            if ( !realPeaks.empty() && !hasValleyBetween( realPeaks.back(), peak ) )
            {
                if ( ySorted[peak] > ySorted[realPeaks.back()] )
                    realPeaks.back() = peak;
                continue;
            }
            realPeaks.push_back( peak );
        }
        peaks = std::move( realPeaks );
    }

    // The @a cycles most prominent peaks (value desc, earliest wins ties).
    std::sort( peaks.begin(), peaks.end(), [&]( std::size_t a, std::size_t b ) {
        if ( ySorted[a] != ySorted[b] )
            return ySorted[a] > ySorted[b];
        return a < b;
    } );
    if ( peaks.size() > static_cast<std::size_t>( cycles ) )
        peaks.resize( static_cast<std::size_t>( cycles ) );
    std::sort( peaks.begin(), peaks.end() ); // back onto the time axis

    std::vector<PhenologyMetrics> results;
    results.reserve( peaks.size() );
    for ( std::size_t p = 0; p < peaks.size(); ++p )
    {
        // Segment: from the nearest valley left of the peak (or the start)
        // to the nearest valley right of it (or the end). Neighbor selected
        // peaks bound the search so segments never overlap.
        const std::size_t leftBound =
            p == 0 ? 0 : peaks[p - 1];
        const std::size_t rightBound =
            p + 1 == peaks.size() ? n - 1 : peaks[p + 1];
        std::size_t segBegin = 0;
        for ( std::size_t i = leftBound + 1; i < peaks[p]; ++i )
            if ( isMin( i ) )
                segBegin = i;
        std::size_t segEnd = n - 1;
        for ( std::size_t i = peaks[p] + 1; i < rightBound; ++i )
            if ( isMin( i ) )
            {
                segEnd = i;
                break; // first valley after the peak delimits this season
            }

        std::vector<float> segY( ySorted.begin() + segBegin, ySorted.begin() + segEnd + 1 );
        std::vector<double> segT( tSorted.begin() + segBegin, tSorted.begin() + segEnd + 1 );
        PhenologyMetrics m = extractDynamicThreshold( segY, segT, thresholdFraction, 1, 365 );
        if ( m.valid )
            results.push_back( m );
    }
    return results;
}

} // namespace sicnu::temporal
