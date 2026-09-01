#include "image_enhancement.h"
#include "math_utils.h"
#include "chunked_processor.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"
#include <gdal.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>
#include <limits>

void ImageEnhancement::linearStretch(const float *input, float *output, size_t count,
                                     float minVal, float maxVal, float nodata)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "linearStretch: null pointer argument");
        return;
    }
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Linear stretch: %1 pixels, range=[%2, %3]" )
        .arg( count ).arg( minVal ).arg( maxVal ) );
    float range = maxVal - minVal;
    if (range == 0) range = 1.0f;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) {
            output[i] = nodata;
            continue;
        }
        float normalized = (input[i] - minVal) / range;
        output[i] = std::clamp(normalized * 255.0f, 0.0f, 255.0f);
    }
}

void ImageEnhancement::percentClipStretch(const float *input, float *output, size_t count,
                                          float pct, float nodata)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "percentClipStretch: null pointer argument");
        return;
    }
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Percent clip stretch: %1 pixels, clip=%2%" ).arg( count ).arg( pct ) );
    std::vector<float> valid;
    valid.reserve(count);
    for (size_t i = 0; i < count; i++) {
        if (input[i] != nodata && !std::isnan(input[i]))
            valid.push_back(input[i]);
    }

    if (valid.empty()) {
        for (size_t i = 0; i < count; i++) output[i] = nodata;
        return;
    }

    std::sort(valid.begin(), valid.end());
    size_t clipCount = static_cast<size_t>(valid.size() * pct / 100.0f);
    size_t lo = clipCount;
    size_t hi = valid.size() - 1 - clipCount;
    if (hi <= lo) {
        lo = 0;
        hi = valid.size() - 1;
    }

    linearStretch(input, output, count, valid[lo], valid[hi], nodata);
}

void ImageEnhancement::stddevStretch(const float *input, float *output, size_t count,
                                     float k, float nodata)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "stddevStretch: null pointer argument");
        return;
    }
    MathUtils::Stats stats = MathUtils::computeStatsWithNodata(input, count, nodata);

    float lo = stats.mean - k * stats.stddev;
    float hi = stats.mean + k * stats.stddev;

    linearStretch(input, output, count, lo, hi, nodata);
}

void ImageEnhancement::histogramEqualize(const float *input, float *output, size_t count,
                                         int bins, float nodata)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "histogramEqualize: null pointer argument");
        return;
    }
    if (bins < 1) bins = 256; // Validate bins parameter
    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Histogram equalization: %1 pixels, %2 bins" ).arg( count ).arg( bins ) );
    MathUtils::Stats stats = MathUtils::computeStatsWithNodata(input, count, nodata);
    if (stats.validCount == 0) {
        for (size_t i = 0; i < count; i++)
            output[i] = nodata;
        return;
    }

    float min = stats.min;
    float max = stats.max;

    if (min == max) {
        for (size_t i = 0; i < count; i++)
            output[i] = (input[i] == nodata || std::isnan(input[i])) ? nodata : 128.0f;
        return;
    }

    // uint64 bin counters: a single bin can hold >2^31 pixels on >2Gpx rasters (#446)
    std::vector<uint64_t> hist(bins, 0);
    float binWidth = (max - min) / bins;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) continue;
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        hist[bin]++;
    }

    std::vector<float> cdf(bins);
    uint64_t validCount = 0;
    for (int i = 0; i < bins; i++) validCount += hist[i];

    // All pixels are nodata — output all nodata
    if (validCount == 0) {
        for (size_t i = 0; i < count; i++) output[i] = nodata;
        return;
    }

    cdf[0] = static_cast<float>(hist[0]) / validCount;
    for (int i = 1; i < bins; i++)
        cdf[i] = cdf[i - 1] + static_cast<float>(hist[i]) / validCount;

    // Standard equalization remaps via (cdf - cdf_min)/(1 - cdf_min) so darkest populated value maps to 0
    float cdf_min = 0.0f;
    for (int i = 0; i < bins; ++i) {
        if (hist[i] > 0) { cdf_min = cdf[i]; break; }
    }
    const float denom = 1.0f - cdf_min;
    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) {
            output[i] = nodata;
            continue;
        }
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        if (denom < 1e-6f) {
            output[i] = 128.0f;
        } else {
            output[i] = (cdf[bin] - cdf_min) / denom * 255.0f;
        }
    }
}

void ImageEnhancement::piecewiseLinearStretch(const float *input, float *output, size_t count,
                                           const std::vector<std::pair<float, float>> &controlPoints,
                                           float nodata)
{
    if (controlPoints.size() < 2) {
        for (size_t i = 0; i < count; i++) {
            output[i] = (input[i] == nodata || std::isnan(input[i])) ? nodata : input[i];
        }
        return;
    }

    for (size_t i = 0; i < count; i++) {
        float val = input[i];
        if (val == nodata || std::isnan(val)) {
            output[i] = nodata;
            continue;
        }

        if (val <= controlPoints.front().first) {
            output[i] = controlPoints.front().second;
            continue;
        }
        if (val >= controlPoints.back().first) {
            output[i] = controlPoints.back().second;
            continue;
        }

        for (size_t p = 0; p < controlPoints.size() - 1; p++) {
            float x1 = controlPoints[p].first;
            float y1 = controlPoints[p].second;
            float x2 = controlPoints[p + 1].first;
            float y2 = controlPoints[p + 1].second;

            if (val >= x1 && val <= x2) {
                float dx = std::max(1e-6f, x2 - x1);
                float ratio = (val - x1) / dx;
                output[i] = y1 + ratio * (y2 - y1);
                break;
            }
        }
    }
}

// ---- Spatial filter helpers ----

void ImageEnhancement::generateGaussianKernel(float *kernel, int size, float sigma)
{
    if (sigma <= 0.0f) sigma = 1e-6f;
    int half = size / 2;
    float sum = 0.0f;
    float twoSigmaSq = 2.0f * sigma * sigma;

    for (int y = -half; y <= half; y++) {
        for (int x = -half; x <= half; x++) {
            float val = std::exp(-(x * x + y * y) / twoSigmaSq);
            kernel[(y + half) * size + (x + half)] = val;
            sum += val;
        }
    }

    // Normalize
    for (int i = 0; i < size * size; i++) {
        kernel[i] /= sum;
    }
}

// Generate 1D Gaussian kernel for separable convolution
static void generateGaussianKernel1D(float *kernel, int size, float sigma)
{
    if (sigma <= 0.0f) sigma = 1e-6f;
    int half = size / 2;
    float sum = 0.0f;
    float twoSigmaSq = 2.0f * sigma * sigma;

    for (int i = -half; i <= half; i++) {
        float val = std::exp(-(i * i) / twoSigmaSq);
        kernel[i + half] = val;
        sum += val;
    }
    for (int i = 0; i < size; i++)
        kernel[i] /= sum;
}

