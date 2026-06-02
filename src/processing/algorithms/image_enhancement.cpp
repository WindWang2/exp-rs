#include "image_enhancement.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

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
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    float lo = mean - k * stddev;
    float hi = mean + k * stddev;

    linearStretch(input, output, count, lo, hi, nodata);
}

void ImageEnhancement::histogramEqualize(const float *input, float *output, size_t count,
                                         int bins, float nodata)
{
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    if (min == max) {
        for (size_t i = 0; i < count; i++)
            output[i] = (input[i] == nodata) ? nodata : 128.0f;
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

void ImageEnhancement::convolve(const float *input, float *output, int width, int height,
                                const float *kernel, int kernelSize)
{
    int half = kernelSize / 2;

    for (int y = 0; y < height; y++) {
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

// ---- Public spatial filters ----

void ImageEnhancement::meanFilter(const float *input, float *output, int width, int height, int kernelSize)
{
    int ks = kernelSize;
    std::vector<float> kernel(ks * ks);
    float val = 1.0f / static_cast<float>(ks * ks);
    std::fill(kernel.begin(), kernel.end(), val);
    convolve(input, output, width, height, kernel.data(), ks);
}

void ImageEnhancement::gaussianFilter(const float *input, float *output, int width, int height, int kernelSize, float sigma)
{
    int ks = kernelSize;
    std::vector<float> kernel(ks * ks);
    generateGaussianKernel(kernel.data(), ks, sigma);
    convolve(input, output, width, height, kernel.data(), ks);
}

void ImageEnhancement::medianFilter(const float *input, float *output, int width, int height, int kernelSize)
{
    int half = kernelSize / 2;
    std::vector<float> neighborhood;
    neighborhood.reserve(kernelSize * kernelSize);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            neighborhood.clear();

            for (int ky = -half; ky <= half; ky++) {
                for (int kx = -half; kx <= half; kx++) {
                    int ix = std::clamp(x + kx, 0, width - 1);
                    int iy = std::clamp(y + ky, 0, height - 1);
                    neighborhood.push_back(input[iy * width + ix]);
                }
            }

            // Find median using nth_element
            size_t mid = neighborhood.size() / 2;
            std::nth_element(neighborhood.begin(), neighborhood.begin() + mid, neighborhood.end());
            output[y * width + x] = neighborhood[mid];
        }
    }
}

void ImageEnhancement::sobelFilter(const float *input, float *output, int width, int height)
{
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
    const float laplacian[9] = {
        0.0f,  1.0f, 0.0f,
        1.0f, -4.0f, 1.0f,
        0.0f,  1.0f, 0.0f
    };

    convolve(input, output, width, height, laplacian, 3);
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
        return PcaResult{};
    }
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
