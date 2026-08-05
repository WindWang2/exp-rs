// src/processing/algorithms/atmospheric_correction.cpp — DOS atmospheric correction
#include "atmospheric_correction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace AtmosphericCorrection
{

static float findMin(const float *data, size_t count)
{
    float minVal = std::numeric_limits<float>::max();
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (!std::isnan(data[i])) {
            if (data[i] < minVal) {
                minVal = data[i];
                found = true;
            }
        }
    }
    return found ? minVal : 0.0f;
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
    if (std::isnan(gain) || std::isnan(bias)) return false;
    for (size_t i = 0; i < count; i++) {
        radiance[i] = gain * dn[i] + bias;
    }
    return true;
}

bool dos1(const float *dn, float *surface, size_t count, float gain, float bias)
{
    if (!dn || !surface || count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "DOS1 atmospheric correction: %1 pixels, gain=%2, bias=%3" )
        .arg( count ).arg( gain ).arg( bias ) );

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

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "DOS2 atmospheric correction: %1 pixels, transmittance=%2" )
        .arg( count ).arg( transmittance ) );
    if (std::isnan(transmittance) || transmittance <= 0.0f || transmittance > 1.0f) return false;

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
    if (std::isnan(airmass) || airmass <= 0.0f) return 0.0f;
    // Aerosol optical depth at ~550 nm for a clear atmosphere.
    // tau=0.1 is the standard DOS1 assumption (Chavez, 1996,
    // "Image-based atmospheric corrections — revisited and improved",
    // Photogrammetric Engineering & Remote Sensing 62(9):1025-1036).
    constexpr float tau = 0.1f;
    return std::exp(-tau * airmass);
}

// ---------------------------------------------------------------------------
// QUAC (Quick Atmospheric Correction) - Bernstein et al., 2008
// ---------------------------------------------------------------------------

namespace {

/// Compute the p-th percentile (0..100) of valid (non-NaN) values in @p data.
/// Uses std::nth_element; @p scratch is reused to avoid mutating the input.
float percentile(std::vector<float> scratch, float pct)
{
    // Partition out NaN values so they do not skew the rank.
    auto end = std::partition(scratch.begin(), scratch.end(),
                              [](float v) { return !std::isnan(v); });
    const size_t n = static_cast<size_t>(std::distance(scratch.begin(), end));
    if (n == 0)
        return 0.0f;
    if (n == 1)
        return scratch[0];
    const size_t rank = static_cast<size_t>(pct / 100.0f * (n - 1));
    std::nth_element(scratch.begin(), scratch.begin() + rank, end);
    return scratch[rank];
}

} // namespace

bool quac(const float *const *dnBands, float *const *outBands,
          int bandCount, size_t pixels, QString *errorMessage)
{
    if (!dnBands || !outBands || bandCount < 2 || pixels == 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC requires >= 2 bands and > 0 pixels");
        return false;
    }

    SICNU_LOG_INFO(SicnuLogTags::Algorithms,
                   QString("QUAC atmospheric correction: %1 bands, %2 pixels").arg(bandCount).arg(pixels));

    // Per-band 1st percentile (dark, path-radiance proxy) and 99th (bright).
    std::vector<float> dark(bandCount), bright(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        if (!dnBands[b] || !outBands[b]) {
            if (errorMessage)
                *errorMessage = QStringLiteral("QUAC: null band buffer at index %1").arg(b);
            return false;
        }
        dark[b] = percentile(std::vector<float>(dnBands[b], dnBands[b] + pixels), 1.0f);
        bright[b] = percentile(std::vector<float>(dnBands[b], dnBands[b] + pixels), 99.0f);
    }

    // Scene-average bright reference (QUAC assumes ~average surface reflectance ~0.5).
    const float meanBright = std::accumulate(bright.begin(), bright.end(), 0.0f) / bandCount;
    const float meanDark = std::accumulate(dark.begin(), dark.end(), 0.0f) / bandCount;
    const float refRange = meanBright - meanDark;
    if (refRange <= 0.0f || meanBright <= 0.0f) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC: degenerate image (zero dynamic range or all-dark scene)");
        return false;
    }

    for (int b = 0; b < bandCount; ++b) {
        const float range = bright[b] - dark[b];
        if (range <= 0.0f) {
            // Flat band: output zeros.
            std::fill(outBands[b], outBands[b] + pixels, 0.0f);
            continue;
        }
        // Scale so the bright percentile maps to ~0.5 reflectance, then stretch
        // by the scene-average ratio. gain = 0.5 * refRange / (range * meanBright)
        const float gain = 0.5f * refRange / (range * meanBright);
        const float offset = -dark[b] * gain;
        const float *src = dnBands[b];
        float *dst = outBands[b];
        for (size_t i = 0; i < pixels; ++i) {
            float v = std::isnan(src[i]) ? std::numeric_limits<float>::quiet_NaN()
                                         : gain * src[i] + offset;
            // Clip to physically plausible reflectance range.
            if (!std::isnan(v))
                v = std::max(0.0f, std::min(1.0f, v));
            dst[i] = v;
        }
    }
    return true;
}