// Separable convolution: horizontal pass then vertical pass
// O(n * 2k) instead of O(n * k^2)
// Uses ChunkedProcessor for parallel processing
static void separableConvolve(const float *input, float *output, int width, int height,
                              const float *kernel1D, int kernelSize)
{
    int half = kernelSize / 2;
    // Zero-sum derivative kernels must not be weight-renormalized (see
    // convolve); averaging kernels renormalize over finite neighbors.
    float kernelSum = 0.0f;
    for (int i = 0; i < kernelSize; ++i)
        kernelSum += kernel1D[i];
    const bool isAveragingKernel = kernelSum > 1e-6f;
    // Temporary buffer for horizontal pass
    std::vector<float> temp(static_cast<size_t>(width) * height);

    // Horizontal pass: input -> temp (row-major, cache-friendly)
    // Process in chunks for better cache locality
    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);
        for (int y = yStart; y < yEnd; y++) {
            size_t rowOff = static_cast<size_t>(y) * width;
            for (int x = 0; x < width; x++) {
                // NoData in -> NoData out (preserve mask)
                if (!std::isfinite(input[rowOff + x])) {
                    temp[rowOff + x] = std::numeric_limits<float>::quiet_NaN();
                    continue;
                }
                float sum = 0.0f;
                float wSum = 0.0f;
                bool hasFinite = false;
                for (int k = -half; k <= half; k++) {
                    int ix = std::clamp(x + k, 0, width - 1);
                    float val = input[rowOff + ix];
                    if (std::isfinite(val)) {
                        float w = kernel1D[k + half];
                        sum += val * w;
                        wSum += w;
                        hasFinite = true;
                    }
                }
                if (isAveragingKernel)
                    temp[rowOff + x] = (wSum > 1e-6f) ? (sum / wSum) : std::numeric_limits<float>::quiet_NaN();
                else
                    temp[rowOff + x] = hasFinite ? sum : std::numeric_limits<float>::quiet_NaN();
            }
        }
    }

    // Vertical pass: temp -> output
    // Process in column chunks for better cache locality
    const int chunkWidth = 64;
    for (int xStart = 0; xStart < width; xStart += chunkWidth) {
        int xEnd = std::min(xStart + chunkWidth, width);
        for (int y = 0; y < height; y++) {
            size_t rowOff = static_cast<size_t>(y) * width;
            for (int x = xStart; x < xEnd; x++) {
                if (!std::isfinite(input[rowOff + x])) {
                    output[rowOff + x] = std::numeric_limits<float>::quiet_NaN();
                    continue;
                }
                float sum = 0.0f;
                float wSum = 0.0f;
                bool hasFinite = false;
                for (int k = -half; k <= half; k++) {
                    int iy = std::clamp(y + k, 0, height - 1);
                    float val = temp[static_cast<size_t>(iy) * width + x];
                    if (std::isfinite(val)) {
                        float w = kernel1D[k + half];
                        sum += val * w;
                        wSum += w;
                        hasFinite = true;
                    }
                }
                if (isAveragingKernel)
                    output[rowOff + x] = (wSum > 1e-6f) ? (sum / wSum) : std::numeric_limits<float>::quiet_NaN();
                else
                    output[rowOff + x] = hasFinite ? sum : std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
}

void ImageEnhancement::convolve(const float *input, float *output, int width, int height,
                                const float *kernel, int kernelSize)
{
    int half = kernelSize / 2;

    // Averaging kernels (positive total weight, e.g. box/Gaussian) renormalize
    // by the finite-neighbor weight sum so NoData borders stay unbiased.
    // Zero-sum derivative kernels (Sobel/Laplacian) must emit the raw sum —
    // normalizing them yields NaN everywhere (#442).
    float kernelSum = 0.0f;
    for (int i = 0; i < kernelSize * kernelSize; ++i)
        kernelSum += kernel[i];
    const bool isAveragingKernel = kernelSum > 1e-6f;

    // Process in chunks for better cache locality
    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);

        for (int y = yStart; y < yEnd; y++) {
            for (int x = 0; x < width; x++) {
                if (!std::isfinite(input[y * width + x])) {
                    output[y * width + x] = std::numeric_limits<float>::quiet_NaN();
                    continue;
                }
                float sum = 0.0f;
                float wSum = 0.0f;
                bool hasFinite = false;

                for (int ky = -half; ky <= half; ky++) {
                    for (int kx = -half; kx <= half; kx++) {
                        // Clamp coordinates to image boundaries (replicate padding)
                        int ix = std::clamp(x + kx, 0, width - 1);
                        int iy = std::clamp(y + ky, 0, height - 1);

                        float pixel = input[iy * width + ix];
                        if (std::isfinite(pixel)) {
                            float kVal = kernel[(ky + half) * kernelSize + (kx + half)];
                            sum += pixel * kVal;
                            wSum += kVal;
                            hasFinite = true;
                        }
                    }
                }

                if (isAveragingKernel)
                    output[y * width + x] = (wSum > 1e-6f) ? (sum / wSum) : std::numeric_limits<float>::quiet_NaN();
                else
                    output[y * width + x] = hasFinite ? sum : std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
}

// ---- Public spatial filters ----

void ImageEnhancement::meanFilter(const float *input, float *output, int width, int height, int kernelSize)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "meanFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("meanFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    // Mean filter is separable: 1D kernel is [1/n, 1/n, ..., 1/n]
    std::vector<float> kernel1D(kernelSize);
    float val = 1.0f / static_cast<float>(kernelSize);
    std::fill(kernel1D.begin(), kernel1D.end(), val);
    separableConvolve(input, output, width, height, kernel1D.data(), kernelSize);
}

void ImageEnhancement::gaussianFilter(const float *input, float *output, int width, int height, int kernelSize, float sigma)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "gaussianFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("gaussianFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Gaussian filter: %1x%2, kernel=%3, sigma=%4" )
        .arg( width ).arg( height ).arg( kernelSize ).arg( sigma ) );
    if (kernelSize < 1) kernelSize = 1;
    if (kernelSize % 2 == 0) kernelSize++; // Force odd
    // Gaussian is separable: use 1D kernel
    std::vector<float> kernel1D(kernelSize);
    generateGaussianKernel1D(kernel1D.data(), kernelSize, sigma);
    separableConvolve(input, output, width, height, kernel1D.data(), kernelSize);
}

void ImageEnhancement::medianFilter(const float *input, float *output, int width, int height, int kernelSize)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "medianFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("medianFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Median filter: %1x%2, kernel=%3" ).arg( width ).arg( height ).arg( kernelSize ) );
    if (kernelSize < 1) kernelSize = 1;
    if (kernelSize % 2 == 0) kernelSize++; // Force odd
    if (kernelSize > 7) kernelSize = 7;    // Clamp to max supported

    int half = kernelSize / 2;
    const int kSize = kernelSize * kernelSize;
    std::vector<float> validNeighbors;
    validNeighbors.reserve(kSize);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (!std::isfinite(input[y * width + x])) {
                output[y * width + x] = std::numeric_limits<float>::quiet_NaN();
                continue;
            }
            validNeighbors.clear();
            for (int ky = -half; ky <= half; ky++) {
                for (int kx = -half; kx <= half; kx++) {
                    int ix = std::clamp(x + kx, 0, width - 1);
                    int iy = std::clamp(y + ky, 0, height - 1);
                    float v = input[iy * width + ix];
                    if (std::isfinite(v))
                        validNeighbors.push_back(v);
                }
            }

            if (validNeighbors.empty()) {
                output[y * width + x] = std::numeric_limits<float>::quiet_NaN();
            } else {
                size_t mid = validNeighbors.size() / 2;
                std::nth_element(validNeighbors.begin(), validNeighbors.begin() + mid, validNeighbors.end());
                output[y * width + x] = validNeighbors[mid];
            }
        }
    }
}

void ImageEnhancement::sobelFilter(const float *input, float *output, int width, int height)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "sobelFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("sobelFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }

    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);
        for (int y = yStart; y < yEnd; y++) {
            const size_t rowOff = static_cast<size_t>(y) * width;
            if (y > 0 && y < height - 1 && width >= 3) {
                const float *rowPrev = input + static_cast<size_t>(y - 1) * width;
                const float *rowCurr = input + rowOff;
                const float *rowNext = input + static_cast<size_t>(y + 1) * width;

                // Left border x = 0 (replicate padding x-1 -> 0)
                {
                    float p00 = rowPrev[0], p01 = rowPrev[0], p02 = rowPrev[1];
                    float p10 = rowCurr[0],                   p12 = rowCurr[1];
                    float p20 = rowNext[0], p21 = rowNext[0], p22 = rowNext[1];
                    float gx = (p02 + 2.0f * p12 + p22) - (p00 + 2.0f * p10 + p20);
                    float gy = (p20 + 2.0f * p21 + p22) - (p00 + 2.0f * p01 + p02);
                    output[rowOff] = std::sqrt(gx * gx + gy * gy);
                }

                // Interior 1 <= x < width - 1
                for (int x = 1; x < width - 1; x++) {
                    float p00 = rowPrev[x - 1], p01 = rowPrev[x], p02 = rowPrev[x + 1];
                    float p10 = rowCurr[x - 1],                    p12 = rowCurr[x + 1];
                    float p20 = rowNext[x - 1], p21 = rowNext[x], p22 = rowNext[x + 1];
                    float gx = (p02 + 2.0f * p12 + p22) - (p00 + 2.0f * p10 + p20);
                    float gy = (p20 + 2.0f * p21 + p22) - (p00 + 2.0f * p01 + p02);
                    output[rowOff + x] = std::sqrt(gx * gx + gy * gy);
                }

                // Right border x = width - 1 (replicate padding x+1 -> width-1)
                {
                    int x = width - 1;
                    float p00 = rowPrev[x - 1], p01 = rowPrev[x], p02 = rowPrev[x];
                    float p10 = rowCurr[x - 1],                   p12 = rowCurr[x];
                    float p20 = rowNext[x - 1], p21 = rowNext[x], p22 = rowNext[x];
                    float gx = (p02 + 2.0f * p12 + p22) - (p00 + 2.0f * p10 + p20);
                    float gy = (p20 + 2.0f * p21 + p22) - (p00 + 2.0f * p01 + p02);
                    output[rowOff + x] = std::sqrt(gx * gx + gy * gy);
                }
            } else {
                for (int x = 0; x < width; x++) {
                    float gx = 0.0f, gy = 0.0f;
                    for (int ky = -1; ky <= 1; ky++) {
                        int iy = std::clamp(y + ky, 0, height - 1);
                        const size_t rOff = static_cast<size_t>(iy) * width;
                        for (int kx = -1; kx <= 1; kx++) {
                            int ix = std::clamp(x + kx, 0, width - 1);
                            float p = input[rOff + ix];
                            if (kx == -1) gx -= (ky == 0 ? 2.0f : 1.0f) * p;
                            else if (kx == 1) gx += (ky == 0 ? 2.0f : 1.0f) * p;
                            if (ky == -1) gy -= (kx == 0 ? 2.0f : 1.0f) * p;
                            else if (ky == 1) gy += (kx == 0 ? 2.0f : 1.0f) * p;
                        }
                    }
                    output[rowOff + x] = std::sqrt(gx * gx + gy * gy);
                }
            }
        }
    }
}

