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
     * Extract the dark-object radiance level from a scene histogram
     * (Chavez, 1996, "Image-based atmospheric corrections — revisited and
     * improved", PERS 62(9):1025-1036).
     *
     * The scene radiance is binned into a histogram (default 1024 bins) and
     * the dark object is the lowest radiance whose bin holds at least
     * max(2, valid/10000) pixels — 0.01% of the valid scene.  Isolated
     * single-pixel sensor noise below the real scene floor is thereby
     * rejected, which a plain global-minimum scan would absorb.
     *
     * @param radiance scene radiance values (NaN pixels are ignored)
     * @param count    number of values
     * @param bins     histogram bin count (clamped to [16, 2^20])
     * @return dark-object radiance level; 0.0 for null/empty/all-NaN input;
     *         falls back to the global minimum for tiny scenes
     */
    float findDarkObjectByHistogram(const float *radiance, size_t count, int bins = 1024);

    /**
     * DOS1 with histogram-based dark-object extraction.
     *
     * Identical to dos1() except the subtracted level comes from
     * findDarkObjectByHistogram() instead of the global scene minimum, so a
     * handful of outlying dark pixels cannot drag the whole scene's baseline.
     * On outlier-free scenes the two produce identical output.
     *
     * @param dn     input DN values
     * @param surface output surface radiance buffer
     * @param count  number of pixels
     * @param gain   radiance gain
     * @param bias   radiance bias
     */
    bool dos1Histogram(const float *dn, float *surface, size_t count, float gain, float bias);

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
