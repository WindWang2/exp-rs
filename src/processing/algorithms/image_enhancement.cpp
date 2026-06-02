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