void ImageEnhancement::laplacianFilter(const float *input, float *output, int width, int height)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "laplacianFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("laplacianFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }

    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);
        for (int y = yStart; y < yEnd; y++) {
            const size_t rowOff = static_cast<size_t>(y) * width;
            if (y > 0 && y < height - 1 && width >= 3) {
                const float *rowPrev = input + static_cast<size_t>(y - 1) * width;
                const float *rowCurr = input + rowOff;
                const float *rowNext = input + static_cast<size_t>(y + 1) * width;

                // Left border
                output[rowOff] = rowPrev[0] + rowCurr[0] + rowCurr[1] + rowNext[0] - 4.0f * rowCurr[0];

                // Interior
                for (int x = 1; x < width - 1; x++) {
                    output[rowOff + x] = rowPrev[x] + rowCurr[x - 1] + rowCurr[x + 1] + rowNext[x] - 4.0f * rowCurr[x];
                }

                // Right border
                int x = width - 1;
                output[rowOff + x] = rowPrev[x] + rowCurr[x - 1] + rowCurr[x] + rowNext[x] - 4.0f * rowCurr[x];
            } else {
                for (int x = 0; x < width; x++) {
                    int ym = std::clamp(y - 1, 0, height - 1);
                    int yp = std::clamp(y + 1, 0, height - 1);
                    int xm = std::clamp(x - 1, 0, width - 1);
                    int xp = std::clamp(x + 1, 0, width - 1);
                    float val = input[static_cast<size_t>(ym) * width + x]
                              + input[static_cast<size_t>(y) * width + xm]
                              + input[static_cast<size_t>(y) * width + xp]
                              + input[static_cast<size_t>(yp) * width + x]
                              - 4.0f * input[rowOff + x];
                    output[rowOff + x] = val;
                }
            }
        }
    }
}

// ---- SAR Speckle Filters ----
// All speckle filters follow the same pattern:
// 1. For each pixel, compute local mean and variance in a window
// 2. Estimate noise characteristics
// 3. Adaptively weight between original pixel and local mean

// Helper: compute local mean and variance for a pixel
// Integral image (summed-area table) for O(1) local statistics
// Handles non-finite (NaN/±Inf) pixels by tracking valid pixel count separately
class IntegralImage {
public:
    IntegralImage(const float *input, int width, int height)
        : m_width(width), m_height(height)
    {
        // Build integral images for sum, sum-of-squares, and valid count.
        // Strides and indexes in size_t (#691): (width+1)*(height+1) and
        // (y+1)*(width+1) overflow int32 at 46341^2 and per-pixel beyond
        // ~2^31 elements (e.g. a 100000x30000 strip at y>21473) — silent
        // corrupt indexing / std::length_error on very large rasters.
        m_rowStride = static_cast<std::size_t>(width) + 1;
        const std::size_t cells = m_rowStride * (static_cast<std::size_t>(height) + 1);
        m_sum.resize(cells, 0.0);
        m_sumSq.resize(cells, 0.0);
        m_count.resize(cells, 0);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float val = input[static_cast<std::size_t>(y) * width + x];
                const std::size_t idx = (static_cast<std::size_t>(y) + 1) * m_rowStride + (x + 1);
                const std::size_t idxLeft = (static_cast<std::size_t>(y) + 1) * m_rowStride + x;
                const std::size_t idxUp = static_cast<std::size_t>(y) * m_rowStride + (x + 1);
                const std::size_t idxDiag = static_cast<std::size_t>(y) * m_rowStride + x;

                if (std::isfinite(val)) {
                    m_sum[idx] = m_sum[idxLeft] + m_sum[idxUp] - m_sum[idxDiag] + val;
                    m_sumSq[idx] = m_sumSq[idxLeft] + m_sumSq[idxUp] - m_sumSq[idxDiag] + val * val;
                    m_count[idx] = m_count[idxLeft] + m_count[idxUp] - m_count[idxDiag] + 1;
                } else {
                    // NaN and ±Inf are both excluded: one +Inf would otherwise
                    // poison every downstream window of the summed-area table.
                    m_sum[idx] = m_sum[idxLeft] + m_sum[idxUp] - m_sum[idxDiag];
                    m_sumSq[idx] = m_sumSq[idxLeft] + m_sumSq[idxUp] - m_sumSq[idxDiag];
                    m_count[idx] = m_count[idxLeft] + m_count[idxUp] - m_count[idxDiag];
                }
            }
        }
    }

    void computeRegion(int x1, int y1, int x2, int y2,
                       double &sum, double &sumSq, qint64 &count) const
    {
        // Clamp to valid pixel range
        x1 = std::clamp(x1, 0, m_width - 1);
        y1 = std::clamp(y1, 0, m_height - 1);
        x2 = std::clamp(x2, 0, m_width - 1);
        y2 = std::clamp(y2, 0, m_height - 1);

        // Integral image uses 1-indexed coordinates (size_t stride, #691)
        const std::size_t r1 = static_cast<std::size_t>(y1);
        const std::size_t r2 = static_cast<std::size_t>(y2) + 1;
        const std::size_t c1 = static_cast<std::size_t>(x1);
        const std::size_t c2 = static_cast<std::size_t>(x2) + 1;

        sum = m_sum[r2 * m_rowStride + c2]
            - m_sum[r1 * m_rowStride + c2]
            - m_sum[r2 * m_rowStride + c1]
            + m_sum[r1 * m_rowStride + c1];

        sumSq = m_sumSq[r2 * m_rowStride + c2]
              - m_sumSq[r1 * m_rowStride + c2]
              - m_sumSq[r2 * m_rowStride + c1]
              + m_sumSq[r1 * m_rowStride + c1];

        count = m_count[r2 * m_rowStride + c2]
              - m_count[r1 * m_rowStride + c2]
              - m_count[r2 * m_rowStride + c1]
              + m_count[r1 * m_rowStride + c1];
    }

private:
    int m_width, m_height;
    std::size_t m_rowStride = 0;
    std::vector<double> m_sum;
    std::vector<double> m_sumSq;
    std::vector<qint64> m_count;
};

static void localStats(const IntegralImage &integral, int width, int height,
                       int cx, int cy, int kernelSize,
                       float &mean, float &variance)
{
    int half = kernelSize / 2;
    // Clamp window to valid pixel range
    int x1 = std::clamp(cx - half, 0, width - 1);
    int y1 = std::clamp(cy - half, 0, height - 1);
    int x2 = std::clamp(cx + half, 0, width - 1);
    int y2 = std::clamp(cy + half, 0, height - 1);

    double sum, sumSq;
    qint64 count;
    integral.computeRegion(x1, y1, x2, y2, sum, sumSq, count);
    if (count <= 0) {
        mean = 0.0f;
        variance = 0.0f;
        return;
    }
    mean = static_cast<float>(sum / count);
    variance = static_cast<float>(sumSq / count - mean * mean);
    if (variance < 0.0f) variance = 0.0f;
}

