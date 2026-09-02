/***************************************************************************
 * rs_change_streaming.h  —  Shared tile-streaming change-detection kernel
 *
 * Single implementation behind the change-detection facade
 * (rs:change_detection) and the atomic primitives (rs:change_difference,
 * rs:change_normalized_difference, rs:change_ratio, rs:change_cva,
 * rs:change_mad). Magnitude paths are memory-bounded: working set is
 * O(tilePixels * bands + bands^2), independent of the raster dimensions.
 * The mask-derivation path additionally materializes a full-resolution Byte
 * mask (O(width * height)) for the cleanup/MMU stages, matching the
 * pre-existing mask workflow.
 *
 * The remaining change atoms (rs:change_log_ratio, rs:change_cva_angle,
 * rs:change_sam) stream through the same tile infrastructure via the
 * run*Streaming entry points below; their working set is O(tilePixels * bands).
 * runIrMadStreaming streams every IR-MAD pass (weighted means, weighted
 * covariance, reweighting, final transform) the same way; the algorithm's only
 * cross-iteration per-pixel state is the Chi-square weight vector, which is
 * carried between reweighting iterations as one full-resolution Float64 frame
 * (8 bytes/pixel — honestly one frame, versus the ~2B+7 frames the
 * full-frame path materialized). No other full frame is ever resident.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator_context.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <json/json.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace sicnu::operators::rs {

/// Change metric selection.
enum class ChangeMetric
{
    Difference,           ///< after - before (signed; atomic primitive semantics)
    AbsoluteDifference,   ///< |after - before| (legacy facade "difference" semantics)
    NormalizedDifference, ///< (after - before) / (after + before)
    Ratio,                ///< after / before (NaN where before <= 0)
    Cva,                  ///< change vector analysis magnitude (all bands)
    Mad                   ///< multivariate alteration detection (all bands)
};

/// Streaming mean / population stddev over non-NaN values, plus running
/// min/max. Matches ChangeDetection::statistics() semantics (NaNs skipped,
/// stddev uses the N denominator).
struct StreamingMagnitudeStats
{
    size_t validCount = 0;
    double mean = 0.0;
    double m2 = 0.0; // Welford M2 accumulator
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();

    void add( float v )
    {
        if ( !std::isfinite( v ) )
            return;
        ++validCount;
        const double d = static_cast<double>( v ) - mean;
        mean += d / static_cast<double>( validCount );
        m2 += d * ( static_cast<double>( v ) - mean );
        if ( v < minVal ) minVal = v;
        if ( v > maxVal ) maxVal = v;
    }

    double stddev() const
    {
        return ( validCount > 1 ) ? std::sqrt( m2 / static_cast<double>( validCount ) ) : 0.0;
    }
};

/// Result of a mask derivation: the effective threshold and the
/// changed/evaluated pixel counts (255 = NoData, 1 = changed).
struct MaskDerivation
{
    float thresholdUsed = 0.0f;
    size_t changed = 0;
    size_t evaluated = 0;
};

struct ChangeStreamingOptions
{
    /// 1-based band on the before raster (single-band metrics).
    int beforeBand = 1;
    /// 1-based band on the after raster (single-band metrics).
    int afterBand = 1;

    /// When true, derive a binary mask from the magnitude (threshold strategy
    /// below) instead of writing the Float32 magnitude.
    bool makeMask = false;
    /// Manual threshold (thresholdMethod == "manual").
    float threshold = 0.5f;
    /// Threshold strategy: "manual" | "otsu" | "percentile" | "statistical".
    std::string thresholdMethod = "manual";
    /// Percentile for thresholdMethod == "percentile" (0-100).
    double percentile = 90.0;
    /// Stddev multiplier for thresholdMethod == "statistical" (mean + k*stddev).
    double statisticalK = 2.0;
    /// Minimum mapping unit: drop changed components smaller than this (0 disables).
    int minAreaPixels = 0;
    /// Morphological cleanup: "none" | "erode" | "dilate" | "open" | "close".
    std::string cleanup = "none";
    int cleanupIterations = 1;

    std::string outputPath;
    /// Value reported in result["method"] (e.g. "difference", "mad").
    std::string methodLabel;
};

/**
 * Computes the change magnitude (or derived mask) between two co-registered
 * rasters in 256x256 tiles. NaN is propagated for invalid pixels (a NaN delta
 * in any band for cva; all-NaN-tile for mad). Cancellation is checked per
 * tile via context.throwIfCancelled(). Returns the result JSON with "output",
 * "method", "mean"/"stddev" and — for the mask path — "thresholdUsed",
 * "changedPixels"/"totalPixels"/"changedPercent"/"changedArea".
 *
 * @throws RSOperatorError on read/write/computation failure or cancellation.
 */
Json::Value runChangeStreaming( const GdalDatasetWrapper &beforeDs,
                                const GdalDatasetWrapper &afterDs,
                                int width, int height, ChangeMetric metric,
                                const ChangeStreamingOptions &opts,
                                RSOperatorContext &context );

