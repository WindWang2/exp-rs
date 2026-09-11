// sar_temporal.cpp — see sar_temporal.h
#include "sar_temporal.h"

#include "sar_metadata.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sicnu::sar
{

bool sarTemporalStats( const double *values, int n, SarTemporalStats *out,
                       double changeThresholdDb )
{
    if ( values == nullptr || out == nullptr || n <= 0 || !( changeThresholdDb >= 0.0 ) )
        return false;
    *out = SarTemporalStats{};

    // Pass 1: valid samples + linear aggregates (median needs storage).
    std::vector<double> valid;
    valid.reserve( static_cast<size_t>( n ) );
    double sum = 0.0;
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for ( int i = 0; i < n; ++i )
    {
        const double v = values[i];
        if ( !std::isfinite( v ) || !( v > 0.0 ) )
            continue;
        valid.push_back( v );
        sum += v;
        if ( v < minV )
        {
            minV = v;
            out->argminDate = i;
        }
        if ( v > maxV )
        {
            maxV = v;
            out->argmaxDate = i;
        }
    }
    out->validCount = static_cast<int>( valid.size() );
    if ( valid.empty() )
        return false;

    const double count = valid.size();
    out->meanLinear = sum / count;
    out->minLinear = minV;
    out->maxLinear = maxV;
    out->meanDb = linearToDb( out->meanLinear );

    // Population stddev over the valid samples.
    double sq = 0.0;
    for ( double v : valid )
        sq += ( v - out->meanLinear ) * ( v - out->meanLinear );
    out->stdDevLinear = std::sqrt( sq / count );
    out->cv = out->stdDevLinear / out->meanLinear;

    // Robust log-domain change vs the median linear baseline.
    const size_t mid = valid.size() / 2;
    std::nth_element( valid.begin(), valid.begin() + mid, valid.end() );
    const double medianLinear = valid[mid];
    out->baselineDb = linearToDb( medianLinear );

    // Pass 2 over the valid samples: log deviations. (Repeated from the
    // stored valid samples — order-independent reductions, bit-exact grade
    // is preserved because the accumulation order is the sample order.)
    double maxDev = 0.0;
    double devSum = 0.0;
    for ( double v : valid )
    {
        const double dev = std::fabs( linearToDb( v ) - out->baselineDb );
        maxDev = std::max( maxDev, dev );
        devSum += dev;
        if ( dev >= changeThresholdDb )
            ++out->changedDates;
    }
    out->maxLogDeviationDb = maxDev;
    out->meanLogDeviationDb = devSum / count;
    return true;
}

} // namespace sicnu::sar