void ImageEnhancement::leeFilter(const float *input, float *output,
                                  int width, int height,
                                  int kernelSize, float noiseVariance)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "leeFilter: null pointer argument");
        return;
    }
    if (noiseVariance < 0.0f) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "leeFilter: noiseVariance must be >= 0");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("leeFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Lee speckle filter: %1x%2, kernel=%3, noiseVar=%4" )
        .arg( width ).arg( height ).arg( kernelSize ).arg( noiseVariance ) );

    // Build integral image for O(1) local statistics
    IntegralImage integral(input, width, height);

    const int half = kernelSize / 2;

    // Use ChunkedProcessor for parallel processing
    ChunkedProcessor processor(width, height, half);
    processor.process([&](const ChunkedProcessor::Chunk &chunk) -> bool {
        for (int y = chunk.startRow; y < chunk.endRow; y++) {
            for (int x = 0; x < width; x++) {
                float pixel = input[y * width + x];

                // Preserve NaN center pixels
                if (std::isnan(pixel)) {
                    output[y * width + x] = pixel;
                    continue;
                }

                float mean, localVar;
                localStats(integral, width, height, x, y, kernelSize, mean, localVar);

                if (localVar <= 0.0f || mean <= 0.0f) {
                    output[y * width + x] = mean;
                } else {
                    float cuSq = noiseVariance;
                    float clSq = localVar / (mean * mean);
                    // Lee weight is (1 - Cu^2/Ci^2); the Kuan filter adds
                    // the extra 1/(1+Cu^2) factor (see :978) — the previous
                    // copy here made Lee ≡ Kuan (#678).
                    float weight = (clSq <= cuSq) ? 0.0f : 1.0f - cuSq / clSq;
                    output[y * width + x] = mean + weight * (pixel - mean);
                }
            }
        }
        return true;
    });
}

void ImageEnhancement::enhancedLeeFilter(const float *input, float *output,
                                          int width, int height,
                                          int kernelSize, float noiseVariance, float damping)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "enhancedLeeFilter: null pointer argument");
        return;
    }
    if (noiseVariance < 0.0f) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "enhancedLeeFilter: noiseVariance must be >= 0");
        return;
    }
    if (damping < 0.0f) {
        damping = 1.0f;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("enhancedLeeFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Enhanced Lee speckle filter: %1x%2, kernel=%3, noiseVar=%4, damping=%5" )
        .arg( width ).arg( height ).arg( kernelSize ).arg( noiseVariance ).arg( damping ) );

    // Build integral image for O(1) local statistics
    IntegralImage integral(input, width, height);

    const int half = kernelSize / 2;
    const float cu = std::sqrt(noiseVariance);
    const float cmax = std::sqrt(1.0f + 2.0f * noiseVariance);

    // Use ChunkedProcessor for parallel processing
    ChunkedProcessor processor(width, height, half);
    processor.process([&](const ChunkedProcessor::Chunk &chunk) -> bool {
        for (int y = chunk.startRow; y < chunk.endRow; y++) {
            for (int x = 0; x < width; x++) {
                float pixel = input[y * width + x];

                // Preserve NaN center pixels
                if (std::isnan(pixel)) {
                    output[y * width + x] = pixel;
                    continue;
                }

                float mean, localVar;
                localStats(integral, width, height, x, y, kernelSize, mean, localVar);

                if (localVar <= 0.0f || mean <= 0.0f) {
                    output[y * width + x] = mean;
                    continue;
                }

                float cl = std::sqrt(localVar) / mean;

                if (cl <= cu) {
                    // Homogeneous region -> mean
                    output[y * width + x] = mean;
                } else if (cl >= cmax) {
                    // Point target / strong edge -> keep pixel
                    output[y * width + x] = pixel;
                } else {
                    // Heterogeneous region -> adaptive exponential weighting
                    float denom = cmax - cl;
                    float weight = (denom > 1e-6f) ? std::exp(-damping * (cl - cu) / denom) : 0.0f;
                    output[y * width + x] = mean * weight + pixel * (1.0f - weight);
                }
            }
        }
        return true;
    });
}

void ImageEnhancement::frostFilter(const float *input, float *output,
                                    int width, int height,
                                    int kernelSize, float damping)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "frostFilter: null pointer argument");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("frostFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Frost speckle filter: %1x%2, kernel=%3, damping=%4" )
        .arg( width ).arg( height ).arg( kernelSize ).arg( damping ) );
    // Frost filter: exponentially weighted adaptive filter
    // weight = exp(-damping * distance * localVar / mean^2)

    int half = kernelSize / 2;

    // Build integral image for O(1) local statistics
    IntegralImage integral(input, width, height);

    // Precompute distance lookup table (only depends on kernel geometry)
    std::vector<float> distLut(kernelSize * kernelSize);
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            int idx = (dy + half) * kernelSize + (dx + half);
            distLut[idx] = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        }
    }

    // Use ChunkedProcessor for parallel processing
    ChunkedProcessor processor(width, height, half);
    processor.process([&](const ChunkedProcessor::Chunk &chunk) -> bool {
        for (int y = chunk.startRow; y < chunk.endRow; y++) {
            for (int x = 0; x < width; x++) {
                float pixel = input[y * width + x];

                // Preserve NaN center pixels
                if (std::isnan(pixel)) {
                    output[y * width + x] = pixel;
                    continue;
                }

                float mean, localVar;
                localStats(integral, width, height, x, y, kernelSize, mean, localVar);

                // Compute coefficient of variation squared
                float cvSq = (mean > 0.0f) ? (localVar / (mean * mean)) : 0.0f;

                double sumWeighted = 0.0;
                double sumWeight = 0.0;

                for (int dy = -half; dy <= half; dy++) {
                    for (int dx = -half; dx <= half; dx++) {
                        int nx = std::clamp(x + dx, 0, width - 1);
                        int ny = std::clamp(y + dy, 0, height - 1);
                        float neighbor = input[ny * width + nx];

                        // Skip NaN neighbors
                    if (std::isnan(neighbor)) continue;

                    // Use precomputed distance
                    float dist = distLut[(dy + half) * kernelSize + (dx + half)];

                    // Weight: exp(-damping * distance * CV^2)
                    float weight = std::exp(-damping * dist * cvSq);

                    sumWeighted += weight * neighbor;
                    sumWeight += weight;
                }
            }

            output[y * width + x] = (sumWeight > 0.0)
                ? static_cast<float>(sumWeighted / sumWeight)
                : mean;
            }
        }
        return true;
    });
}

void ImageEnhancement::kuanFilter(const float *input, float *output,
                                   int width, int height,
                                   int kernelSize, float noiseVariance)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "kuanFilter: null pointer argument");
        return;
    }
    if (noiseVariance < 0.0f) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "kuanFilter: noiseVariance must be >= 0");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("kuanFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    // Kuan filter: similar to Lee but with different weighting

    // Build integral image for O(1) local statistics
    IntegralImage integral(input, width, height);

    const int half = kernelSize / 2;

    // Use ChunkedProcessor for parallel processing
    ChunkedProcessor processor(width, height, half);
    processor.process([&](const ChunkedProcessor::Chunk &chunk) -> bool {
        for (int y = chunk.startRow; y < chunk.endRow; y++) {
            for (int x = 0; x < width; x++) {
                float pixel = input[y * width + x];

                // Preserve NaN center pixels
                if (std::isnan(pixel)) {
                    output[y * width + x] = pixel;
                    continue;
                }

                float mean, localVar;
                localStats(integral, width, height, x, y, kernelSize, mean, localVar);

                if (localVar <= 0.0f || mean <= 0.0f) {
                    output[y * width + x] = mean;
                    continue;
                }

                // Cu^2: noise variance normalized by mean^2
                // For multiplicative noise model: Cu^2 = noiseVariance (given as variance of speckle)
                float cuSq = noiseVariance;

                // Cl^2: local coefficient of variation squared
                float clSq = localVar / (mean * mean);

                float weight;
                if (clSq <= cuSq) {
                    // Local variation less than noise → full smoothing
                    weight = 0.0f;
                } else {
                // Kuan keeps the extra 1/(1+Cu^2) factor that Lee drops
                // (#678 — a shared perl edit stripped it from BOTH filters).
                weight = ( 1.0f - cuSq / clSq ) / ( 1.0f + cuSq );
            }

            output[y * width + x] = mean + weight * (pixel - mean);
            }
        }
        return true;
    });
}

