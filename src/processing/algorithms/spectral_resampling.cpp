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

bool resampleSpectrumGaussian( const float *src, const float *srcWl, int srcBands,
                               const float *dstWl, const float *dstFwhm, int dstBands,
                               float *out )
{
    if ( !src || !srcWl || !dstWl || !out || srcBands < 2 || dstBands < 1 )
        return false;

    // Same contract as the linear path / spectral_library.h: source wavelength
    // grids must be strictly increasing. Refuse (do not silently weight) a
    // non-monotonic grid — matchSpectrum then skips the entry.
    for ( int i = 1; i < srcBands; ++i )
    {
        if ( srcWl[i] <= srcWl[i - 1] )
            return false;
    }

    if ( !dstFwhm )
    {
        return resampleSpectrum( src, srcWl, srcBands, dstWl, dstBands, out );
    }

    bool hasValidFwhm = false;
    for ( int j = 0; j < dstBands; ++j )
    {
        if ( std::isfinite( dstFwhm[j] ) && dstFwhm[j] > 0.0f )
        {
            hasValidFwhm = true;
            break;
        }
    }
    if ( !hasValidFwhm )
    {
        return resampleSpectrum( src, srcWl, srcBands, dstWl, dstBands, out );
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    // FWHM = 2 * sqrt(2 * ln(2)) * sigma ≈ 2.35482 * sigma
    // => sigma = FWHM / (2 * sqrt(2 * ln(2))) ≈ FWHM * 0.42466
    constexpr float kFwhmToSigma = 1.0f / ( 2.0f * 1.1774100225154747f );

    for ( int t = 0; t < dstBands; ++t )
    {
        const float targetWl = dstWl[t];
        const float fwhm = dstFwhm[t];

        if ( !std::isfinite( targetWl ) || targetWl < srcWl[0] || targetWl > srcWl[srcBands - 1] )
        {
            out[t] = nan;
            continue;
        }

        if ( !std::isfinite( fwhm ) || fwhm <= 0.0f )
        {
            resampleSpectrum( src, srcWl, srcBands, &targetWl, 1, &out[t] );
            continue;
        }

        const float sigma = fwhm * kFwhmToSigma;
        const float twoSigmaSq = 2.0f * sigma * sigma;

        double sumWeight = 0.0;
        double sumVal = 0.0;

        for ( int i = 0; i < srcBands; ++i )
        {
            const float sVal = src[i];
            if ( !std::isfinite( sVal ) )
                continue;
            const float diff = srcWl[i] - targetWl;
            if ( std::abs( diff ) > 3.5f * fwhm )
                continue;

            const double w = std::exp( -static_cast<double>( diff * diff ) / static_cast<double>( twoSigmaSq ) );
            sumWeight += w;
            sumVal += w * static_cast<double>( sVal );
        }

        if ( sumWeight > 1e-12 )
        {
            out[t] = static_cast<float>( sumVal / sumWeight );
        }
        else
        {
            resampleSpectrum( src, srcWl, srcBands, &targetWl, 1, &out[t] );
        }
    }
    return true;
}

const char *bandCoverageText( BandCoverage coverage )
{
    switch ( coverage )
    {
        case BandCoverage::Full:
            return "full";
        case BandCoverage::Partial:
            return "partial";
        case BandCoverage::None:
            break;
    }
    return "none";
}

bool analyzeResamplingCoverage( const float *srcWl, int srcBands,
                                const float *dstWl, const float *dstFwhm, int dstBands,
                                CoverageReport *out )
{
    if ( !srcWl || !dstWl || !out || srcBands < 2 || dstBands < 1 )
        return false;
    for ( int i = 1; i < srcBands; ++i )
    {
        if ( srcWl[i] <= srcWl[i - 1] )
            return false; // same strictly-increasing contract as the kernels
    }

    const float srcMin = srcWl[0];
    const float srcMax = srcWl[srcBands - 1];

    out->bands.assign( static_cast<size_t>( dstBands ), BandCoverage::None );
    out->full = 0;
    out->partial = 0;
    out->none = 0;

    // FWHM → sigma, identical constant to resampleSpectrumGaussian: sigma =
    // FWHM / 2.3548200450309493. Spelled as a literal rather than
    // 1/(2*sqrt(2*log(2))): std::log/std::sqrt are not constexpr on every
    // standard library, and the expression made this TU fail to compile on
    // MSVC (C2131). The DIVISION is load-bearing: multiplying here (as an
    // earlier revision did) inflates sigma by 2.3548² and reports Partial for
    // targets whose response is in fact fully captured.
    constexpr double kFwhmToSigma = 2.3548200450309493;

    for ( int t = 0; t < dstBands; ++t )
    {
        const float wl = dstWl[t];
        BandCoverage coverage = BandCoverage::None;
        if ( std::isfinite( wl ) && wl >= srcMin && wl <= srcMax )
        {
            coverage = BandCoverage::Full;
            if ( dstFwhm )
            {
                const float fwhm = dstFwhm[t];
                if ( std::isfinite( fwhm ) && fwhm > 0.0f )
                {
                    // Fraction of the target band's Gaussian response captured
                    // by the source range, via the closed-form erf integral
                    // (z in units of sqrt(2)*sigma). Missing mass beyond ~1%
                    // means the resampled value rests on a visibly truncated
                    // SRF — report Partial.
                    const double sigmaSqrt2 =
                        static_cast<double>( fwhm ) / kFwhmToSigma * std::sqrt( 2.0 );
                    const double zLo = ( static_cast<double>( srcMin ) - wl ) / sigmaSqrt2;
                    const double zHi = ( static_cast<double>( srcMax ) - wl ) / sigmaSqrt2;
                    const double captured = 0.5 * ( std::erf( zHi ) - std::erf( zLo ) );
                    if ( !( captured >= 0.99 ) )
                        coverage = BandCoverage::Partial;
                }
            }
        }
        out->bands[static_cast<size_t>( t )] = coverage;
        switch ( coverage )
        {
            case BandCoverage::Full:
                ++out->full;
                break;
            case BandCoverage::Partial:
                ++out->partial;
                break;
            case BandCoverage::None:
                ++out->none;
                break;
        }
    }
    return true;
}

} // namespace SpectralResampling
