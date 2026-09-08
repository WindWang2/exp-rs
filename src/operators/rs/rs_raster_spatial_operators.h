/***************************************************************************
 * rs_raster_spatial_operators.h — mask/label/window raster operators
 * (Foundation 5.0, Milestone G) over the Milestone A primitives.
 *
 * All seven operators share the mask contract: single-band input, declared
 * NoData sentinel (NaN when undeclared), foreground = value 1, background =
 * value 0, other finite values are invalid input for mask ops (typed
 * refusal) — matching the 0/1/255 primitive encoding where 255-reserved is
 * replaced by the declared sentinel.
 *
 *   rs:morphology           erode|dilate|open|close, 4/8-conn, iterations
 *   rs:connected_components raster-order compact labels (Float32 integral)
 *   rs:fill_holes           background components not touching the border
 *                           become foreground
 *   rs:sieve                remove components smaller than min_area_pixels
 *   rs:proximity            exact Euclidean distance to foreground (pixels)
 *   rs:local_extrema        window max/min test → 1/0 (streamed halo tiles)
 *   rs:focal_stats          window mean|sum|min|max|stddev|range (replicate
 *                           edge policy, streamed halo tiles)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

#include <string>

namespace sicnu::operators::rs {

/// Shared implementation driver (mask_ops == the mask/label family,
/// window_ops == focal/extrema); the classes below are thin.
Json::Value runRasterSpatialOp( const std::string &opName, const Json::Value &params,
                                RSOperatorContext &context );

#define SICNU_DECLARE_SPATIAL_OP(CLASS, ID, DISPLAY, GROUP, DESC)                  \
    class CLASS : public RSOperator {                                              \
    public:                                                                        \
        std::string name() const override { return ID; }                           \
        std::string displayName() const override { return DISPLAY; }               \
        std::string group() const override { return GROUP; }                       \
        std::string description() const override { return DESC; }                  \
        RSOperatorMemoryPolicy memoryPolicy() const override                       \
        {                                                                          \
            return RSOperatorMemoryPolicy::FullRaster;                             \
        }                                                                          \
        Json::Value schema() const override;                                       \
        Json::Value metadata() const override;                                     \
        Json::Value executionEstimate() const override;                            \
        Json::Value estimateExecution( const Json::Value &params ) const override; \
        Json::Value run( const Json::Value &params, RSOperatorContext &context ) override \
        {                                                                          \
            return runRasterSpatialOp( ID, params, context );                      \
        }                                                                          \
    }

SICNU_DECLARE_SPATIAL_OP( RsMorphologyOperator, "rs:morphology", "Morphology",
                          "raster_spatial",
                          "Binary morphology over a 0/1 mask: erode, dilate, open, "
                          "close (4/8-connectivity, iterations)." );
SICNU_DECLARE_SPATIAL_OP( RsConnectedComponentsOperator, "rs:connected_components",
                          "Connected Components", "raster_spatial",
                          "Deterministic connected-component labeling of a 0/1 mask "
                          "(raster-order compact labels)." );
SICNU_DECLARE_SPATIAL_OP( RsFillHolesOperator, "rs:fill_holes", "Fill Holes",
                          "raster_spatial",
                          "Fill background regions that do not touch the raster "
                          "border (interior holes become foreground)." );
SICNU_DECLARE_SPATIAL_OP( RsSieveOperator, "rs:sieve", "Sieve", "raster_spatial",
                          "Remove foreground components smaller than a minimum "
                          "area (pixels)." );
SICNU_DECLARE_SPATIAL_OP( RsProximityOperator, "rs:proximity", "Proximity",
                          "raster_spatial",
                          "Exact Euclidean distance (pixels) to the nearest "
                          "foreground cell; unreachable cells are NaN." );

#undef SICNU_DECLARE_SPATIAL_OP

/// Window operators stream (halo tiles, O(tile) memory).
#define SICNU_DECLARE_WINDOW_OP(CLASS, ID, DISPLAY, DESC)                          \
    class CLASS : public RSOperator {                                              \
    public:                                                                        \
        std::string name() const override { return ID; }                           \
        std::string displayName() const override { return DISPLAY; }               \
        std::string group() const override { return "raster_spatial"; }            \
        std::string description() const override { return DESC; }                  \
        RSOperatorMemoryPolicy memoryPolicy() const override                       \
        {                                                                          \
            return RSOperatorMemoryPolicy::Streaming;                              \
        }                                                                          \
        Json::Value schema() const override;                                       \
        Json::Value metadata() const override;                                     \
        Json::Value executionEstimate() const override;                            \
        Json::Value estimateExecution( const Json::Value &params ) const override; \
        Json::Value run( const Json::Value &params, RSOperatorContext &context ) override \
        {                                                                          \
            return runRasterSpatialOp( ID, params, context );                      \
        }                                                                          \
    }

SICNU_DECLARE_WINDOW_OP( RsLocalExtremaOperator, "rs:local_extrema", "Local Extrema",
                         "Flag window maxima or minima as 1/0." );
SICNU_DECLARE_WINDOW_OP( RsFocalStatsOperator, "rs:focal_stats", "Focal Statistics",
                         "Window statistics (mean, sum, min, max, stddev, range) "
                         "with the replicate edge policy." );

#undef SICNU_DECLARE_WINDOW_OP

} // namespace sicnu::operators::rs