void ImageEnhancement::gammaMapFilter(const float *input, float *output,
                                       int width, int height,
                                       int kernelSize, float noiseVariance)
{
    if (!input || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "gammaMapFilter: null pointer argument");
        return;
    }
    if (noiseVariance < 0.0f) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "gammaMapFilter: noiseVariance must be >= 0");
        return;
    }
    if (width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("gammaMapFilter: invalid dimensions %1x%2").arg(width).arg(height));
        return;
    }
    QString error;
    if (!InputValidator::validateKernelSize(kernelSize, error)) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return;
    }
    // Gamma-MAP filter: Maximum A Posteriori for Gamma distributed speckle

    // Build integral image for O(1) local statistics
    IntegralImage integral(input, width, height);

    const int half = kernelSize / 2;

    // Use ChunkedProcessor for parallel processing
    ChunkedProcessor processor(width, height, half);
    processor.process([&](const ChunkedProcessor::Chunk &chunk) -> bool {
        for (int y = chunk.startRow; y < chunk.endRow; y++) {
            for (int x = 0; x < width; x++) {
                float pixel = input[y * width + x];

                // Preserve NaN center pixels
                if (std::isnan(pixel)) {
                    output[y * width + x] = pixel;
                    continue;
                }

                float mean, localVar;
                localStats(integral, width, height, x, y, kernelSize, mean, localVar);

                if (localVar <= 0.0f || mean <= 0.0f) {
                    output[y * width + x] = mean;
                    continue;
                }

                float cuSq = noiseVariance;
                float clSq = localVar / (mean * mean);

                if (clSq <= cuSq) {
                    // Homogeneous region — smooth fully
                    output[y * width + x] = mean;
                } else {
                    // Heterogeneous region — preserve structure
                    float alpha = (1.0f + cuSq) / (clSq - cuSq);
                    if (alpha <= 1e-6f) {
                        // Extreme variance / point target -> preserve original pixel
                        output[y * width + x] = pixel;
                    } else {
                        // MAP estimate
                        float a = alpha;
                        float b = (alpha - 1.0f) * mean;
                        float discriminant = b * b + 4.0f * a * pixel;
                        if (discriminant < 0.0f) discriminant = 0.0f;
                        output[y * width + x] = (b + std::sqrt(discriminant)) / (2.0f * a);
                    }
                }
            }
        }
        return true;
    });
}

// ---- Band ratio ----

void ImageEnhancement::bandRatio(const float *band1, const float *band2,
                                  float *output, size_t count)
{
    if (!band1 || !band2 || !output) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "bandRatio: null pointer argument");
        return;
    }
    if (count == 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "bandRatio: zero pixel count");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        output[i] = MathUtils::safeDiv(band1[i], band2[i]);
    }
}

// ---- IHS transform ----
// Cylindrical intensity-hue-saturation model.
// Basis: u = (2R - G - B) / sqrt(6), v = (G - B) / sqrt(2)
// I = (R + G + B) / 3
// H = atan2(v, u) / (2*pi), mapped to [0, 1)
// S = sqrt(u^2 + v^2) / (3*I)  (when I > 0, else 0)
//
// This cylindrical S is NOT the hexcone formula 1 - min(R,G,B)/I — they
// coincide only at the extremes (e.g. R=1,G=0,B=0 gives S=0.8165 here but
// 1 - min/I = 1). S spans [0, 1] and is mathematically consistent with the
// basis vectors, allowing exact round-trip reconstruction.

void ImageEnhancement::rgbToIhs(float r, float g, float b,
                                  float &i, float &h, float &s)
{
    static constexpr float inv3 = 1.0f / 3.0f;
    static constexpr float sqrt6 = 2.449489742783178f;   // std::sqrt(6.0f)
    static constexpr float sqrt2 = 1.4142135623730951f;   // std::sqrt(2.0f)
    static constexpr float inv2Pi = 1.0f / (2.0f * 3.14159265358979323846f);
    static constexpr float inv3sqrt6 = 1.0f / (3.0f * 2.449489742783178f); // 1/(3*sqrt(6))

    i = (r + g + b) * inv3;

    if (i == 0.0f) {
        h = 0.0f;
        s = 0.0f;
        return;
    }

    float u = (2.0f * r - g - b) / sqrt6;
    float v = (g - b) / sqrt2;

    // S = sqrt(u^2 + v^2) / (3*I) — cylindrical saturation (see block comment:
    // deliberately not the hexcone 1 - min/I form)
    float chroma = std::sqrt(u * u + v * v);
    s = chroma * inv3 / i;

    h = std::atan2(v, u) * inv2Pi;
    if (h < 0.0f) h += 1.0f;
}

void ImageEnhancement::ihsToRgb(float i, float h, float s,
                                  float &r, float &g, float &b)
{
    if (s == 0.0f || i == 0.0f) {
        r = g = b = i;
        return;
    }

    static constexpr float sqrt6 = 2.449489742783178f;   // std::sqrt(6.0f)
    static constexpr float sqrt2 = 1.4142135623730951f;   // std::sqrt(2.0f)
    static constexpr float twoPi = 2.0f * 3.14159265358979323846f;

    float hRad = h * twoPi;
    // chroma = S * 3 * I (to invert the S = chroma/(3*I) definition)
    float chroma = s * 3.0f * i;
    float u = chroma * std::cos(hRad);
    float v = chroma * std::sin(hRad);

    // Inverse of the forward basis transform:
    // u = (2R - G - B) / sqrt(6)  ->  R = I + u * 2/sqrt(6) + 0
    // v = (G - B) / sqrt(2)       ->  G = I - u/sqrt(6) + v/sqrt(2)
    //                                 B = I - u/sqrt(6) - v/sqrt(2)
    r = i + u * 2.0f / sqrt6;
    g = i - u / sqrt6 + v / sqrt2;
    b = i - u / sqrt6 - v / sqrt2;
}

// ---- PCA ----

void ImageEnhancement::computeCovarianceMatrix(const std::vector<std::vector<float>> &centered,
                                                 int bands, size_t n,
                                                 std::vector<std::vector<float>> &cov)
{
    cov.assign(bands, std::vector<float>(bands, 0.0f));
    if (bands <= 0 || n == 0)
        return;

    // Cache-friendly pixel-outer accumulation: loads each pixel's spectrum once.
    std::vector<std::vector<double>> sum(bands, std::vector<double>(bands, 0.0));
    std::vector<std::vector<size_t>> validCount(bands, std::vector<size_t>(bands, 0));
    std::vector<float> pVal(bands);

    for (size_t k = 0; k < n; ++k) {
        for (int b = 0; b < bands; ++b)
            pVal[b] = centered[b][k];

        for (int i = 0; i < bands; ++i) {
            const float vi = pVal[i];
            if (std::isnan(vi))
                continue;
            const double dvi = static_cast<double>(vi);
            for (int j = i; j < bands; ++j) {
                const float vj = pVal[j];
                if (std::isnan(vj))
                    continue;
                sum[i][j] += dvi * static_cast<double>(vj);
                validCount[i][j]++;
            }
        }
    }

    for (int i = 0; i < bands; ++i) {
        for (int j = i; j < bands; ++j) {
            const size_t vc = validCount[i][j];
            const double divisor = (vc > 1) ? static_cast<double>(vc - 1) : 1.0;
            const float val = static_cast<float>(sum[i][j] / divisor);
            cov[i][j] = val;
            cov[j][i] = val;
        }
    }
}

