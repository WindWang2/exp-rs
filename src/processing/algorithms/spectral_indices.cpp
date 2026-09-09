// src/processing/algorithms/spectral_indices.cpp — Spectral index implementations
#include "spectral_indices.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace SpectralIndices
{

namespace {
bool isScaledReflectance(const float *a, const float *b, size_t count)
{
    float m = 0.0f;
    for (size_t i = 0; i < count; i++) {
        if (std::isfinite(a[i])) m = std::max(m, std::abs(a[i]));
        if (std::isfinite(b[i])) m = std::max(m, std::abs(b[i]));
    }
    return m > 5.0f; // 0..10000 DN lands here; 0..1 reflectance never does
}
} // namespace

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

bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count, bool isScaled)
{
    return isScaled ? eviDn(nir, red, blue, out, count) : eviUnit(nir, red, blue, out, count);
}

bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Auto form: whole-buffer decision (see spectral_indices.h — streaming
    // callers must resolve the domain once and call eviUnit/eviDn).
    return isScaledReflectance(nir, red, count) ? eviDn(nir, red, blue, out, count)
                                                : eviUnit(nir, red, blue, out, count);
}

bool eviUnit(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "eviUnit: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    constexpr float addConst = 1.0f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i]) || (blue && !std::isfinite(blue[i]))) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const float bVal = blue ? blue[i] : 0.0f;
        const float denom = nir[i] + 6.0f * red[i] - 7.5f * bVal + addConst;
        out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), denom);
    }
    return true;
}

bool eviDn(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "eviDn: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    constexpr float addConst = 10000.0f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i]) || (blue && !std::isfinite(blue[i]))) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const float bVal = blue ? blue[i] : 0.0f;
        const float denom = nir[i] + 6.0f * red[i] - 7.5f * bVal + addConst;
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
bool savi(const float *nir, const float *red, float *out, size_t count, bool isScaled)
{
    return isScaled ? saviDn(nir, red, out, count) : saviUnit(nir, red, out, count);
}

bool savi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "savi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Auto form: whole-buffer decision (see spectral_indices.h).
    return isScaledReflectance(nir, red, count) ? saviDn(nir, red, out, count)
                                                : saviUnit(nir, red, out, count);
}

bool saviUnit(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "saviUnit: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    constexpr float L = 0.5f;
    // The trailing factor (1+L) is dimensionless and stays 1.5 in BOTH
    // regimes — scaling it too multiplied DN-scale outputs by ~3334x
    // (#680 review).
    constexpr float kOnePlusL = 1.5f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i])) { out[i] = std::numeric_limits<float>::quiet_NaN(); continue; }
        out[i] = MathUtils::safeDiv(nir[i] - red[i], nir[i] + red[i] + L) * kOnePlusL;
    }
    return true;
}

bool saviDn(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "saviDn: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Only the soil-brightness term scales with the data (L=0.5 on [0,1]
    // reflectance → 0.5*10000 on DN-scaled products).
    constexpr float L = 5000.0f;
    constexpr float kOnePlusL = 1.5f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        out[i] = MathUtils::safeDiv(nir[i] - red[i], nir[i] + red[i] + L) * kOnePlusL;
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

bool gndvi(const float *nir, const float *green, float *out, size_t count)
{
    if (!nir || !green || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "gndvi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(nir, green, out, count);
}

bool ndmi(const float *nir, const float *swir1, float *out, size_t count)
{
    if (!nir || !swir1 || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndmi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(nir, swir1, out, count);
}

bool arvi(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !blue || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "arvi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // RB = 2*Red - Blue; NaN in any participating band → NaN.
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i]) || !std::isfinite(blue[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        const float rb = 2.0f * red[i] - blue[i];
        out[i] = MathUtils::safeDiv(nir[i] - rb, nir[i] + rb);
    }
    return true;
}

namespace {
/// Unit-reflectance normalization for the additive-constant indices: returns
/// scaled copies (÷10000) when the magnitude heuristic fires (#680 regime),
/// otherwise borrows the inputs. Pointers stay valid for the kernel's scope.
struct ReflectancePair {
    std::vector<float> nirBuf, redBuf;
    const float *nir = nullptr;
    const float *red = nullptr;
    ReflectancePair(const float *n, const float *r, size_t count, bool isScaled)
        : nir(n), red(r)
    {
        if (isScaled) {
            nirBuf.resize(count); redBuf.resize(count);
            for (size_t i = 0; i < count; i++) {
                nirBuf[i] = std::isfinite(n[i]) ? n[i] / 10000.0f : std::numeric_limits<float>::quiet_NaN();
                redBuf[i] = std::isfinite(r[i]) ? r[i] / 10000.0f : std::numeric_limits<float>::quiet_NaN();
            }
            nir = nirBuf.data(); red = redBuf.data();
        }
    }
    ReflectancePair(const float *n, const float *r, size_t count)
        : ReflectancePair(n, r, count, isScaledReflectance(n, r, count)) {}
};
} // namespace

bool msavi(const float *nir, const float *red, float *out, size_t count, bool isScaled)
{
    return isScaled ? msaviDn(nir, red, out, count) : msaviUnit(nir, red, out, count);
}

bool msavi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "msavi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Auto form: whole-buffer decision (see spectral_indices.h).
    ReflectancePair scaled(nir, red, count);
    return msaviUnit(scaled.nir, scaled.red, out, count);
}

bool msaviUnit(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "msaviUnit: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        const float n = nir[i];
        const float r = red[i];
        if (!std::isfinite(n) || !std::isfinite(r)) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        // Formula: (2*NIR + 1 - sqrt((2*NIR + 1)^2 - 8*(NIR - Red))) / 2.
        const float term = 2.0f * n + 1.0f;
        const float discr = term * term - 8.0f * (n - r);
        if (discr < 0.0f) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        out[i] = (term - std::sqrt(discr)) / 2.0f;
    }
    return true;
}

