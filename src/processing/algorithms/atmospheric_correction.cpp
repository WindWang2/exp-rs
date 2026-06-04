// src/processing/algorithms/atmospheric_correction.cpp — DOS atmospheric correction
#include "atmospheric_correction.h"

#include <cmath>
#include <vector>

namespace AtmosphericCorrection
{

static float findMin(const float *data, size_t count)
{
    float minVal = data[0];
    for (size_t i = 1; i < count; i++) {
        if (data[i] < minVal) minVal = data[i];
    }
    return minVal;
}

static bool convertAndFindMin(const float *dn, std::vector<float> &radiance,
                              size_t count, float gain, float bias, float &minRadiance)
{
    if (!dnToRadiance(dn, radiance.data(), count, gain, bias))
        return false;
    minRadiance = findMin(radiance.data(), count);
    return true;
}

bool dnToRadiance(const float *dn, float *radiance, size_t count, float gain, float bias)
{
    if (!dn || !radiance || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        radiance[i] = gain * dn[i] + bias;
    }
    return true;
}

bool dos1(const float *dn, float *surface, size_t count, float gain, float bias)
{
    if (!dn || !surface || count == 0) return false;

    std::vector<float> radiance(count);
    float minRadiance;
    if (!convertAndFindMin(dn, radiance, count, gain, bias, minRadiance))
        return false;

    for (size_t i = 0; i < count; i++) {
        surface[i] = radiance[i] - minRadiance;
    }
    return true;
}

bool dos2(const float *dn, float *surface, size_t count, float gain, float bias, float transmittance)
{
    if (!dn || !surface || count == 0) return false;
    if (transmittance <= 0.0f || transmittance > 1.0f) return false;

    std::vector<float> radiance(count);
    float pathRadiance;
    if (!convertAndFindMin(dn, radiance, count, gain, bias, pathRadiance))
        return false;

    for (size_t i = 0; i < count; i++) {
        surface[i] = (radiance[i] - pathRadiance) / transmittance;
    }
    return true;
}

float estimateTransmittance(float airmass)
{
    if (airmass <= 0.0f) return 0.0f;
    constexpr float tau = 0.1f;
    return std::exp(-tau * airmass);
}

} // namespace AtmosphericCorrection