void ImageEnhancement::jacobiEigen(std::vector<std::vector<float>> &A, int n,
                                    std::vector<float> &eigenvalues,
                                    std::vector<std::vector<float>> &eigenvectors)
{
    // Initialize eigenvectors as identity matrix
    eigenvectors.assign(n, std::vector<float>(n, 0.0f));
    for (int i = 0; i < n; i++)
        eigenvectors[i][i] = 1.0f;

    eigenvalues.assign(n, 0.0f);
    if (n <= 0)
        return;
    if (n == 1) {
        eigenvalues[0] = A[0][0];
        return;
    }

    // Classical cyclic Jacobi (#670): full sweeps of Givens rotations with a
    // RELATIVE convergence test. The previous solver performed at most 200
    // individual rotations with an absolute 1e-10 tolerance on covariance
    // entries — one classical sweep alone needs n(n-1)/2 rotations (a
    // 224-band hyperspectral cube needs ~25k per sweep), and the absolute
    // tolerance is unreachable for DN-scale entries (10^4..10^6), so PCA/MNF
    // silently returned partial-rotation components for >~15 bands.
    const int maxSweeps = 60;
    const float eps = std::numeric_limits<float>::epsilon();

    for (int sweep = 0; sweep < maxSweeps; sweep++) {
        float maxOff = 0.0f;
        float maxDiag = 0.0f;
        for (int i = 0; i < n; i++) {
            maxDiag = std::max(maxDiag, std::abs(A[i][i]));
            for (int j = i + 1; j < n; j++)
                maxOff = std::max(maxOff, std::abs(A[i][j]));
        }

        // Relative test: converged when the off-diagonal energy is at the
        // float rounding level of the diagonal magnitude.
        if (maxOff <= eps * std::max(maxDiag, 1.0f))
            break;

        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                const float apq = A[p][q];
                // Skip numerically-already-zero pairs: rotating them would
                // only add rounding noise.
                if (std::abs(apq) <= eps * std::max(std::abs(A[p][p]), std::abs(A[q][q])))
                    continue;

                const float theta = 0.5f * std::atan2(2.0f * apq, A[q][q] - A[p][p]);
                const float c = std::cos(theta);
                const float s = std::sin(theta);

                // A' = G^T * A * G: update rows/cols p and q.
                const float app = A[p][p];
                const float aqq = A[q][q];
                const float newApp = c * c * app + s * s * aqq - 2.0f * s * c * apq;
                const float newAqq = s * s * app + c * c * aqq + 2.0f * s * c * apq;

                for (int r = 0; r < n; r++) {
                    if (r == p || r == q) continue;
                    const float arp = A[r][p];
                    const float arq = A[r][q];
                    A[r][p] = c * arp - s * arq;
                    A[p][r] = A[r][p];
                    A[r][q] = s * arp + c * arq;
                    A[q][r] = A[r][q];
                }

                A[p][p] = newApp;
                A[q][q] = newAqq;
                A[p][q] = 0.0f; // by construction
                A[q][p] = 0.0f;

                // Accumulate eigenvectors: V' = V * G.
                for (int r = 0; r < n; r++) {
                    const float erp = eigenvectors[r][p];
                    const float erq = eigenvectors[r][q];
                    eigenvectors[r][p] = c * erp - s * erq;
                    eigenvectors[r][q] = s * erp + c * erq;
                }
            }
        }
    }

    // Extract eigenvalues from diagonal
    for (int i = 0; i < n; i++)
        eigenvalues[i] = A[i][i];
}

ImageEnhancement::PcaResult ImageEnhancement::pca(
    const std::vector<std::vector<float>> &input, int numComponents)
{
    int bands = static_cast<int>(input.size());
    if (bands == 0 || input[0].empty()) {
        SICNU_LOG_ERROR( SicnuLogTags::Algorithms, "PCA: empty input data" );
        return PcaResult{};
    }

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "PCA decomposition: %1 bands, %2 components" )
        .arg( bands ).arg( numComponents ) );
    size_t n = input[0].size();

    // Clamp requested components
    if (numComponents > bands)
        numComponents = bands;
    if (numComponents <= 0)
        numComponents = bands;

    // Step 1: Compute mean per band and center data. NaN pixels are excluded
    // from the mean so a single invalid pixel cannot corrupt the covariance.
    std::vector<float> means(bands, 0.0f);
    for (int b = 0; b < bands; b++) {
        double sum = 0.0;
        size_t valid = 0;
        for (size_t k = 0; k < n; k++) {
            if (std::isnan(input[b][k]))
                continue;
            sum += input[b][k];
            ++valid;
        }
        means[b] = valid > 0 ? static_cast<float>(sum / valid) : 0.0f;
    }

    std::vector<std::vector<float>> centered(bands, std::vector<float>(n));
    for (int b = 0; b < bands; b++) {
        for (size_t k = 0; k < n; k++)
            centered[b][k] = input[b][k] - means[b];
    }

    // Step 2: Compute covariance matrix
    std::vector<std::vector<float>> cov;
    computeCovarianceMatrix(centered, bands, n, cov);

    // Step 3: Eigen decomposition
    std::vector<float> eigenvalues;
    std::vector<std::vector<float>> eigenvectors;
    jacobiEigen(cov, bands, eigenvalues, eigenvectors);

    // Step 4: Sort eigenvalues/eigenvectors in descending order
    std::vector<int> indices(bands);
    for (int i = 0; i < bands; i++) indices[i] = i;
    std::sort(indices.begin(), indices.end(),
              [&](int a, int b) { return eigenvalues[a] > eigenvalues[b]; });

    std::vector<float> sortedEigen(bands);
    std::vector<std::vector<float>> sortedVectors(bands, std::vector<float>(bands));
    for (int i = 0; i < bands; i++) {
        sortedEigen[i] = eigenvalues[indices[i]];
        for (int b = 0; b < bands; b++)
            sortedVectors[i][b] = eigenvectors[b][indices[i]];
    }

    // Step 5: Compute total variance and explained variance ratio
    double totalVar = 0.0;
    for (int i = 0; i < bands; i++)
        totalVar += std::max(0.0, static_cast<double>(sortedEigen[i]));

    PcaResult result;
    result.explainedVariance.resize(numComponents);
    for (int i = 0; i < numComponents; i++) {
        result.explainedVariance[i] = (totalVar > 0.0)
            ? static_cast<float>(std::max(0.0, static_cast<double>(sortedEigen[i])) / totalVar)
            : 0.0f;
    }

    // Store eigenvectors for the requested components
    result.eigenvectors.resize(numComponents, std::vector<float>(bands));
    for (int i = 0; i < numComponents; i++) {
        result.eigenvectors[i] = sortedVectors[i];
    }

    // Step 6: Project centered data onto top-K eigenvectors
    result.output.resize(numComponents, std::vector<float>(n));
    for (int comp = 0; comp < numComponents; comp++) {
        for (size_t k = 0; k < n; k++) {
            float val = 0.0f;
            for (int b = 0; b < bands; b++)
                val += sortedVectors[comp][b] * centered[b][k];
            result.output[comp][k] = val;
        }
    }

    return result;
}