bool processFileMultiBand(const QString &sourcePath, const QString &outputPath,
                          int method, QString *errorMessage,
                          const std::function<void(double, const QString &)> &progress)
{
    if (method != 3) {
        if (errorMessage)
            *errorMessage = QStringLiteral("processFileMultiBand: unsupported method %1").arg(method);
        return false;
    }

    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (bandCount < 2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC requires a multi-band raster (>= 2 bands)");
        return false;
    }

    if (progress)
        progress(0.1, QStringLiteral("Reading %1 bands").arg(bandCount));

    // Read all bands into memory (QUAC needs full-scene statistics).
    std::vector<std::vector<float>> dnBands(bandCount, std::vector<float>(pixelCount));
    std::vector<float *> dnPtrs(bandCount), outPtrs(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        if (!srcDataset.readBandData(b + 1, dnBands[b].data(), width, height)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Failed to read band %1").arg(b + 1);
            return false;
        }
        dnPtrs[b] = dnBands[b].data();
        if (progress)
            progress(0.1 + 0.3 * (b + 1) / bandCount, QStringLiteral("Read band %1").arg(b + 1));
    }

    // Allocate output buffers.
    std::vector<std::vector<float>> outBands(bandCount, std::vector<float>(pixelCount));
    for (int b = 0; b < bandCount; ++b)
        outPtrs[b] = outBands[b].data();

    if (progress)
        progress(0.45, QStringLiteral("Running QUAC"));

    QString quacErr;
    if (!quac(dnPtrs.data(), outPtrs.data(), bandCount, pixelCount, &quacErr)) {
        if (errorMessage)
            *errorMessage = quacErr;
        return false;
    }

    if (progress)
        progress(0.9, QStringLiteral("Writing output"));

    QString writeError;
    if (!writeGdalOutput(outputPath, width, height, outBands,
                         srcDataset.geoTransform(), srcDataset.projection(), &writeError)) {
        if (errorMessage)
            *errorMessage = writeError;
        return false;
    }

    if (progress)
        progress(1.0, QStringLiteral("QUAC complete"));
    return true;
}

bool processFile(const QString &sourcePath, const QString &outputPath,
                 int bandNum, int method, float gain, float bias,
                 float airmass, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    std::vector<float> dn(pixelCount);
    if (!srcDataset.readBandData(bandNum, dn.data(), width, height)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to read band %1").arg(bandNum);
        return false;
    }

    std::vector<float> output(pixelCount);
    bool success = false;
    switch (method) {
    case 0:
        success = dnToRadiance(dn.data(), output.data(), pixelCount, gain, bias);
        break;
    case 1:
        success = dos1(dn.data(), output.data(), pixelCount, gain, bias);
        break;
    case 2: {
        const float transmittance = estimateTransmittance(airmass);
        success = dos2(dn.data(), output.data(), pixelCount, gain, bias, transmittance);
        break;
    }
    default:
        if (errorMessage)
            *errorMessage = QStringLiteral("Unknown atmospheric correction method");
        return false;
    }

    if (!success) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Atmospheric correction failed");
        return false;
    }

    std::vector<std::vector<float>> outBands = {std::move(output)};
    QString writeError;
    if (!writeGdalOutput(outputPath, width, height, outBands,
                         srcDataset.geoTransform(), srcDataset.projection(), &writeError)) {
        if (errorMessage)
            *errorMessage = writeError;
        return false;
    }

    return true;
}

} // namespace AtmosphericCorrection
