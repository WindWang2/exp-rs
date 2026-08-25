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
 * Kittler-Illingworth Minimum Error Thresholding (KI-MET) over a precomputed histogram.
 * Assumes a mixture of two Gaussian distributions with unequal variances and priors.
 * Minimizes J(T) = 1 + 2*(P1*ln(sigma1) + P2*ln(sigma2)) - 2*(P1*ln(P1) + P2*ln(P2)).
 * Returns false when @p finiteCount is zero; returns @p minVal when all finite values coincide.
 */
bool kittlerIllingworthThresholdFromHistogram(double minVal, double maxVal,
                                              const std::vector<double> &hist,
                                              size_t finiteCount, float *threshold);

/**
 * Kittler-Illingworth Minimum Error Thresholding (KI-MET) over the finite values of @p values.
 * @return true and the threshold when at least two distinct values exist;
 *         false for null/empty/all-NaN input.
 */
bool kittlerIllingworthThreshold(const float *values, size_t count, float *threshold, int bins = 256);

/**
 * Change Vector Analysis magnitude and 2-band directional angle.
 * Magnitude = sqrt((after1 - before1)^2 + (after2 - before2)^2).
 * Angle = atan2(after2 - before2, after1 - before1) in radians [-pi, pi].
 * If either band has NaN at pixel i, outMagnitude[i] and outAngle[i] are NaN.
 */
bool cvaMagnitudeAndAngle(const float *beforeBand1, const float *beforeBand2,
                          const float *afterBand1, const float *afterBand2,
                          size_t pixels, float *outMagnitude, float *outAngle,
                          QString *errorMessage = nullptr);

/**
 * Change Vector Analysis semantic quadrant mapping (1..4) based on sign of deltas:
 * - Quadrant 1 (1): delta1 > 0, delta2 > 0
 * - Quadrant 2 (2): delta1 <= 0, delta2 > 0
 * - Quadrant 3 (3): delta1 <= 0, delta2 <= 0
 * - Quadrant 4 (4): delta1 > 0, delta2 <= 0
 * Invalid/NaN pixels receive 255 (NoData).
 */
bool cvaQuadrant(const float *beforeBand1, const float *beforeBand2,
                 const float *afterBand1, const float *afterBand2,
                 size_t pixels, uint8_t *outQuadrant,
                 QString *errorMessage = nullptr);

/**
 * Spectral Angle Mapper (SAM) change angle across @p bandCount spectral bands.
 * Computes spectral angle alpha = arccos( (X_t1 . X_t2) / (||X_t1|| * ||X_t2||) ) in radians.
 * Output is illumination-invariant (scaling band reflectances by constant leaves angle unchanged).
 * Returns NaN for pixels with NaN or invalid values in any band.
 */
bool samChangeAngle(const float *const *beforeBands, const float *const *afterBands,
                    int bandCount, size_t pixels, float *outAngleRadians,
                    QString *errorMessage = nullptr);

/**
 * Log-Ratio change: out[i] = ln(max(after[i], 0) + eps) - ln(max(before[i], 0) + eps).
 * Symmetric around 0, ideal for SAR and wide dynamic range sensors.
 */
bool logRatio(const float *before, const float *after, float *out,
              size_t count, float epsilon = 1e-4f);

/**
 * Iteratively Reweighted Multivariate Alteration Detection (IR-MAD).
 * Iteratively estimates canonical correlation analysis (CCA) transformation
 * with Chi-Square sample weights w_i = 1 - P(Chi^2(B) <= Z_i) = P(Chi^2(B) > Z_i)
 * until max canonical correlation delta < convThreshold or maxIterations reached.
 * Returns Chi-Square change distance per pixel in @p outChiSquare.
 */
bool irMadChange(const float *const *beforeBands, const float *const *afterBands,
                 int bandCount, size_t pixels, float *outChiSquare,
                 int maxIterations = 20, double convThreshold = 1e-4,
                 QString *errorMessage = nullptr);

/**
 * Minimum mapping unit: removes every 8-connected component whose area is
 * below @p minArea pixels (0/1 mask; 255 = NoData never modified). This is a
 * no-op when @p minArea is 0. Returns false only on invalid arguments.
 */
bool connectedComponentFilter(uint8_t *mask, int width, int height,
                              size_t minArea);

}
