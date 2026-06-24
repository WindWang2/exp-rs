#include "image_enhancement.h"
#include "chunked_processor.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>
#include <limits>

void ImageEnhancement::computeStats(const float *data, size_t count, float nodata,
                                    float &min, float &max, float &mean, float &stddev)
{
    min = 1e30f;
    max = -1e30f;
    double sum = 0;
    size_t validCount = 0;

    for (size_t i = 0; i < count; i++) {
        if (data[i] == nodata || std::isnan(data[i])) continue;
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
        sum += data[i];
        validCount++;
    }

    if (validCount == 0) {
        mean = 0;
        stddev = 0;
        return;
    }

    mean = static_cast<float>(sum / validCount);

    double sqSum = 0;
    for (size_t i = 0; i < count; i++) {
        if (data[i] == nodata || std::isnan(data[i])) continue;
        double diff = data[i] - mean;
        sqSum += diff * diff;
    }
    stddev = static_cast<float>(std::sqrt(sqSum / validCount));
}

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
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    float lo = mean - k * stddev;
    float hi = mean + k * stddev;

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
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    if (min == max) {
        for (size_t i = 0; i < count; i++)
            output[i] = (input[i] == nodata || std::isnan(input[i])) ? nodata : 128.0f;
        return;
    }

    std::vector<int> hist(bins, 0);
    float binWidth = (max - min) / bins;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) continue;
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        hist[bin]++;
    }

    std::vector<float> cdf(bins);
    size_t validCount = 0;
    for (int i = 0; i < bins; i++) validCount += hist[i];

    // All pixels are nodata — output all nodata
    if (validCount == 0) {
        for (size_t i = 0; i < count; i++) output[i] = nodata;
        return;
    }

    cdf[0] = static_cast<float>(hist[0]) / validCount;
    for (int i = 1; i < bins; i++)
        cdf[i] = cdf[i - 1] + static_cast<float>(hist[i]) / validCount;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) {
            output[i] = nodata;
            continue;
        }
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        output[i] = cdf[bin] * 255.0f;
    }
}

// ---- Spatial filter helpers ----

void ImageEnhancement::generateGaussianKernel(float *kernel, int size, float sigma)
{
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
    // Temporary buffer for horizontal pass
    std::vector<float> temp(width * height);

    // Horizontal pass: input -> temp (row-major, cache-friendly)
    // Process in chunks for better cache locality
    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);
        for (int y = yStart; y < yEnd; y++) {
            for (int x = 0; x < width; x++) {
                float sum = 0.0f;
                for (int k = -half; k <= half; k++) {
                    int ix = std::clamp(x + k, 0, width - 1);
                    sum += input[y * width + ix] * kernel1D[k + half];
                }
                temp[y * width + x] = sum;
            }
        }
    }

    // Vertical pass: temp -> output
    // Process in column chunks for better cache locality
    const int chunkWidth = 64;
    for (int xStart = 0; xStart < width; xStart += chunkWidth) {
        int xEnd = std::min(xStart + chunkWidth, width);
        for (int y = 0; y < height; y++) {
            for (int x = xStart; x < xEnd; x++) {
                float sum = 0.0f;
                for (int k = -half; k <= half; k++) {
                    int iy = std::clamp(y + k, 0, height - 1);
                    sum += temp[iy * width + x] * kernel1D[k + half];
                }
                output[y * width + x] = sum;
            }
        }
    }
}

