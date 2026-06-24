// src/processing/algorithms/atmospheric_correction.h
#pragma once

#include <cstddef>

/**
 * Atmospheric correction algorithms for optical remote sensing.
 *
 * Dark Object Subtraction (DOS) methods:
 * - DOS1: subtract minimum radiance (atmospheric path radiance estimate)
 * - DOS2: DOS1 + transmittance correction
 *
 * Input: raw DN (digital number) pixel values.
 * Output: surface radiance or reflectance.
 */
namespace AtmosphericCorrection
{
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
} // namespace AtmosphericCorrection
