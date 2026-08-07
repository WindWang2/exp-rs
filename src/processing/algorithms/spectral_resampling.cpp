// src/processing/algorithms/spectral_resampling.cpp — wavelength resampling
#include "spectral_resampling.h"

#include <cmath>
#include <limits>

namespace SpectralResampling
{

bool resampleSpectrum( const float *src, const float *srcWl, int srcBands,
                       const float *dstWl, int dstBands, float *out )
{
    if ( !src || !srcWl || !dstWl || !out || srcBands < 2 || dstBands < 1 )
        return false;
    for ( int i = 1; i < srcBands; ++i )
    {
        if ( srcWl[i] <= srcWl[i - 1] )
            return false; // wavelengths must be strictly increasing
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for ( int t = 0; t < dstBands; ++t )
    {
        const float target = dstWl[t];
        if ( target < srcWl[0] || target > srcWl[srcBands - 1] )
        {
            out[t] = nan;
            continue;
        }

        // Find the bracketing source band: srcWl[i-1] <= target <= srcWl[i].
        int i = 1;
        while ( i < srcBands && srcWl[i] < target )
            ++i;
        if ( i >= srcBands )
        {
            out[t] = src[srcBands - 1];
            continue;
        }
        const float w0 = srcWl[i - 1];
        const float w1 = srcWl[i];
        if ( w1 == w0 )
        {
            out[t] = src[i];
            continue;
        }
        const float frac = ( target - w0 ) / ( w1 - w0 );
        out[t] = src[i - 1] + frac * ( src[i] - src[i - 1] );
    }
    return true;
}

} // namespace SpectralResampling
