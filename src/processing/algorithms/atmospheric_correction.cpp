// src/processing/algorithms/atmospheric_correction.cpp — DOS atmospheric correction
#include "atmospheric_correction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"

#include <cmath>
#include <limits>
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
