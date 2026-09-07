// sar_dualpol.cpp — see sar_dualpol.h for the feature contracts.

#include "sar_dualpol.h"

#include <cmath>
#include <limits>
#include <string>

namespace sicnu::sar
{

const char *dualPolFeatureToString( DualPolFeature feature )
{
    switch ( feature )
    {
        case DualPolFeature::Ratio: return "ratio";
        case DualPolFeature::NormalizedDifference: return "normalized_difference";
        case DualPolFeature::LogRatio: return "log_ratio";
        case DualPolFeature::Rvi: return "rvi";
        case DualPolFeature::Span: return "span";
    }
    return "ratio";
}

bool parseDualPolFeature( const char *token, DualPolFeature *out )
{
    if ( !token || !out )
        return false;
    const std::string t( token );
    if ( t == "ratio" ) { *out = DualPolFeature::Ratio; return true; }
    if ( t == "normalized_difference" ) { *out = DualPolFeature::NormalizedDifference; return true; }
    if ( t == "log_ratio" ) { *out = DualPolFeature::LogRatio; return true; }
    if ( t == "rvi" ) { *out = DualPolFeature::Rvi; return true; }
    if ( t == "span" ) { *out = DualPolFeature::Span; return true; }
    return false;
}

double dualPolFeature( DualPolFeature feature, double vv, double vh )
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    // Linear-power domain: negative inputs are invalid (not clamped); exactly
    // zero VH kills ratio/log_ratio (division), zero total power kills the
    // sum-based features.
    if ( vv < 0.0 || vh < 0.0 || !std::isfinite( vv ) || !std::isfinite( vh ) )
        return kNaN;

    switch ( feature )
    {
        case DualPolFeature::Ratio:
            return ( vh > 0.0 ) ? vv / vh : kNaN;
        case DualPolFeature::NormalizedDifference:
        {
            const double denom = vv + vh;
            return ( denom > 0.0 ) ? ( vv - vh ) / denom : kNaN;
        }
        case DualPolFeature::LogRatio:
            return ( vh > 0.0 && vv > 0.0 ) ? 10.0 * std::log10( vv / vh ) : kNaN;
        case DualPolFeature::Rvi:
        {
            const double denom = vv + vh;
            return ( denom > 0.0 ) ? 4.0 * vv / denom : kNaN;
        }
        case DualPolFeature::Span:
            return vv + vh;
    }
    return kNaN;
}

} // namespace sicnu::sar
