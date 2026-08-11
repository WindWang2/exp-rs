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
    Ratio,                ///< after / before (NaN where before == 0)
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

} // namespace sicnu::operators::rs
