/***************************************************************************
  geospatial/multidim/multidim_cube.h
  Cloud-Native Geospatial Data Fabric 9.0 (M5) — logical EO cube descriptor
  over the lazy multidim view.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  A cube descriptor is the bounded, serializable ANSWER to "what is this
  4D/5D store, and how do its axes resolve in time?":

    * one variable (the data cube) + one descriptor per dimension axis,
      carrying numeric axis values AND string labels verbatim, plus
      normalized UTC instants wherever the axis resolves (string datetime
      labels; numeric CF-relative units "… since <instant>").
    * instants resolve per label — an unresolvable axis keeps its verbatim
      values and says so (instantsResolved=false). Nothing is guessed, and
      NO array data is read: descriptors come from metadata + axis capture
      only, so 4D/5D stores stay lazy under any size.
    * the descriptor is JSON-symmetric (toJson/fromJson round-trip is exact)
      — the serialization is the workspace/temporal-seam contract, not a
      debug dump.
    * the spatial anchor (CRS + geotransform of the two trailing dimensions)
      rides along so consumers can geolocate slices without reopening.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_MULTIDIM_CUBE_H
#define SICNU_GEOSPATIAL_MULTIDIM_CUBE_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

class MultidimView;

struct MultidimCubeAxis
{
    std::string name;
    std::string type;            ///< GDAL dimension type ("TEMPORAL", ...) or ""
    std::string unit;            ///< declared unit (CF units for numeric axes)
    std::int64_t size = 0;

    // Verbatim capture (bounded per DimensionInfo::kMaxAxisValues).
    bool hasNumericValues = false;
    bool valuesBounded = false;
    std::vector<double> numericValues;
    bool hasStringLabels = false;
    bool stringValuesBounded = false;
    std::vector<std::string> stringLabels;

    // Normalized UTC instants, one per captured label/value, in axis order —
    // resolved from string datetime labels or CF-relative numeric units.
    // Empty when the axis does not resolve (verbatim values remain the truth).
    bool instantsResolved = false;
    std::vector<std::string> instantsUtc;

    Json::Value toJson() const;
    static MultidimCubeAxis fromJson( const Json::Value &json );
};

struct MultidimCubeDescriptor
{
    std::string path;
    std::string driver;

    // The data variable (one descriptor = one logical cube).
    std::string variable;
    std::string dtype;
    std::string unit;
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool noDataIsNaN = false;
    bool hasScale = false;
    double scale = 1.0;
    bool hasOffset = false;
    double offset = 0.0;
    std::string bandRole;        ///< declared variable role ("" when none)

    // Dimension order, slowest → fastest (exactly the variable's dims).
    std::vector<std::string> dimensionNames;
    std::vector<MultidimCubeAxis> axes; ///< parallel to dimensionNames

    // Spatial anchor for the two trailing (fastest) dimensions.
    CrsInfo crs;
    bool hasGeoTransform = false;
    std::vector<double> geotransform;   ///< 6 values when hasGeoTransform

    // Bounded read-planning facts (chunk shape, slowest → fastest).
    std::vector<std::int64_t> blockShape;

    Json::Value toJson() const;
    /// Symmetric inverse of toJson(). Throws GeoError(InvalidMetadata) on
    /// structural violations (missing variable/dimensions, ragged axes).
    static MultidimCubeDescriptor fromJson( const Json::Value &json );
};

/// Describes the logical cube of `variable` from an OPEN view. Lazy: no
/// array data is read beyond the axis capture already performed by the view.
/// Throws GeoError(InvalidArgument) for an unknown variable; the axes of
/// dimensions without captured coordinate values carry sizes only.
MultidimCubeDescriptor describeCube( const MultidimView &view, const std::string &variable );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_MULTIDIM_CUBE_H
