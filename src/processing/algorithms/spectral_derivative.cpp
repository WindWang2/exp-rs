// spectral_derivative.cpp — see spectral_derivative.h for contracts.

#include "spectral_derivative.h"

#include <cmath>
#include <limits>
#include <vector>

namespace SpectralDerivative
{

void firstDerivative( const float *values, const double *wavelengths, int bands, float *out )
{
    if ( !values || !wavelengths || !out || bands < 2 )
        return;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for ( int i = 0; i < bands - 1; ++i )
    {
        const double dl = wavelengths[i + 1] - wavelengths[i];
        if ( !std::isfinite( dl ) || dl <= 0.0 )
        {
            out[i] = nan;
            continue;
        }
        const float a = values[i];
        const float b = values[i + 1];
        out[i] = ( !std::isfinite( a ) || !std::isfinite( b ) )
                   ? nan
                   : static_cast<float>( ( b - a ) / dl );
    }
}

void midpointAxis( const double *wavelengths, int bands, double *outMidpoints )
{
    if ( !wavelengths || !outMidpoints || bands < 2 )
        return;
    for ( int i = 0; i < bands - 1; ++i )
        outMidpoints[i] = 0.5 * ( wavelengths[i] + wavelengths[i + 1] );
}

void secondDerivative( const float *values, const double *wavelengths, int bands, float *out )
{
    if ( !values || !wavelengths || !out || bands < 3 )
        return;
    // First pass into scratch (stack buffer via vector-free double pass):
    // reuse the caller-visible contract by computing into a temporary axis.
    static thread_local std::vector<float> scratch;
    static thread_local std::vector<double> midAxis;
    scratch.resize( static_cast<size_t>( bands - 1 ) );
    midAxis.resize( static_cast<size_t>( bands - 1 ) );
    firstDerivative( values, wavelengths, bands, scratch.data() );
    midpointAxis( wavelengths, bands, midAxis.data() );
    firstDerivative( scratch.data(), midAxis.data(), bands - 1, out );
}

} // namespace SpectralDerivative
