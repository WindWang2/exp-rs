// src/processing/algorithms/temporal/temporal_region_table.h
// Multi-region temporal extraction (Temporal Platform 10.0, closes C-2:
// one call, many points/polygons — a region × date table instead of a loop
// of single-ROI extractions).
//
// Contract:
//   - Region ids are caller-owned strings (duplicated ids are a parse error,
//     never silently merged); geometry is map coordinates in the collection
//     CRS — the same contract as rs:temporal_extract_series.
//   - Pixel membership: pixel centers inside the polygon by even-odd ray
//     casting (the extract_series semantics), confined to the polygon's
//     bounding-box window; point regions map to exactly one pixel (the one
//     under the point), refused when outside the grid.
//   - Statistics per region × date: mean / min / max / population stddev
//     (Welford) / valid_count, plus an optional exact median (bounded
//     scratch, guarded by the median budget).
//   - Memory: O(R) accumulators + one bounded median scratch. The reducer is
//     single-threaded by contract (matches TemporalTileReader).
#pragma once

#include <QString>
#include <QVector>

#include <json/json.h>

#include <array>
#include <cstddef>
#include <vector>

namespace sicnu::temporal
{

struct RegionRef
{
  QString id;
  bool isPoint = true;
  double x = 0.0;  ///< point x (map coords)
  double y = 0.0;  ///< point y
  std::vector<std::array<double, 2>> polygon;  ///< polygon vertices (>= 3)
};

/// Parses the canonical regions JSON: an array of
///   {"id": "...", "point": [x, y]}               or
///   {"id": "...", "polygon": [[x, y], ...]}.
/// @a maxRegions caps the accepted count (guard, not pagination). Duplicate
/// or empty ids are errors. Returns false + @a error on malformed input.
bool parseRegionsJson( const Json::Value &regionsJson, int maxRegions,
                       QVector<RegionRef> *out, QString *error );

/// Pixel footprint of one region inside the reference grid (north-up affine,
/// rotation rejected like preflight's same-grid check).
struct RegionGeometry
{
  int regionIndex = -1;
  int xOff = 0;               ///< bounding-box window origin (grid pixels)
  int yOff = 0;
  int w = 0;                  ///< window size (>= 1)
  int h = 0;
  bool isPoint = true;
  std::vector<int> insideOffsets;  ///< pixel offsets within the window that
                                   ///  belong to the region; empty for point
                                   ///  regions (the single window pixel).
  size_t insideCount() const
  {
    return isPoint ? 1 : insideOffsets.size();
  }
};

/// Builds the geometry for @a region. Returns false + @a error when the
/// region lies completely outside the grid (points outside; polygons whose
/// clipped bbox is empty).
bool buildRegionGeometry( const RegionRef &region, int regionIndex,
                          const std::array<double, 6> &geoTransform,
                          int gridWidth, int gridHeight,
                          RegionGeometry *out, QString *error );

/// Streaming per-date region statistics. Reused across dates: beginDate()
/// resets, addSample() folds one valid sample (NaN samples are SKIPPED BY
/// THE CALLER — the reducer only sees finite values), endDate() finalizes.
struct RegionDateStats
{
  double mean = 0.0;
  double min = 0.0;
  double max = 0.0;
  double stddev = 0.0;      ///< population (÷N), NaN when validCount == 0
  float median = 0.0f;      ///< NaN when median buffer disabled or count == 0
  int validCount = 0;       ///< finite samples contributing
};

class RegionDateReducer
{
public:
  /// @a medianScratchFloats bounds the exact-median buffer (0 disables the
  /// median). The caller derives the bound from region pixel counts; when
  /// the total exceeds the budget the median reports NaN instead of
  /// over-allocating (declared degradation, never a crash).
  RegionDateReducer( int regionCount, size_t medianScratchFloats,
                     const std::vector<size_t> &regionInsideCounts );

  void beginDate();
  void addSample( int regionIndex, float value );
  void endDate();

  const RegionDateStats &stats( int regionIndex ) const { return m_stats[static_cast<size_t>( regionIndex )]; }
  int regionCount() const { return static_cast<int>( m_stats.size() ); }
  size_t totalSamplesLastDate() const { return m_totalSamples; }
  /// False when the median scratch budget was exceeded (median degrades to
  /// NaN for every region — the declared degradation, surfaced in results).
  bool medianEnabled() const { return m_medianEnabled; }

private:
  std::vector<RegionDateStats> m_stats;
  // Welford state per region.
  std::vector<double> m_mean;
  std::vector<double> m_m2;
  std::vector<float> m_min;
  std::vector<float> m_max;
  std::vector<int> m_count;
  // Median scratch (CSR layout over regions).
  std::vector<float> m_medianScratch;
  std::vector<size_t> m_regionStarts;
  std::vector<size_t> m_regionWrites;
  bool m_medianEnabled = false;
  size_t m_totalSamples = 0;
};

} // namespace sicnu::temporal
