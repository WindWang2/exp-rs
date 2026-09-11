/***************************************************************************
 * rs_zonal_stats_operator.h — per-zone raster statistics (8.0, package D)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:zonal_stats — statistics of raster values inside vector zones.
 *
 * Zones stream through the geospatial VectorReader contract (declared CRS
 * transform into the value raster's CRS); pixels belong to a zone through
 * the shared windowed rasterization seam (rs_raster_vector) — the SAME
 * pixel-membership implementation rs:rasterize uses. Both rasters and the
 * feature cache stream in bounded windows; zones spanning windows
 * accumulate across them.
 *
 * Statistics per (zone, band): count (valid pixels), nodata count, min,
 * max, mean, stddev (population, ÷N), and optional exact median
 * (budgeted — zones whose median collection exceeds the shared budget are
 * flagged, their other stats stay exact).
 *
 * Semantics:
 *   * valid pixel = finite AND not the band's declared sentinel;
 *   * overlapping zones: last feature wins (input order), matching
 *     rs:rasterize;
 *   * zone key: `zoneField` attribute (string or numeric) or the FID;
 *   * multi-polygon zones = several features sharing a key;
 *   * an empty zone (no valid pixel) still reports with count 0 and NaN
 *     statistics — silence would hide geometry/CRS mistakes.
 *
 * Refusals: raster without CRS/north-up grid, missing zone field,
 * malformed geometry, cache over budget, no zones at all.
 *
 * Parameters:
 *   input      (string, required) value raster
 *   vector     (string, required) zone polygons (OGR)
 *   layer      (string, optional) layer name/index (default first)
 *   zoneField  (string, optional) attribute for zone identity (default FID)
 *   bands      (array int, optional) 1-based bands (default [1])
 *   median     (bool, optional) compute exact median (default true)
 *   output     (string, required) CSV path
 *
 * CSV columns: zone_key, band, valid_pixels, nodata_pixels, min, max,
 * mean, stddev, median, median_truncated
 */
class RsZonalStatsOperator : public RSOperator {
public:
    std::string name() const override { return "rs:zonal_stats"; }
    std::string displayName() const override { return "Zonal Statistics"; }
    std::string group() const override { return "raster-vector"; }
    std::string description() const override {
        return "Per-zone raster statistics (count/min/max/mean/stddev/median) for "
               "vector polygons on the value raster's grid.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
