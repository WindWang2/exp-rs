// temporal_gapfill.cpp — see temporal_gapfill.h
#include "temporal_gapfill.h"

#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
}

void gapFillSeries( const float *series, int T, const double *tDays,
                    GapFillMethod method, double maxGapDays,
                    float *out, GapFillCounts *outCounts )
{
    GapFillCounts local;
    GapFillCounts &counts = outCounts ? *outCounts : local;
    counts = GapFillCounts{};

    if ( !series || !out || !tDays || T <= 0 )
        return;

    // Single-pass semantics: anchors are the ORIGINAL observations. When the
    // caller aliases out with the input (the streaming operator writes back
    // in place), a local copy protects the anchor walk from seeing values
    // this call just synthesized — cascading fills would otherwise chain
    // synthetic values that were never observed.
    std::vector<float> original;
    if ( out == series )
    {
        original.assign( series, series + T );
        series = original.data();
    }

    for ( int s = 0; s < T; ++s )
    {
        const float v = series[s];
        if ( std::isfinite( v ) )
        {
            out[s] = v; // valid samples copy through unchanged
            continue;
        }

        // Nearest valid sample on each side of the gap (l < s < r).
        int l = s - 1;
        while ( l >= 0 && !std::isfinite( series[l] ) )
            --l;
        int r = s + 1;
        while ( r < T && !std::isfinite( series[r] ) )
            ++r;
        const bool hasLeft = l >= 0;
        const bool hasRight = r < T;
        if ( !hasLeft && !hasRight )
        {
            out[s] = kNan; // no anchor at all — nothing to interpolate from
            continue;
        }

        ++counts.fillable;
        float filled = kNan;
        if ( method == GapFillMethod::Linear )
        {
            if ( hasLeft && hasRight )
            {
                const double span = tDays[r] - tDays[l];
                if ( span <= maxGapDays )
                {
                    if ( span > 0.0 )
                    {
                        const double weight = ( tDays[s] - tDays[l] ) / span;
                        filled = static_cast<float>( series[l] +
                                                     ( series[r] - series[l] ) * weight );
                    }
                    else
                    {
                        // Duplicate instants (keep_all): identical timestamps —
                        // deterministic average instead of a 0/0 weight.
                        filled = 0.5f * ( series[l] + series[r] );
                    }
                }
            }
            // one-sided gaps stay NaN: linear never extrapolates
        }
        else // Nearest
        {
            const double distLeft =
                hasLeft ? tDays[s] - tDays[l] : std::numeric_limits<double>::infinity();
            const double distRight =
                hasRight ? tDays[r] - tDays[s] : std::numeric_limits<double>::infinity();
            const double dist = std::min( distLeft, distRight );
            if ( dist <= maxGapDays )
                filled = distLeft <= distRight // tie -> earlier scene
                             ? series[l]
                             : series[r];
        }

        out[s] = filled;
        if ( std::isfinite( filled ) )
            ++counts.filled;
    }
}

std::vector<float> gapFillSeries( const std::vector<float> &series,
                                  const std::vector<double> &tDays,
                                  GapFillMethod method, double maxGapDays )
{
    std::vector<float> out( series.size(), kNan );
    gapFillSeries( series.data(), static_cast<int>( series.size() ), tDays.data(),
                   method, maxGapDays, out.data() );
    return out;
}

} // namespace sicnu::temporal
