// temporal/temporal_monitoring.cpp — see temporal_monitoring.h for contracts.

#include "temporal_monitoring.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace sicnu::temporal::monitoring
{

void cusumStep( CusumState *state, double z, double drift, int index )
{
    if ( !state || !std::isfinite( z ) )
        return;
    state->s += z - drift;
    ++state->used;
    if ( const double absS = std::abs( state->s ); absS > state->maxAbs )
    {
        state->maxAbs = absS;
        state->argmax = index;
    }
}

void ewmaStep( EwmaState *state, double zAnomaly, double lambda, int index )
{
    if ( !state || !std::isfinite( zAnomaly ) )
        return;
    state->z = lambda * state->z + ( 1.0 - lambda ) * zAnomaly;
    ++state->used;
    if ( const double absZ = std::abs( state->z ); absZ > state->maxAbs )
    {
        state->maxAbs = absZ;
        state->argmax = index;
    }
}

SeasonalMkResult seasonalMannKendall( const double *times, const double *values,
                                      const int *seasons, const std::uint8_t *valid,
                                      int count )
{
    SeasonalMkResult out;
    if ( !times || !values || !seasons || !valid || count <= 0 )
        return out;

    // Per-season series: (time, value) pairs, valid observations only.
    std::map<int, std::vector<std::pair<double, double>>> bySeason;
    for ( int i = 0; i < count; ++i )
    {
        if ( !valid[i] || !std::isfinite( values[i] ) || !std::isfinite( times[i] ) )
            continue;
        bySeason[seasons[i]].emplace_back( times[i], values[i] );
    }

    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    double pairsTotal = 0.0;
    for ( auto &[season, series] : bySeason )
    {
        const int n = static_cast<int>( series.size() );
        if ( n < 2 )
            continue;
        ++out.seasonsUsed;

        // Chronological ordering (ties in time are handled by the score rule
        // below: equal times contribute 0 to S, like equal values).
        std::sort( series.begin(), series.end() );

        double s = 0.0;
        std::map<double, int> valueTies;
        std::map<double, int> timeTies;
        for ( int a = 0; a < n; ++a )
        {
            ++valueTies[series[a].second];
            ++timeTies[series[a].first];
            for ( int b = a + 1; b < n; ++b )
            {
                const double dt = series[b].first - series[a].first;
                if ( dt == 0.0 )
                    continue; // tied times are not comparable pairs
                ++pairsTotal;
                const double dv = series[b].second - series[a].second;
                if ( dv == 0.0 )
                    continue; // tied value: no score
                s += dv > 0.0 ? 1.0 : -1.0;
            }
        }
        out.s += s;

        // Var(S) with tie corrections at each term's multiplicity.
        double var = static_cast<double>( n ) * ( n - 1 ) * ( 2 * n + 5 ) / 18.0;
        for ( const auto &[value, t] : valueTies )
            if ( t > 1 )
                var -= static_cast<double>( t ) * ( t - 1 ) * ( 2 * t + 5 ) / 18.0;
        for ( const auto &[time, t] : timeTies )
            if ( t > 1 )
                var -= static_cast<double>( t ) * ( t - 1 ) * ( 2 * t + 5 ) / 18.0;
        out.variance += std::max( 0.0, var );
    }

    if ( out.seasonsUsed == 0 || pairsTotal <= 0.0 )
    {
        out.z = kNaN;
        out.tau = kNaN;
        return out;
    }

    if ( out.variance > 0.0 )
        out.z = ( out.s - ( out.s > 0.0 ? 1.0 : out.s < 0.0 ? -1.0 : 0.0 ) ) /
                std::sqrt( out.variance );
    else
        out.z = kNaN; // all seasons' values constant: no discriminating pairs
    out.tau = out.s / pairsTotal;
    return out;
}

} // namespace sicnu::temporal::monitoring
