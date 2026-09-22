// src/processing/algorithms/spectral_resampling.h — wavelength resampling
#pragma once

#include <cstddef>

#include <vector>

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

    /**
     * Resample one spectrum using Gaussian Spectral Response Functions (SRFs)
     * derived from target band center wavelengths @p dstWl and FWHM values @p dstFwhm.
     *
     * Gaussian SRF weight for source band wavelength \lambda_i and target band (\lambda_j, F_j):
     * \sigma_j = F_j / (2 \sqrt{2 \ln 2}) \approx F_j / 2.35482
     * w_{i,j} = \exp( - (\lambda_i - \lambda_j)^2 / (2 \sigma_j^2) )
     *
     * Non-finite source values follow the linear kernel's bracket scoping
     * (#1186): a non-finite value within a target band's SRF support
     * (|\lambda_i - \lambda_j| <= 3.5 F_j — the same band set that carries
     * weight) propagates as NaN for that band; non-finite values outside the
     * support are ignored and never degrade the Gaussian aggregate. A target
     * band whose support holds no source band at all (FWHM small against the
     * source sampling) falls back to linear interpolation, which emits NaN
     * out of the source range.
     *
     * @param dstFwhm array of target band FWHMs (nm); if nullptr or invalid, falls back
     *                to linear interpolation (resampleSpectrum).
     */
    bool resampleSpectrumGaussian( const float *src, const float *srcWl, int srcBands,
                                   const float *dstWl, const float *dstFwhm, int dstBands,
                                   float *out );

    /// Per-target-band source-range coverage (Spectral Intelligence 12.0).
    /// The linear path is center-based: a target is Full inside the source
    /// range, None outside (the kernel emits NaN exactly there). The Gaussian
    /// SRF path additionally reports Partial: the target center is inside the
    /// range but its response reaches past the source edge, so resampled
    /// values rest on truncated weight mass (missing SRF integral > 1%).
    enum class BandCoverage
    {
        Full,    ///< target fully inside the source response coverage
        Partial, ///< in-range but edge-truncated SRF (Gaussian path only)
        None,    ///< outside the source range / unusable target (NaN output)
    };

    const char *bandCoverageText( BandCoverage coverage );

    struct CoverageReport
    {
        std::vector<BandCoverage> bands; ///< per target band
        int full = 0;
        int partial = 0;
        int none = 0;
    };

    /**
     * Per-band coverage analysis WITHOUT resampling any data (pure geometry;
     * cheap enough to run once per library/sensor pair). Same argument
     * contract as resampleSpectrum: false for null pointers, srcBands < 2,
     * dstBands < 1, or a non-increasing source grid.
     */
    bool analyzeResamplingCoverage( const float *srcWl, int srcBands,
                                    const float *dstWl, const float *dstFwhm, int dstBands,
                                    CoverageReport *out );
} // namespace SpectralResampling
