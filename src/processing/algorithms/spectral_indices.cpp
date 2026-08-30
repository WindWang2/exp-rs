// src/processing/algorithms/spectral_indices.cpp — Spectral index implementations
#include "spectral_indices.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace SpectralIndices
{

static bool isScaledReflectance(const float *a, const float *b, size_t n) {
    float m = 0.0f;
    for (size_t i = 0; i < n && m <= 5.0f; i++)
        if (std::isfinite(a[i])) m = std::max(m, std::abs(a[i]));
    for (size_t i = 0; i < n && m <= 5.0f; i++)
        if (std::isfinite(b[i])) m = std::max(m, std::abs(b[i]));
    return m > 5.0f; // 0..10000 DN lands here; 0..1 reflectance never does
}

bool ndvi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndvi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Computing NDVI: %1 pixels" ).arg( count ) );
    return MathUtils::normalizedDifference(nir, red, out, count);
}

bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !blue || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    const bool scaled = isScaledReflectance(nir, red, count);
    if (scaled) {
        for (size_t i = 0; i < count; i++) {
            if (!std::isfinite(nir[i]) || !std::isfinite(red[i]) || (blue && !std::isfinite(blue[i]))) {
                out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
            }
            const float denom = nir[i] + 6.0f * red[i] - 7.5f * blue[i] + 10000.0f;
            out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), denom);
        }
        return true;
    }
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i]) || (blue && !std::isfinite(blue[i]))) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        float denom = nir[i] + 6.0f * red[i] - 7.5f * blue[i] + 1.0f;
        out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), denom);
    }
    return true;
}

// Scale heuristic for #680: the stack output copies pixels verbatim (no
// gain/bias applied, satellite_products.cpp:1515), but stamps
// SICNU_RADIOMETRIC_STATE = reflectance — so S2/Landsat L2 values sit on
// 0..10000. SAVI with L=0.5 is then negligible (SAVI ~= 1.5*NDVI). Detect
// the DN scale by the magnitude of the samples (max absolute > 5) and
// scale L/EWI constant proportionally, matching the index magnitude.
bool savi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "savi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    const bool scaled = isScaledReflectance(nir, red, count);
    const float L = scaled ? 5000.0f : 0.5f; // 0.5*10000 when the data are DN-scaled
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i])) { out[i] = std::numeric_limits<float>::quiet_NaN(); continue; }
        out[i] = MathUtils::safeDiv(nir[i] - red[i], nir[i] + red[i] + L) * (1.0f + L);
    }
    return true;
}

bool ndwi(const float *green, const float *nir, float *out, size_t count)
{
    if (!green || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndwi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(green, nir, out, count);
}

bool ndbi(const float *swir, const float *nir, float *out, size_t count)
{
    if (!swir || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndbi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(swir, nir, out, count);
}

bool mndwi(const float *green, const float *swir, float *out, size_t count)
{
    if (!green || !swir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "mndwi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(green, swir, out, count);
}

} // namespace SpectralIndices
