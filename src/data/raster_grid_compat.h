// src/data/raster_grid_compat.h — shared raster-grid compatibility service
#pragma once

#include <array>
#include <optional>

#include <QString>
#include <QVector>

#include "data_asset.h"

namespace sicnu::data
{

/// Minimal grid description needed for pixel-grid compatibility checks. Built
/// from a `RasterStructure` (catalog side) or directly from a GDAL dataset
/// (operator side). The service is pure: it opens nothing and mutates nothing.
struct RasterGrid
{
  QString crsWkt;                       ///< empty = unreferenced/unknown
  bool hasGeoTransform = false;         ///< false = no georeferencing
  std::array<double, 6> geoTransform{}; ///< GDAL affine (originX, pixelW, rotX, originY, rotY, pixelH)
  int width = 0;
  int height = 0;
  /// Per-band NoData values, 1-based band at index band-1. Empty vector = unknown.
  QVector<std::optional<double>> bandNoData;

  double pixelSizeX() const;
  double pixelSizeY() const;
  /// Raster extent derived from the geotransform + dimensions; nullopt when
  /// the raster is ungeoreferenced or the extent is degenerate.
  std::optional<SpatialExtent> extent() const;
};

/// Verdict for one grid-compatibility issue, in priority order.
enum class GridCompatVerdict
{
  Compatible = 0,
  MissingCrs,           ///< exactly one raster has no CRS
  CrsMismatch,          ///< CRSs differ
  AxisOrientationMismatch, ///< opposite axis orientation (north-up vs south-up or mirrored)
  RotationMismatch,     ///< raster carries rotation terms
  PixelSizeMismatch,    ///< pixel sizes differ
  OriginMisalignment,   ///< same pixel size, sub-pixel origin offset
  ExtentMismatch,       ///< same lattice, differing extents
  NoDataMismatch,       ///< per-band NoData values differ (warning-grade)
};

/// One grid-compatibility issue: verdict, stable code, and an actionable
/// message. `blocking` is false for warnings (e.g. NoData mismatch).
struct GridCompatIssue
{
  GridCompatVerdict verdict = GridCompatVerdict::Compatible;
  QString code;    ///< stable id, e.g. "grid.pixel_size_mismatch"
  QString message; ///< actionable human-readable message
  bool blocking = true;
};

/// Result of comparing two grids. `issues` is ordered by priority; the first
/// blocking issue is the primary reason the grids are incompatible.
struct GridCompatReport
{
  QVector<GridCompatIssue> issues;

  /// True when no blocking issue exists (warnings such as NoData mismatch are
  /// allowed). This is the gate callers should enforce before combining pixels.
  bool compatible() const { return !hasBlockingIssue(); }
  /// True when there are no issues at all (not even warnings).
  bool aligned() const { return issues.isEmpty(); }
  /// The first blocking issue, or nullopt when none exists.
  std::optional<GridCompatIssue> primaryBlocking() const;

private:
  bool hasBlockingIssue() const;
};

/// Compares two rasters' pixel grids. Priority order, first blocking issue
/// wins: MissingCrs, CrsMismatch, PixelSizeMismatch, OriginMisalignment,
/// ExtentMismatch. NoDataMismatch is collected as a warning once the grid
/// itself is compatible and both sides declare known, differing NoData.
/// Two rasters that both lack georeferencing are not spatially comparable and
/// report Compatible — callers fall back to dimension checks.
GridCompatReport compareGrids( const RasterGrid &a, const RasterGrid &b );

/// Convenience over `RasterStructure` for catalog-side comparisons.
GridCompatReport compareStructures( const RasterStructure &a,
                                    const RasterStructure &b );

} // namespace sicnu::data
