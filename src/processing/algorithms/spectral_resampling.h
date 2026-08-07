// src/processing/algorithms/spectral_resampling.h — wavelength resampling
#pragma once

#include <cstddef>

/// Spectral resampling: interpolate spectra from their native wavelength grid
/// onto a target wavelength grid (e.g. an imaging spectrometer onto Landsat /
/// Sentinel-2 band positions, or onto a spectral library's grid).
namespace SpectralResampling
{
    /**
     * Resample one spectrum by linear interpolation between source band
     * centers.
     *
     * Source wavelengths must be strictly increasing. For each target
     * wavelength: if it falls within the source range, interpolate between
     * the bracketing source bands; otherwise the output is NaN (no data).
     *
     * @param src     source spectrum (srcBands floats)
     * @param srcWl   source band center wavelengths (nm), strictly increasing
     * @param srcBands source band count (>= 2)
     * @param dstWl   target band center wavelengths (nm)
     * @param dstBands target band count (>= 1)
     * @param out     output buffer (dstBands floats)
     * @return true on success; false for invalid arguments (null pointers,
     *         srcBands < 2, dstBands < 1, or non-increasing source wavelengths)
     */
    bool resampleSpectrum( const float *src, const float *srcWl, int srcBands,
                           const float *dstWl, int dstBands, float *out );
} // namespace SpectralResampling