bool msaviDn(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "msaviDn: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    ReflectancePair scaled(nir, red, count, true);
    return msaviUnit(scaled.nir, scaled.red, out, count);
}

bool evi2(const float *nir, const float *red, float *out, size_t count, bool isScaled)
{
    return isScaled ? evi2Dn(nir, red, out, count) : evi2Unit(nir, red, out, count);
}

bool evi2(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi2: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Auto form: whole-buffer decision (see spectral_indices.h).
    return isScaledReflectance(nir, red, count) ? evi2Dn(nir, red, out, count)
                                                : evi2Unit(nir, red, out, count);
}

bool evi2Unit(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi2Unit: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    constexpr float c = 1.0f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), nir[i] + 2.4f * red[i] + c);
    }
    return true;
}

bool evi2Dn(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi2Dn: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Same constant-regime rule as EVI: the additive constant scales with the
    // data regime (1 → 10000), the numerator factor stays dimensionless.
    constexpr float c = 10000.0f;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(nir[i]) || !std::isfinite(red[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), nir[i] + 2.4f * red[i] + c);
    }
    return true;
}

bool bai(const float *red, const float *nir, float *out, size_t count, bool isScaled)
{
    return isScaled ? baiDn(red, nir, out, count) : baiUnit(red, nir, out, count);
}

bool bai(const float *red, const float *nir, float *out, size_t count)
{
    if (!red || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "bai: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Auto form: whole-buffer decision (see spectral_indices.h).
    ReflectancePair scaled(nir, red, count);
    return baiUnit(scaled.red, scaled.nir, out, count);
}

bool baiUnit(const float *red, const float *nir, float *out, size_t count)
{
    if (!red || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "baiUnit: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    // Note the argument order: BAI anchors are (0.1 reflectance at Red,
    // 0.06 at NIR) per Chuvieco et al. 2002.
    for (size_t i = 0; i < count; i++) {
        const float n = nir[i];
        const float r = red[i];
        if (!std::isfinite(n) || !std::isfinite(r)) {
            out[i] = std::numeric_limits<float>::quiet_NaN(); continue;
        }
        const float dRed = 0.1f - r;
        const float dNir = 0.06f - n;
        const float denom = dRed * dRed + dNir * dNir;
        out[i] = MathUtils::safeDiv(1.0f, denom);
    }
    return true;
}

bool baiDn(const float *red, const float *nir, float *out, size_t count)
{
    if (!red || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "baiDn: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    ReflectancePair scaled(nir, red, count, true);
    return baiUnit(scaled.red, scaled.nir, out, count);
}

bool ui(const float *swir2, const float *nir, float *out, size_t count)
{
    if (!swir2 || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ui: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(swir2, nir, out, count);
}

bool bui(const float *swir, const float *nir, const float *red, float *out, size_t count)
{
    if (!swir || !nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "bui: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        if (!std::isfinite(swir[i]) || !std::isfinite(nir[i]) || !std::isfinite(red[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const float ndbi = MathUtils::safeDiv(swir[i] - nir[i], swir[i] + nir[i]);
        const float ndvi = MathUtils::safeDiv(nir[i] - red[i], nir[i] + red[i]);
        out[i] = ndbi - ndvi;
    }
    return true;
}

} // namespace SpectralIndices