ImageEnhancement::MnfResult ImageEnhancement::mnf(
    const std::vector<std::vector<float>> &input, int numComponents)
{
    const int bands = static_cast<int>(input.size());
    if (bands == 0 || input[0].empty()) {
        SICNU_LOG_ERROR( SicnuLogTags::Algorithms, "MNF: empty input data" );
        return MnfResult{};
    }
    const size_t n = input[0].size();
    if (n < 2) {
        SICNU_LOG_ERROR( SicnuLogTags::Algorithms, "MNF: need at least 2 pixels" );
        return MnfResult{};
    }
    if (numComponents <= 0 || numComponents > bands)
        numComponents = bands;

    // 1. Mean-center the data (skipping NaNs).
    std::vector<float> means(bands, 0.0f);
    for (int b = 0; b < bands; b++) {
        double sum = 0.0;
        size_t validCount = 0;
        for (size_t k = 0; k < n; k++) {
            if (!std::isnan(input[b][k])) {
                sum += input[b][k];
                validCount++;
            }
        }
        means[b] = (validCount > 0) ? static_cast<float>(sum / validCount) : 0.0f;
    }
    std::vector<std::vector<float>> centered(bands, std::vector<float>(n));
    for (int b = 0; b < bands; b++)
        for (size_t k = 0; k < n; k++)
            centered[b][k] = input[b][k] - means[b];

    // 2. Noise covariance estimated from lagged (shift) differences.
    std::vector<std::vector<float>> noise(bands, std::vector<float>(n - 1));
    for (int b = 0; b < bands; b++)
        for (size_t k = 0; k + 1 < n; k++)
            noise[b][k] = centered[b][k + 1] - centered[b][k];
    std::vector<std::vector<float>> noiseCov;
    computeCovarianceMatrix(noise, bands, n - 1, noiseCov);
    for (int b = 0; b < bands; b++)
        for (int b2 = 0; b2 < bands; b2++)
            noiseCov[b][b2] /= 2.0f;

    // 3. Eigen-decompose the noise covariance.
    std::vector<float> noiseEigen;
    std::vector<std::vector<float>> noiseVectors; // noiseVectors[band][eigenvector]
    jacobiEigen(noiseCov, bands, noiseEigen, noiseVectors);

    // 4. Whitening transform: column k = noise eigenvector k / sqrt(eigenvalue k).
    std::vector<std::vector<float>> whitening(bands, std::vector<float>(bands, 0.0f));
    for (int b = 0; b < bands; b++)
        for (int k = 0; k < bands; k++) {
            const double dk = std::max(static_cast<double>(noiseEigen[k]), 1e-9);
            whitening[b][k] = static_cast<float>(noiseVectors[b][k] / std::sqrt(dk));
        }

    // 5. Whiten the data: y[k] = sum_b W[b][k] * centered[b].
    std::vector<std::vector<float>> whitened(bands, std::vector<float>(n, 0.0f));
    for (int k = 0; k < bands; k++)
        for (size_t p = 0; p < n; p++) {
            double v = 0.0;
            for (int b = 0; b < bands; b++)
                v += whitening[b][k] * centered[b][p];
            whitened[k][p] = static_cast<float>(v);
        }

    // 6. Covariance of the whitened data; eigen-decomposition orders the
    //    components by signal-to-noise (eigenvalue of the whitened covariance).
    std::vector<std::vector<float>> yCov;
    computeCovarianceMatrix(whitened, bands, n, yCov);
    std::vector<float> yEigen;
    std::vector<std::vector<float>> yVectors; // yVectors[band][eigenvector]
    jacobiEigen(yCov, bands, yEigen, yVectors);

    std::vector<int> indices(bands);
    for (int i = 0; i < bands; i++)
        indices[i] = i;
    std::sort(indices.begin(), indices.end(),
              [&](int a, int b) { return yEigen[a] > yEigen[b]; });

    // 7. MNF components: z_i = sum_k Ey[k][i] * y[k].
    MnfResult result;
    result.output.resize(numComponents, std::vector<float>(n));
    result.signalToNoise.resize(numComponents);
    for (int comp = 0; comp < numComponents; comp++) {
        const int ei = indices[comp];
        result.signalToNoise[comp] = yEigen[ei];
        for (size_t p = 0; p < n; p++) {
            double v = 0.0;
            for (int k = 0; k < bands; k++)
                v += yVectors[k][ei] * whitened[k][p];
            result.output[comp][p] = static_cast<float>(v);
        }
    }
    return result;
}

bool ImageEnhancement::processPcaFile(const QString &sourcePath, const QString &outputPath,
                                      int numComponents, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();

    if (numComponents > bandCount) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Number of components exceeds band count");
        return false;
    }

    const int tileWidth = 512;
    const int tileHeight = 512;

    std::vector<std::pair<bool, double>> bandNoData(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        bool hasNodata = false;
        double nd = srcDataset.bandNoDataValue(b + 1, &hasNodata);
        bandNoData[b] = {hasNodata, nd};
    }

    auto isPixelValid = [&](int b, float val) {
        if (!std::isfinite(val)) return false;
        // Float-space compare: the cast NoData matches large sentinels
        // exactly where a double-space == never would (#444).
        if (bandNoData[b].first && val == static_cast<float>(bandNoData[b].second)) return false;
        return true;
    };

    // Pass 1: Stream tiles to compute band means
    std::vector<double> bandSums(bandCount, 0.0);
    uint64_t validPixelCount = 0;
    std::vector<std::vector<float>> tileBuffers(bandCount, std::vector<float>(tileWidth * tileHeight));

    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const int tileSize = bw * bh;

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int p = 0; p < tileSize; ++p) {
                bool valid = true;
                for (int b = 0; b < bandCount; ++b) {
                    if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                }
                if (!valid) continue;

                validPixelCount++;
                for (int b = 0; b < bandCount; ++b) {
                    bandSums[b] += tileBuffers[b][p];
                }
            }
        }
    }

    if (validPixelCount == 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No valid pixels found for PCA calculation");
        return false;
    }

    std::vector<float> means(bandCount, 0.0f);
    for (int b = 0; b < bandCount; ++b) {
        means[b] = static_cast<float>(bandSums[b] / static_cast<double>(validPixelCount));
    }

    // Pass 2: Stream tiles to compute centered covariance matrix
    std::vector<std::vector<double>> covSum(bandCount, std::vector<double>(bandCount, 0.0));
    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const int tileSize = bw * bh;

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int p = 0; p < tileSize; ++p) {
                bool valid = true;
                for (int b = 0; b < bandCount; ++b) {
                    if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                }
                if (!valid) continue;

                for (int b = 0; b < bandCount; ++b) {
                    const double diff1 = static_cast<double>(tileBuffers[b][p]) - means[b];
                    for (int b2 = b; b2 < bandCount; ++b2) {
                        const double diff2 = static_cast<double>(tileBuffers[b2][p]) - means[b2];
                        covSum[b][b2] += diff1 * diff2;
                    }
                }
            }
        }
    }

    std::vector<std::vector<float>> covMatrix(bandCount, std::vector<float>(bandCount, 0.0f));
    const double N = static_cast<double>(validPixelCount > 1 ? validPixelCount - 1 : 1);
    for (int b = 0; b < bandCount; ++b) {
        for (int b2 = b; b2 < bandCount; ++b2) {
            const float cov = static_cast<float>(covSum[b][b2] / N);
            covMatrix[b][b2] = cov;
            covMatrix[b2][b] = cov;
        }
    }

    std::vector<float> eigenVals;
    std::vector<std::vector<float>> eigenVecs;
    jacobiEigen(covMatrix, bandCount, eigenVals, eigenVecs);

    std::vector<int> indices(bandCount);
    for (int i = 0; i < bandCount; ++i) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return eigenVals[a] > eigenVals[b];
    });

    // Pass 3: Create output GeoTIFF & block-stream transformation
    GdalDatasetWrapper outDataset;
    if (!outDataset.create(outputPath, width, height, numComponents, GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection())) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create output PCA dataset: ") + outDataset.lastError();
        return false;
    }

    for (int comp = 0; comp < numComponents; ++comp)
        outDataset.setBandNoDataValue(comp + 1, std::numeric_limits<float>::quiet_NaN());

    std::vector<float> outTileBuffer(tileWidth * tileHeight);

    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const int tileSize = bw * bh;

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int comp = 0; comp < numComponents; ++comp) {
                const int ei = indices[comp];
                for (int p = 0; p < tileSize; ++p) {
                    bool valid = true;
                    for (int b = 0; b < bandCount; ++b) {
                        if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                    }
                    if (!valid) {
                        outTileBuffer[p] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                    double val = 0.0;
                    for (int b = 0; b < bandCount; ++b) {
                        val += static_cast<double>(eigenVecs[b][ei]) * (static_cast<double>(tileBuffers[b][p]) - means[b]);
                    }
                    outTileBuffer[p] = static_cast<float>(val);
                }

                if (!outDataset.writeBandWindow(comp + 1, x, y, bw, bh, outTileBuffer.data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to write PCA component band window %1 at %2,%3").arg(comp + 1).arg(x).arg(y);
                    return false;
                }
            }
        }
    }

    return true;
}

