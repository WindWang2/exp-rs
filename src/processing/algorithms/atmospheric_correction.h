// src/processing/algorithms/atmospheric_correction.h
#pragma once

#include <QString>
#include <cstddef>
#include <functional>

/**
 * Atmospheric correction algorithms for optical remote sensing.
 *
 * Dark Object Subtraction (DOS) methods:
 * - DOS1: subtract minimum radiance (atmospheric path radiance estimate)
 * - DOS2: DOS1 + transmittance correction
 * - QUAC: Quick Atmospheric Correction (Bernstein 2008), image-statistics based,
 *         multi-band, outputs approximate surface reflectance in [0, 1].
 *
 * Input: raw DN (digital number) pixel values.
 * Output: surface radiance or reflectance.
 */
namespace AtmosphericCorrection
{
    /// Correction method identifiers (used by processFile/processFileMultiBand).
    /// Values match the int codes consumed by the algorithm kernels and the
    /// RS operator / QGIS algorithm wrappers.
    enum Method : int {
        DnToRadiance = 0,
        Dos1         = 1,
        Dos2         = 2,
        Quac         = 3,
    };

    /**
     * Convert DN to radiance: L = gain * DN + bias
     * @param dn     input DN values
     * @param radiance output radiance buffer
     * @param count  number of pixels
     * @param gain   radiance gain (from metadata)
     * @param bias   radiance bias (from metadata)
     */
    bool dnToRadiance(const float *dn, float *radiance, size_t count, float gain, float bias);

    /**
     * DOS1: Dark Object Subtraction (simple).
     * surface_radiance = radiance - min(radiance)
     * @param dn     input DN values
     * @param surface output surface radiance buffer
     * @param count  number of pixels
     * @param gain   radiance gain
     * @param bias   radiance bias
     */
    bool dos1(const float *dn, float *surface, size_t count, float gain, float bias);

    /**
     * DOS2: Dark Object Subtraction with transmittance correction.
     * surface = (radiance - path_radiance) / transmittance
     * @param dn           input DN values
     * @param surface      output surface reflectance buffer
     * @param count        number of pixels
     * @param gain         radiance gain
     * @param bias         radiance bias
     * @param transmittance atmospheric transmittance (0,1], e.g. from estimateTransmittance()
     */
    bool dos2(const float *dn, float *surface, size_t count, float gain, float bias, float transmittance);

    /**
     * Estimate atmospheric transmittance using simple exponential model.
     * T = exp(-tau * airmass), where tau ≈ 0.1 (clear atmosphere)
     * @param airmass relative airmass (1.0 at zenith, >1 at oblique angles)
     * @return transmittance in (0, 1]
     */
    float estimateTransmittance(float airmass);

    /**
     * QUAC (Quick Atmospheric Correction) multi-band kernel.
     *
     * Image-statistics based method (Bernstein et al., 2008). For each band the
     * dark-end (1st percentile) and bright-end (99th percentile) DN are used to
     * estimate per-band gain; the result is scaled so the scene-average bright
     * reference maps to ~0.5 reflectance and clipped to [0, 1].
     *
     * @param dnBands    array of @p bandCount input buffers (each @p pixels floats)
     * @param outBands   array of @p bandCount output buffers (each @p pixels floats)
     * @param bandCount  number of bands
     * @param pixels     number of pixels per band
     * @param errorMessage optional error sink
     * @return true on success
     */
    bool quac(const float *const *dnBands, float *const *outBands,
              int bandCount, size_t pixels, QString *errorMessage = nullptr);

    /**
     * Apply atmospheric correction to one band of a GeoTIFF and write output.
     * @param method a Method enum value (DnToRadiance / Dos1 / Dos2)
     */
    bool processFile(const QString &sourcePath, const QString &outputPath,
                     int bandNum, int method, float gain, float bias,
                     float airmass = 1.0f, QString *errorMessage = nullptr);

    /**
     * Apply multi-band atmospheric correction to a GeoTIFF and write output.
     *
     * Currently supports only Method::Quac, which requires all bands in memory.
     * The output is a Float32 multi-band GeoTIFF preserving geotransform and
     * projection. Band order follows the source raster.
     *
     * @param method Method::Quac
     */
    bool processFileMultiBand(const QString &sourcePath, const QString &outputPath,
                              int method, QString *errorMessage = nullptr,
                              const std::function<void(double, const QString &)> &progress = {});
} // namespace AtmosphericCorrection
