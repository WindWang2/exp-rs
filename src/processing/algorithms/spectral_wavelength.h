// src/processing/algorithms/spectral_wavelength.h — wavelength grid helpers
#pragma once

#include <string>
#include <utility>
#include <vector>

/// Wavelength-grid value helpers shared by the spectral reference seam, band
/// selection and (via operators) the wavelength-aware operators.
///
/// The platform's raster convention is per-band GDAL metadata: `WAVELENGTH`
/// (numeric string) plus optional `WAVELENGTH_UNITS` (default "nm" when
/// absent — the de facto convention of every writer in this repo). Grids here
/// are always normalized to nanometers; unknown units are a typed refusal,
/// never a silent pass-through.
namespace SpectralWavelength
{
    /// A validated, nanometer-normalized band grid.
    struct Grid
    {
        std::vector<float> centersNm; ///< strictly increasing, positive, finite
        std::vector<float> fwhmNm;    ///< optional (empty when absent); positive finite

        bool empty() const { return centersNm.empty(); }
        int size() const { return static_cast<int>( centersNm.size() ); }
        bool hasFwhm() const { return !fwhmNm.empty(); }
        float minNm() const { return centersNm.empty() ? 0.0f : centersNm.front(); }
        float maxNm() const { return centersNm.empty() ? 0.0f : centersNm.back(); }
    };

    enum class Status
    {
        Ok,
        Empty,          ///< no band carries a wavelength
        Partial,        ///< some, but not all, bands carry a wavelength
        NonMonotonic,   ///< centers not strictly increasing after normalization
        UnknownUnits,   ///< WAVELENGTH_UNITS outside the supported set
        NonFinite,      ///< unparsable / non-finite value
        SizeMismatch,   ///< FWHM count differs from center count
    };

    const char *statusText( Status status );

    /// Normalize one wavelength to nm. Supported units (case-insensitive,
    /// ASCII/Unicode micro sign accepted): "nm", "µm"/"um"/"micrometer(s)"/
    /// "micron(s)". @p units empty defaults to "nm" (repo convention).
    /// Returns false for unknown units or non-finite values.
    bool normalizeToNm( double value, const std::string &units, float *outNm );

    /// Build a grid from per-band (value, units) pairs as they come from GDAL
    /// band metadata, plus optional per-band FWHM values (same unit rule,
    /// paired with @p fwhmUnits per band). Wavelengths are required on every
    /// band — a partial grid is a typed refusal, never a silent subset.
    Status gridFromBandValues( const std::vector<std::pair<double, std::string>> &wavelengths,
                               const std::vector<std::pair<double, std::string>> &fwhm,
                               Grid *out );

    /// Overlap of two grids in nm. Returns false (with the reason text) when
    /// the ranges are disjoint or the overlap is a single degenerate point —
    /// resampling onto an empty range cannot produce data.
    bool rangesOverlap( const Grid &a, const Grid &b, std::string *reason = nullptr );
} // namespace SpectralWavelength