void ImageEnhancement::convolve(const float *input, float *output, int width, int height,
                                const float *kernel, int kernelSize)
{
    int half = kernelSize / 2;

    // Process in chunks for better cache locality
    const int chunkHeight = 256;
    for (int yStart = 0; yStart < height; yStart += chunkHeight) {
        int yEnd = std::min(yStart + chunkHeight, height);

        for (int y = yStart; y < yEnd; y++) {
            for (int x = 0; x < width; x++) {
                float sum = 0.0f;

                for (int ky = -half; ky <= half; ky++) {
                    for (int kx = -half; kx <= half; kx++) {
                        // Clamp coordinates to image boundaries (replicate padding)
                        int ix = std::clamp(x + kx, 0, width - 1);
                        int iy = std::clamp(y + ky, 0, height - 1);

                        float pixel = input[iy * width + ix];
                        float kVal = kernel[(ky + half) * kernelSize + (kx + half)];
                        sum += pixel * kVal;
                    }
                }

            output[y * width + x] = sum;
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
    std::vector<float> neighborhood(kSize);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = 0;
            for (int ky = -half; ky <= half; ky++) {
                for (int kx = -half; kx <= half; kx++) {
                    int ix = std::clamp(x + kx, 0, width - 1);
                    int iy = std::clamp(y + ky, 0, height - 1);
                    neighborhood[idx++] = input[iy * width + ix];
                }
            }

            // Find median using nth_element
            int mid = kSize / 2;
            std::nth_element(neighborhood.begin(), neighborhood.begin() + mid, neighborhood.begin() + kSize);
            output[y * width + x] = neighborhood[mid];
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
    // Sobel X kernel
    const float sobelX[9] = {
        -1.0f, 0.0f, 1.0f,
        -2.0f, 0.0f, 2.0f,
        -1.0f, 0.0f, 1.0f
    };

    // Sobel Y kernel
    const float sobelY[9] = {
        -1.0f, -2.0f, -1.0f,
         0.0f,  0.0f,  0.0f,
         1.0f,  2.0f,  1.0f
    };

    std::vector<float> gx(width * height);
    std::vector<float> gy(width * height);

    convolve(input, gx.data(), width, height, sobelX, 3);
    convolve(input, gy.data(), width, height, sobelY, 3);

    for (int i = 0; i < width * height; i++) {
        output[i] = std::sqrt(gx[i] * gx[i] + gy[i] * gy[i]);
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
    const float laplacian[9] = {
        0.0f,  1.0f, 0.0f,
        1.0f, -4.0f, 1.0f,
        0.0f,  1.0f, 0.0f
    };

    convolve(input, output, width, height, laplacian, 3);
}

// ---- SAR Speckle Filters ----
// All speckle filters follow the same pattern:
// 1. For each pixel, compute local mean and variance in a window
// 2. Estimate noise characteristics
// 3. Adaptively weight between original pixel and local mean

// Helper: compute local mean and variance for a pixel
// Integral image (summed-area table) for O(1) local statistics
// Handles NaN pixels by tracking valid pixel count separately
class IntegralImage {
public:
    IntegralImage(const float *input, int width, int height)
        : m_width(width), m_height(height)
    {
        // Build integral images for sum, sum-of-squares, and valid count
        m_sum.resize((width + 1) * (height + 1), 0.0);
        m_sumSq.resize((width + 1) * (height + 1), 0.0);
        m_count.resize((width + 1) * (height + 1), 0);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float val = input[y * width + x];
                int idx = (y + 1) * (m_width + 1) + (x + 1);
                int idxLeft = (y + 1) * (m_width + 1) + x;
                int idxUp = y * (m_width + 1) + (x + 1);
                int idxDiag = y * (m_width + 1) + x;

                if (!std::isnan(val)) {
                    m_sum[idx] = m_sum[idxLeft] + m_sum[idxUp] - m_sum[idxDiag] + val;
                    m_sumSq[idx] = m_sumSq[idxLeft] + m_sumSq[idxUp] - m_sumSq[idxDiag] + val * val;
                    m_count[idx] = m_count[idxLeft] + m_count[idxUp] - m_count[idxDiag] + 1;
                } else {
                    m_sum[idx] = m_sum[idxLeft] + m_sum[idxUp] - m_sum[idxDiag];
                    m_sumSq[idx] = m_sumSq[idxLeft] + m_sumSq[idxUp] - m_sumSq[idxDiag];
                    m_count[idx] = m_count[idxLeft] + m_count[idxUp] - m_count[idxDiag];
                }
            }
        }
    }

    void computeRegion(int x1, int y1, int x2, int y2,
                       double &sum, double &sumSq, int &count) const
    {
        // Clamp to valid pixel range
        x1 = std::clamp(x1, 0, m_width - 1);
        y1 = std::clamp(y1, 0, m_height - 1);
        x2 = std::clamp(x2, 0, m_width - 1);
        y2 = std::clamp(y2, 0, m_height - 1);

        // Integral image uses 1-indexed coordinates
        int r1 = y1;
        int r2 = y2 + 1;
        int c1 = x1;
        int c2 = x2 + 1;

        sum = m_sum[r2 * (m_width+1) + c2]
            - m_sum[r1 * (m_width+1) + c2]
            - m_sum[r2 * (m_width+1) + c1]
            + m_sum[r1 * (m_width+1) + c1];

        sumSq = m_sumSq[r2 * (m_width+1) + c2]
              - m_sumSq[r1 * (m_width+1) + c2]
              - m_sumSq[r2 * (m_width+1) + c1]
              + m_sumSq[r1 * (m_width+1) + c1];

        count = m_count[r2 * (m_width+1) + c2]
              - m_count[r1 * (m_width+1) + c2]
              - m_count[r2 * (m_width+1) + c1]
              + m_count[r1 * (m_width+1) + c1];
    }

private:
    int m_width, m_height;
    std::vector<double> m_sum;
    std::vector<double> m_sumSq;
    std::vector<int> m_count;
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
    int count;
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

                if (localVar <= 0.0f) {
                    output[y * width + x] = mean;
                } else {
                    float weight = localVar / (localVar + noiseVariance);
                    output[y * width + x] = mean + weight * (pixel - mean);
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
                weight = (1.0f - cuSq / clSq) / (1.0f + cuSq);
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
                    // Ensure alpha is positive
                    if (alpha < 0.0f) alpha = 0.0f;

                // MAP estimate
                float a = alpha;
                float b = (alpha - 1.0f) * mean; // simplified from (alpha - L - 1)
                float discriminant = b * b + 4.0f * a * pixel;
                if (discriminant < 0.0f) discriminant = 0.0f;
                output[y * width + x] = (b + std::sqrt(discriminant)) / (2.0f * a);
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
        if (band2[i] == 0.0f) {
            output[i] = std::numeric_limits<float>::quiet_NaN();
        } else {
            output[i] = band1[i] / band2[i];
        }
    }
}

// ---- IHS transform ----
// Cylindrical intensity-hue-saturation model.
// Basis: u = (2R - G - B) / sqrt(6), v = (G - B) / sqrt(2)
// I = (R + G + B) / 3
// H = atan2(v, u) / (2*pi), mapped to [0, 1)
// S = sqrt(u^2 + v^2) / (3*I) = 1 - min(R,G,B)/I  (when I > 0, else 0)
//
// This saturation formula gives S in [0, 1] and is mathematically consistent
// with the basis vectors, allowing exact round-trip reconstruction.

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

    // S = sqrt(u^2 + v^2) / (3*I), which equals 1 - min(R,G,B)/I
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
    double divisor = (n > 1) ? static_cast<double>(n - 1) : 1.0;

    for (int i = 0; i < bands; i++) {
        for (int j = i; j < bands; j++) {
            double sum = 0.0;
            for (size_t k = 0; k < n; k++) {
                sum += static_cast<double>(centered[i][k]) * centered[j][k];
            }
            cov[i][j] = static_cast<float>(sum / divisor);
            cov[j][i] = cov[i][j];
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

    const int maxIter = 200;
    const float tolerance = 1e-10f;

    for (int iter = 0; iter < maxIter; iter++) {
        // Find largest off-diagonal element
        float maxOff = 0.0f;
        int p = 0, q = 1;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (std::abs(A[i][j]) > maxOff) {
                    maxOff = std::abs(A[i][j]);
                    p = i;
                    q = j;
                }
            }
        }

        if (maxOff < tolerance)
            break;

        // Compute rotation angle
        float app = A[p][p];
        float aqq = A[q][q];
        float apq = A[p][q];

        float theta;
        if (std::abs(app - aqq) < 1e-15f) {
            theta = static_cast<float>(M_PI / 4.0);
        } else {
            theta = 0.5f * std::atan2(2.0f * apq, app - aqq);
        }

        float c = std::cos(theta);
        float s = std::sin(theta);

        // Compute new matrix elements: A' = G^T * A * G
        // Only update rows/cols p and q (others unchanged)
        float newApp = c * c * app + s * s * aqq - 2.0f * s * c * apq;
        float newAqq = s * s * app + c * c * aqq + 2.0f * s * c * apq;
        float newApq = 0.0f; // By construction

        // Update off-diagonal elements in rows/cols p and q
        for (int r = 0; r < n; r++) {
            if (r == p || r == q) continue;
            float arp = A[r][p];
            float arq = A[r][q];
            A[r][p] = c * arp - s * arq;
            A[p][r] = A[r][p];
            A[r][q] = s * arp + c * arq;
            A[q][r] = A[r][q];
        }

        A[p][p] = newApp;
        A[q][q] = newAqq;
        A[p][q] = newApq;
        A[q][p] = newApq;

        // Update eigenvectors
        for (int r = 0; r < n; r++) {
            float erp = eigenvectors[r][p];
            float erq = eigenvectors[r][q];
            eigenvectors[r][p] = c * erp - s * erq;
            eigenvectors[r][q] = s * erp + c * erq;
        }
    }

    // Extract eigenvalues from diagonal
    eigenvalues.resize(n);
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

    // Step 1: Compute mean per band and center data
    std::vector<float> means(bands, 0.0f);
    for (int b = 0; b < bands; b++) {
        double sum = 0.0;
        for (size_t k = 0; k < n; k++)
            sum += input[b][k];
        means[b] = static_cast<float>(sum / n);
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