/**
 * Thresholds an existing single-band magnitude raster into a Byte 0/1 mask
 * (255 = NoData) using the manual/otsu/percentile/statistical strategies with
 * optional morphological cleanup and minimum-mapping-unit filtering. Shared by
 * the change-detection mask path and the atomic rs:threshold_raster operator.
 *
 * @param inputPath  magnitude raster (must exist, single band)
 * @param opts       threshold strategy + cleanup + MMU + outputPath
 * @throws RSOperatorError on open/read/write/computation failure or cancel.
 */
MaskDerivation thresholdRasterToMask( const std::string &inputPath,
                                      const ChangeStreamingOptions &opts,
                                      RSOperatorContext &context );

/// Per-atom parameters for the streaming change atoms below. Only the fields
/// documented for the invoked atom are consumed; the rest keep their defaults.
struct ChangeAtomStreamingOptions
{
    /// log_ratio: 1-based band on the before raster.
    int beforeBand = 1;
    /// log_ratio: 1-based band on the after raster.
    int afterBand = 1;
    /// cva_angle: 1-based second band of the 2-band pair on the before raster
    /// (the first pair member is beforeBand).
    int beforeBand2 = 2;
    /// cva_angle: 1-based second band of the 2-band pair on the after raster.
    int afterBand2 = 2;
    /// log_ratio: small positive constant preventing ln(0); <= 0 falls back
    /// to 1e-4 (matching ChangeDetection::logRatio).
    float epsilon = 1e-4f;
    /// cva_angle output mode: "angle" (radians [-pi, pi]) or "quadrant"
    /// (1..4; NoData pixels carry 255 like the full-frame path).
    std::string mode = "angle";
    /// irmad: maximum reweighting iterations (clamped to 1..100 like
    /// ChangeDetection::irMadChange).
    int maxIterations = 20;
    /// irmad: convergence threshold on the max canonical-correlation delta
    /// (<= 0 falls back to 1e-4).
    double convThreshold = 1e-4;

    std::string outputPath;
};

/**
 * Streams rs:change_log_ratio: ln(max(after, 0) + eps) - ln(max(before, 0) + eps)
 * per 256x256 tile, with declared NoData / non-finite input pixels propagating
 * to NaN output. Working set is O(tilePixels); no full frame is materialized.
 * Result JSON: "output", "method" ("log_ratio"), "width", "height",
 * "mean"/"stddev" (NaN-skipping streaming stats).
 *
 * @throws RSOperatorError on read/write failure or cancellation.
 */
Json::Value runLogRatioStreaming( const GdalDatasetWrapper &beforeDs,
                                  const GdalDatasetWrapper &afterDs,
                                  int width, int height,
                                  const ChangeAtomStreamingOptions &opts,
                                  RSOperatorContext &context );

/**
 * Streams rs:change_cva_angle: 2-band directional change angle (radians) or
 * semantic quadrant via ChangeDetection::cvaMagnitudeAndAngle / cvaQuadrant,
 * evaluated per 256x256 tile (the magnitude lives only for the tile).
 * Result JSON: "output", "method" ("cva_angle"), "mode", "width", "height".
 *
 * @throws RSOperatorError on read/write/computation failure or cancellation.
 */
Json::Value runCvaAngleStreaming( const GdalDatasetWrapper &beforeDs,
                                  const GdalDatasetWrapper &afterDs,
                                  int width, int height,
                                  const ChangeAtomStreamingOptions &opts,
                                  RSOperatorContext &context );

/**
 * Streams rs:change_sam: spectral angle between the before/after spectra over
 * all bands via ChangeDetection::samChangeAngle, evaluated per 256x256 tile
 * with per-tile band-major buffers. Result JSON: "output", "method" ("sam"),
 * "width", "height", "mean"/"stddev".
 *
 * @throws RSOperatorError on read/write/computation failure or cancellation.
 */
Json::Value runSamStreaming( const GdalDatasetWrapper &beforeDs,
                             const GdalDatasetWrapper &afterDs,
                             int width, int height,
                             const ChangeAtomStreamingOptions &opts,
                             RSOperatorContext &context );

/**
 * Streams rs:change_irmad (multi-pass): every iteration re-estimates the
 * weighted CCA by streaming tiles (weighted-sums pass, weighted-covariance
 * pass), and each reweighting step streams another pass to refresh the
 * per-pixel Chi-square weights; the final transform streams once more. The
 * only cross-iteration per-pixel state is the weight vector, carried as one
 * full-resolution Float64 frame (documented trade-off — see the header
 * comment); everything else is O(tilePixels * bands + bands^2). The streamed
 * iteration performs the same accumulations in the same pixel order as
 * ChangeDetection::irMadChange, so results match the full-frame kernel.
 * Result JSON: "output", "method" ("irmad"), "width", "height",
 * "mean"/"stddev".
 *
 * @throws RSOperatorError on read/write/computation failure, degenerate
 *         inputs (< bandCount + 2 valid pixels), or cancellation.
 */
Json::Value runIrMadStreaming( const GdalDatasetWrapper &beforeDs,
                               const GdalDatasetWrapper &afterDs,
                               int width, int height,
                               const ChangeAtomStreamingOptions &opts,
                               RSOperatorContext &context );

} // namespace sicnu::operators::rs