bool ImageEnhancement::processMnfFile(const QString &sourcePath, const QString &outputPath,
                                      int numComponents, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();

    if (numComponents > bandCount) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Number of components exceeds band count");
        return false;
    }
    if (bandCount < 2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("MNF requires at least 2 bands");
        return false;
    }

    const int tileWidth = 512;
    const int tileHeight = 512;

    std::vector<std::pair<bool, double>> bandNoData(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        bool hasNodata = false;
        double nd = srcDataset.bandNoDataValue(b + 1, &hasNodata);
        bandNoData[b] = {hasNodata, nd};
    }

    auto isPixelValid = [&](int b, float val) {
        if (!std::isfinite(val)) return false;
        // Float-space compare: the cast NoData matches large sentinels
        // exactly where a double-space == never would (#444).
        if (bandNoData[b].first && val == static_cast<float>(bandNoData[b].second)) return false;
        return true;
    };

    // Pass 1: Compute band means
    std::vector<double> bandSums(bandCount, 0.0);
    uint64_t validPixelCount = 0;
    std::vector<std::vector<float>> tileBuffers(bandCount, std::vector<float>(tileWidth * tileHeight));

    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const int tileSize = bw * bh;

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int p = 0; p < tileSize; ++p) {
                bool valid = true;
                for (int b = 0; b < bandCount; ++b) {
                    if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                }
                if (!valid) continue;

                validPixelCount++;
                for (int b = 0; b < bandCount; ++b) {
                    bandSums[b] += tileBuffers[b][p];
                }
            }
        }
    }

    if (validPixelCount == 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No valid pixels found for MNF calculation");
        return false;
    }

    std::vector<float> means(bandCount, 0.0f);
    for (int b = 0; b < bandCount; ++b) {
        means[b] = static_cast<float>(bandSums[b] / static_cast<double>(validPixelCount));
    }

    // Pass 2: Compute global covariance & noise covariance (from horizontal shift differences)
    std::vector<std::vector<double>> covSum(bandCount, std::vector<double>(bandCount, 0.0));
    std::vector<std::vector<double>> noiseSum(bandCount, std::vector<double>(bandCount, 0.0));
    uint64_t noiseCount = 0;

    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int r = 0; r < bh; ++r) {
                for (int c = 0; c < bw; ++c) {
                    const int p = r * bw + c;
                    bool valid = true;
                    for (int b = 0; b < bandCount; ++b) {
                        if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                    }
                    if (!valid) continue;

                    for (int b = 0; b < bandCount; ++b) {
                        const double diff1 = static_cast<double>(tileBuffers[b][p]) - means[b];
                        for (int b2 = b; b2 < bandCount; ++b2) {
                            const double diff2 = static_cast<double>(tileBuffers[b2][p]) - means[b2];
                            covSum[b][b2] += diff1 * diff2;
                        }
                    }

                    // Noise estimate from adjacent pixel difference
                    if (c + 1 < bw) {
                        const int pNext = r * bw + (c + 1);
                        bool validNext = true;
                        for (int b = 0; b < bandCount; ++b) {
                            if (!isPixelValid(b, tileBuffers[b][pNext])) { validNext = false; break; }
                        }
                        if (validNext) {
                            noiseCount++;
                            for (int b = 0; b < bandCount; ++b) {
                                const double n1 = static_cast<double>(tileBuffers[b][pNext]) - static_cast<double>(tileBuffers[b][p]);
                                for (int b2 = b; b2 < bandCount; ++b2) {
                                    const double n2 = static_cast<double>(tileBuffers[b2][pNext]) - static_cast<double>(tileBuffers[b2][p]);
                                    noiseSum[b][b2] += n1 * n2;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::vector<std::vector<float>> covMatrix(bandCount, std::vector<float>(bandCount, 0.0f));
    std::vector<std::vector<float>> noiseCov(bandCount, std::vector<float>(bandCount, 0.0f));
    const double N = static_cast<double>(validPixelCount > 1 ? validPixelCount - 1 : 1);
    const double Nnoise = static_cast<double>(noiseCount > 1 ? noiseCount - 1 : 1);

    for (int b = 0; b < bandCount; ++b) {
        for (int b2 = b; b2 < bandCount; ++b2) {
            float c = static_cast<float>(covSum[b][b2] / N);
            covMatrix[b][b2] = c;
            covMatrix[b2][b] = c;

            float nc = static_cast<float>(noiseSum[b][b2] / (2.0 * Nnoise));
            noiseCov[b][b2] = nc;
            noiseCov[b2][b] = nc;
        }
    }

    // Step 1: Eigen-decompose noise covariance matrix
    std::vector<float> noiseEigen;
    std::vector<std::vector<float>> noiseVectors;
    jacobiEigen(noiseCov, bandCount, noiseEigen, noiseVectors);

    // Whitening matrix W: column k = noise vector k / sqrt(max(noiseEigen[k], 1e-9))
    std::vector<std::vector<double>> W(bandCount, std::vector<double>(bandCount, 0.0));
    for (int b = 0; b < bandCount; ++b) {
        for (int k = 0; k < bandCount; ++k) {
            const double dk = std::max(static_cast<double>(noiseEigen[k]), 1e-9);
            W[b][k] = static_cast<double>(noiseVectors[b][k]) / std::sqrt(dk);
        }
    }

    // Step 2: Compute whitened data covariance: C_Y = W^T * C_data * W
    // 2-stage O(B^3) matrix multiplication: T = C_data * W, then C_Y = W^T * T
    std::vector<std::vector<double>> T(bandCount, std::vector<double>(bandCount, 0.0));
    for (int r = 0; r < bandCount; ++r) {
        for (int c = 0; c < bandCount; ++c) {
            double sum = 0.0;
            for (int k = 0; k < bandCount; ++k)
                sum += static_cast<double>(covMatrix[r][k]) * W[k][c];
            T[r][c] = sum;
        }
    }

    std::vector<std::vector<float>> yCov(bandCount, std::vector<float>(bandCount, 0.0f));
    for (int i = 0; i < bandCount; ++i) {
        for (int j = i; j < bandCount; ++j) {
            double sum = 0.0;
            for (int r = 0; r < bandCount; ++r)
                sum += W[r][i] * T[r][j];
            yCov[i][j] = static_cast<float>(sum);
            yCov[j][i] = yCov[i][j];
        }
    }

    // Step 3: Eigen-decompose whitened covariance matrix
    std::vector<float> yEigen;
    std::vector<std::vector<float>> yVectors;
    jacobiEigen(yCov, bandCount, yEigen, yVectors);

    std::vector<int> indices(bandCount);
    for (int i = 0; i < bandCount; ++i) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return yEigen[a] > yEigen[b];
    });

    // Step 4: Final MNF transformation matrix M = W * V_Y (top numComponents)
    std::vector<std::vector<double>> Mtrans(numComponents, std::vector<double>(bandCount, 0.0));
    for (int comp = 0; comp < numComponents; ++comp) {
        const int ei = indices[comp];
        for (int b = 0; b < bandCount; ++b) {
            double sum = 0.0;
            for (int k = 0; k < bandCount; ++k) {
                sum += W[b][k] * yVectors[k][ei];
            }
            Mtrans[comp][b] = sum;
        }
    }

    // Pass 3: Create output GeoTIFF & block-stream transformation
    GdalDatasetWrapper outDataset;
    if (!outDataset.create(outputPath, width, height, numComponents, GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection())) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create output MNF dataset: ") + outDataset.lastError();
        return false;
    }

    for (int comp = 0; comp < numComponents; ++comp)
        outDataset.setBandNoDataValue(comp + 1, std::numeric_limits<float>::quiet_NaN());

    std::vector<float> outTileBuffer(tileWidth * tileHeight);

    for (int y = 0; y < height; y += tileHeight) {
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const int tileSize = bw * bh;

            for (int b = 0; b < bandCount; ++b) {
                if (!srcDataset.readBandWindow(b + 1, x, y, bw, bh, tileBuffers[b].data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to read band window %1 at %2,%3").arg(b + 1).arg(x).arg(y);
                    return false;
                }
            }

            for (int comp = 0; comp < numComponents; ++comp) {
                for (int p = 0; p < tileSize; ++p) {
                    bool valid = true;
                    for (int b = 0; b < bandCount; ++b) {
                        if (!isPixelValid(b, tileBuffers[b][p])) { valid = false; break; }
                    }
                    if (!valid) {
                        outTileBuffer[p] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                    double val = 0.0;
                    for (int b = 0; b < bandCount; ++b) {
                        val += Mtrans[comp][b] * (static_cast<double>(tileBuffers[b][p]) - means[b]);
                    }
                    outTileBuffer[p] = static_cast<float>(val);
                }

                if (!outDataset.writeBandWindow(comp + 1, x, y, bw, bh, outTileBuffer.data())) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("Failed to write MNF component band window %1 at %2,%3").arg(comp + 1).arg(x).arg(y);
                    return false;
                }
            }
        }
    }

    return true;
}
