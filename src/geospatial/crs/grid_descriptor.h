/***************************************************************************
  geospatial/crs/grid_descriptor.h
  Cloud-Native Geospatial I/O 7.0 — generalized grid classification &
  placement (task F). North-up is a special case here, never an assumption.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
    * GridDescriptor::fromMetadata classifies a raster's placement from its
      canonical metadata: north-up affine, rotated affine, flipped axes,
      GCP-based, RPC-based, or unknown — the classification decides which
      placement operations are honest.
    * pixelToWorld / worldToPixel use the FULL affine (rotation terms
      included); worldToPixel refuses a singular affine instead of dividing
      through it.
    * footprintPolygon densifies the grid edges through the affine — an
      axis-aligned bbox of corners is a lie for rotated grids (it is still
      available, honestly named approximateExtent).
    * matchesReference compares two affine grids for pixel-grid agreement
      (origin, pixel size, rotation, extent) within a tolerance. GCP/RPC
      placements are explicitly NOT axis-matchable — the refusal is a
      structured answer with a reason, never a silent mismatch.
    * checkResamplingPolicy refuses interpolated resampling for categorical
      sources (palette / classification bands): averaging categories is a
      fidelity violation, nearest/mode stay available.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_GRID_DESCRIPTOR_H
#define SICNU_GEOSPATIAL_GRID_DESCRIPTOR_H

#include "geospatial/common.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <array>
#include <string>
#include <vector>

namespace sicnu::geo
{

enum class GridKind
{
  NorthUp,    ///< affine with zero rotation and convention axis directions
  Rotated,    ///< affine with non-zero rotation terms
  Flipped,    ///< affine whose axis directions run against the convention
  GcpBased,   ///< placement carried by GCPs (no usable affine)
  RpcBased,   ///< placement carried by RPC coefficients
  Unknown     ///< no usable placement declared
};

const char *gridKindName( GridKind kind );

/// Resampling semantics of a source's bands.
enum class ResamplingCategory
{
  Continuous,   ///< interpolation is meaningful (Float/int reflectance, DEMs)
  Categorical   ///< classes/palettes: only nearest/mode preserve semantics
};

const char *resamplingCategoryName( ResamplingCategory category );

class GridDescriptor
{
  public:
    /// Classifies from canonical metadata (pure — never touches a dataset).
    static GridDescriptor fromMetadata( const RasterMetadata &metadata );

    GridKind kind = GridKind::Unknown;
    std::array<double, 6> geotransform = { 0, 1, 0, 0, 0, 1 };
    CrsInfo crs;
    int width = 0;
    int height = 0;
    bool flippedX = false;        ///< pixel size runs against the x convention
    bool flippedY = false;        ///< pixel size runs against the y convention
    double rotationDegrees = 0.0; ///< signed rotation of the x axis (0 north-up)
    bool hasGcps = false;
    bool hasRpcs = false;

    /// Full-affine placement (rotation terms included).
    CrsPoint pixelToWorld( double column, double row ) const;
    /// Inverse affine. Throws GeoError(TransformFailed) for a singular
    /// affine (degenerate pixel size) — never divides through it.
    CrsPoint worldToPixel( const CrsPoint &world ) const;

    bool isNorthUp() const { return kind == GridKind::NorthUp; }
    bool hasAffinePlacement() const
    {
      return kind == GridKind::NorthUp || kind == GridKind::Rotated || kind == GridKind::Flipped;
    }

    /// Axis-aligned bounds of the four corners — an honest LOWER bound for
    /// rotated grids (the true extent is the footprint below).
    CrsBoundingBox approximateExtent() const;

    /// Densified grid-edge polygon through the full affine: the true
    /// footprint of a rotated grid, not its bounding box.
    std::vector<CrsPoint> footprintPolygon( int edgeSamples = 16 ) const;

    /// Result of a reference-grid comparison.
    struct MatchResult
    {
        bool matches = false;
        double maxDeviation = 0.0; ///< worst compared deviation (units of the CRS)
        std::string note;          ///< which comparisons ran / why refused
    };

    /// Compares pixel-grid agreement with a reference grid: same placement
    /// kind, same pixel size, same rotation, and the origins within
    /// `tolerance`. GCP/RPC placements are refused with a reason (axis
    /// matching is meaningless for them) — an explicit refusal, not a
    /// silent mismatch.
    MatchResult matchesReference( const GridDescriptor &reference, double tolerance ) const;
};

/// Classifies a source's resampling semantics. A band is categorical when
/// it is a palette (color table) or a declared classification/QA role.
ResamplingCategory resamplingCategoryFor( const RasterMetadata &metadata );

/// Policy gate for conversions: categorical sources refuse interpolated
/// resampling (anything beyond nearest/mode) with GeoError(FidelityLoss) —
/// averaging categories fabricates classes that exist nowhere. Continuous
/// sources pass; `resampling` "near"/"nearest"/"mode" always passes.
void checkResamplingPolicy( const RasterMetadata &source, const std::string &resampling );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_GRID_DESCRIPTOR_H
