// src/processing/algorithms/spectral_indices.cpp — Spectral index implementations
#include "spectral_indices.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace SpectralIndices
{

static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

static inline float safeDiv(float num, float denom)
{
    return (denom == 0.0f) ? NaN : (num / denom);
}

bool ndvi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        out[i] = safeDiv(nir[i] - red[i], nir[i] + red[i]);
    }
    return true;
}

bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !blue || !out || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        float denom = nir[i] + 6.0f * red[i] - 7.5f * blue[i] + 1.0f;
        out[i] = safeDiv(2.5f * (nir[i] - red[i]), denom);
    }
    return true;
}

bool savi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out || count == 0) return false;
    constexpr float L = 0.5f;
    for (size_t i = 0; i < count; i++) {
        out[i] = safeDiv(nir[i] - red[i], nir[i] + red[i] + L) * (1.0f + L);
    }
    return true;
}

bool ndwi(const float *green, const float *nir, float *out, size_t count)
{
    if (!green || !nir || !out || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        out[i] = safeDiv(green[i] - nir[i], green[i] + nir[i]);
    }
    return true;
}

bool ndbi(const float *swir, const float *nir, float *out, size_t count)
{
    if (!swir || !nir || !out || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        out[i] = safeDiv(swir[i] - nir[i], swir[i] + nir[i]);
    }
    return true;
}

bool mndwi(const float *green, const float *swir, float *out, size_t count)
{
    if (!green || !swir || !out || count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        out[i] = safeDiv(green[i] - swir[i], green[i] + swir[i]);
    }
    return true;
}

} // namespace SpectralIndices
