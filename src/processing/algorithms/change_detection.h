// src/processing/algorithms/change_detection.h
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>

namespace ChangeDetection
{

struct ChangeStats {
    size_t count = 0;
    float mean = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    float stddev = 0.0f;
};

bool difference(const float *before, const float *after, float *out, size_t count);
bool normalizedDifference(const float *before, const float *after, float *out, size_t count);
bool changeMask(const float *diff, uint8_t *mask, size_t count, float threshold);
ChangeStats statistics(const float *diff, size_t count);

/**
 * Ratio change: out[i] = after[i] / before[i]. Pixels where before == 0
 * become NaN (guarded, matching the NaN convention of the other kernels).
 */
bool ratio(const float *before, const float *after, float *out, size_t count);

/**
 * Change Vector Analysis magnitude across @p bandCount bands:
 * magnitude = sqrt( sum_b (after_b - before_b)^2 ). A NaN in any band of a
 * pixel propagates to that pixel's magnitude.
 * @param beforeBands / afterBands  arrays of @p bandCount buffers, each @p pixels floats
 */
bool cvaMagnitude(const float *const *beforeBands, const float *const *afterBands,
                  int bandCount, size_t pixels, float *out,
                  QString *errorMessage = nullptr);

/**
 * Otsu's between-class variance threshold over the finite values of @p values.
 * @return true and the threshold when at least two distinct values exist;
 *         false for null/empty/all-NaN input.
 */
bool otsuThreshold(const float *values, size_t count, float *threshold, int bins = 256);

/**
 * Nearest-rank percentile threshold (0..100) over the finite values.
 * @return true and the threshold when at least one finite value exists.
 */
bool percentileThreshold(const float *values, size_t count, float percentile,
                         float *threshold);

/// Morphological operations for mask cleanup.
enum class MorphOp {
    None = 0,
    Erode,  ///< shrink changed areas (removes isolated noise)
    Dilate, ///< grow changed areas (fills holes)
    Open,   ///< erode then dilate
    Close,  ///< dilate then erode
};

/**
 * Morphological cleanup of a 0/1 change mask (255 = NoData, never modified).
 * 3x3 structuring element, @p iterations passes.
 */
void morphologicalCleanup(uint8_t *mask, int width, int height, int iterations,
                          MorphOp op);

}

