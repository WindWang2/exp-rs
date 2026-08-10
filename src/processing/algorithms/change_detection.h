// src/processing/algorithms/change_detection.h
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ChangeDetection
{

struct ChangeStats {
    size_t count = 0;        ///< total samples
    size_t validCount = 0;   ///< finite (non-NaN) samples the mean/stddev use
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
 * Multivariate Alteration Detection (MAD) change magnitude across @p bandCount bands.
 * Performs canonical correlation analysis (CCA) between before and after image bands,
 * returning Chi-Square change distance per pixel.
 * @param beforeBands / afterBands arrays of @p bandCount buffers, each @p pixels floats
 */
bool madChange(const float *const *beforeBands, const float *const *afterBands,
               int bandCount, size_t pixels, float *out,
               QString *errorMessage = nullptr);

// ---------------------------------------------------------------------------
// Streaming MAD primitives (memory-bounded, multi-pass).
//
// MAD is decomposed into three streaming passes over band-interleaved-by-pixel
// (BIP) tiles plus two finalize steps, so the working set is O(tilePixels *
// bandCount + bandCount^2) instead of O(pixels * bandCount):
//
//   Pass 1  madAccumulateSums     per-band raw sums over valid pixels
//           madFinalizeMeans      means (fails when validCount < bandCount+2)
//   Pass 2  madAccumulateCentered centered cross-products S_XX / S_YY / S_XY
//           madFinalize           regularization -> sqrt-inverse -> CCA/SVD
//   Pass 3  madTransformTile      per-pixel chi-square change magnitude
//
// A pixel is valid iff ALL before/after bands are std::isfinite. Invalid
// pixels are skipped by the accumulation passes and produce quiet_NaN in the
// transformed output. The math is identical to madChange(), which is now a
// thin wrapper feeding the full-scene band-major buffers to these primitives
// as one giant tile.
// ---------------------------------------------------------------------------

/// Mutable accumulator for the streaming MAD pipeline.
struct MadStreamingState
{
    int bandCount = 0;                 ///< bands; set by the first accumulate call
    size_t validCount = 0;             ///< pixels finite in every before/after band
    std::vector<double> sumX, sumY;    ///< [B] raw per-band sums (pass 1)
    std::vector<double> meanX, meanY;  ///< [B]
    std::vector<double> xx, yy, xy;    ///< [B*B] row-major centered products (pass 2)
    std::vector<double> A, B;          ///< [B*B] row-major canonical coefficients
    std::vector<double> varMad;        ///< [B] MAD variate variances (>= 1e-6)
    bool meansReady = false;
    bool covReady = false;
    bool ready = false;
};

/**
 * Pass 1: accumulate per-band raw sums over the valid pixels of one BIP tile
 * (@p tilePixels pixels, @p bandCount floats per pixel).
 */
bool madAccumulateSums(const float *beforeBip, const float *afterBip,
                       size_t tilePixels, int bandCount, MadStreamingState *s);

/**
 * Finalize the per-band means. Returns false (with @p errorMessage) when
 * validCount < bandCount + 2, i.e. the covariance estimate is degenerate.
 */
bool madFinalizeMeans(MadStreamingState *s, QString *errorMessage = nullptr);

/**
 * Pass 2: accumulate centered cross-products against the finalized means
 * (s.meanX/meanY) into s.xx / s.yy / s.xy. Centering against the final means
 * avoids the cancellation-prone E[xy] - xbar*ybar form.
 */
bool madAccumulateCentered(const float *beforeBip, const float *afterBip,
                           size_t tilePixels, int bandCount, MadStreamingState *s);

/**
 * Finalize covariances: divide by (N-1), trace-scaled diagonal regularization,
 * SVD-based sqrt-inverse, canonical correlation analysis, sign convention and
 * MAD variate variances. Scales with bandCount^2 only.
 */
bool madFinalize(MadStreamingState *s, QString *errorMessage = nullptr);

/**
 * Pass 3: per-pixel chi-square change magnitude
 *   Z = sum_k (u_k - v_k)^2 / varMad_k,  u = A^T (x - xbar), v = B^T (y - ybar).
 * @p out holds @p tilePixels floats; invalid pixels become quiet_NaN.
 */
void madTransformTile(const float *beforeBip, const float *afterBip,
                      size_t tilePixels, int bandCount, const MadStreamingState &s,
                      float *out);

/**
 * Otsu's between-class variance threshold over a precomputed histogram
 * (streaming variant of otsuThreshold; same maximization and threshold
 * placement: minVal + (bestBin + 0.5) * range / bins). Returns false when
 * @p finiteCount is zero; returns @p minVal when all finite values coincide.
 */
bool otsuThresholdFromHistogram(double minVal, double maxVal,
                                const std::vector<double> &hist,
                                size_t finiteCount, float *threshold);

/**
 * Nearest-rank percentile (0..100) estimated from a precomputed histogram:
 * returns the bin lower edge plus the fractional position of the rank within
 * that bin. Streams over the same binning as the otsu histogram variant.
 * Returns false when @p finiteCount is zero.
 */
bool percentileThresholdFromHistogram(double minVal, double maxVal,
                                      const std::vector<double> &hist,
                                      size_t finiteCount, double percentile,
                                      float *threshold);

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

/**
 * Minimum mapping unit: removes every 8-connected component whose area is
 * below @p minArea pixels (0/1 mask; 255 = NoData never modified). This is a
 * no-op when @p minArea is 0. Returns false only on invalid arguments.
 */
bool connectedComponentFilter(uint8_t *mask, int width, int height,
                              size_t minArea);

}

