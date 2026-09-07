/***************************************************************************
  geospatial/crs/crs_policy.h
  Geospatial I/O Foundation 4.0 — explicit CRS resolution & transform policy.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Policy contract:
  * Source and destination CRS are always explicit at the transform boundary.
  * The working axis order of every transform is declared, never implicit.
  * A dataset without CRS is an error (MissingCrs) unless the caller passes an
    explicit, declared fallback (CrsPolicy::allowDeclaredFallback) — the layer
    never silently guesses a CRS.
  * Datum operation choice stays with PROJ/GDAL; ballpark vs exact is visible
    in transform diagnostics (transformDiagnostics), never silent.
  * Coordinate epoch is carried through when present (dynamic datums).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_CRS_POLICY_H
#define SICNU_GEOSPATIAL_CRS_POLICY_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <ogr_srs_api.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

/// Axis order used by transform INPUT/OUTPUT coordinates.
enum class AxisOrder
{
    /// Authority-defined order (EPSG:4326 = lat, lon). Required when a caller
    /// works with authority-ordered pairs end to end.
    Authority,
    /// Traditional GIS order (EPSG:4326 = lon, lat). The foundation default:
    /// it matches the geotransform/extent convention of CanonicalMetadata.
    TraditionalGis
};

const char *axisOrderName( AxisOrder order );
AxisOrder axisOrderFromName( const std::string &name ); ///< throws GeoError(InvalidArgument)

/// An immutable, validated spatial reference. Copyable (clones), movable.
class Crs
{
  public:
    Crs() = default;
    ~Crs();
    Crs( const Crs &other );
    Crs &operator=( const Crs &other );
    Crs( Crs &&other ) noexcept;
    Crs &operator=( Crs &&other ) noexcept;

    /// EPSG:NNNN (also ESRI:..., MIF:..., etc. through OSR user input).
    static Crs fromAuthid( const std::string &authid );
    /// WKT text (WKT1 or WKT2). Rejects non-WKT input.
    static Crs fromWkt( const std::string &wkt );
    /// Anything OSR accepts: "EPSG:4326", PROJ strings, WKT, "urn:ogc:def:...".
    static Crs fromUserInput( const std::string &text );
    /// Resolve a dataset's carried CRS. Missing → GeoError(MissingCrs);
    /// present but broken → GeoError(InvalidCrs). Never guesses.
    static Crs fromDatasetInfo( const CrsInfo &info );

    bool isValid() const;
    std::string wkt() const;
    std::string authid() const;
    bool isGeographic() const;
    bool isProjected() const;
    double coordinateEpoch() const; ///< 0 when unset
    OGRSpatialReferenceH handle() const { return mHandle; }

    Json::Value toJson() const;
    CrsInfo toCrsInfo() const;

  private:
    explicit Crs( OGRSpatialReferenceH handle );
    OGRSpatialReferenceH mHandle = nullptr;
};

struct CrsPoint
{
    double x = 0.0;
    double y = 0.0;
};

/// Axis-aligned bounding box in traditional GIS order (x = lon/east).
struct CrsBoundingBox
{
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    /// True when the transformed bounds wrap the antimeridian for a
    /// geographic target (longitudes need normalization handling).
    bool crossesAntimeridian = false;
};

struct TransformDiagnostics
{
    std::string sourceAuthid;
    std::string targetAuthid;
    AxisOrder workingOrder = AxisOrder::TraditionalGis;
    bool ballpark = false;      ///< no exact datum operation available (PROJ info)
    std::string cautionNotes;   ///< PROJ/GDAL caution text when present
};

class CrsTransform
{
  public:
    /// Builds a PROJ pipeline. Throws GeoError(TransformFailed) when the pair
    /// cannot be constructed (invalid CRS, no path between datums).
    static CrsTransform create( const Crs &source, const Crs &target,
                                AxisOrder workingOrder = AxisOrder::TraditionalGis );

    CrsTransform() = default;
    ~CrsTransform();
    CrsTransform( const CrsTransform & ) = delete;
    CrsTransform &operator=( const CrsTransform & ) = delete;
    CrsTransform( CrsTransform &&other ) noexcept;
    CrsTransform &operator=( CrsTransform &&other ) noexcept;

    CrsPoint forward( const CrsPoint &point ) const;  ///< throws GeoError(TransformFailed)
    CrsPoint inverse( const CrsPoint &point ) const;  ///< throws GeoError(TransformFailed)

    /// Transforms a bounding box by densified edge sampling (corner-only
    /// sampling is wrong across antimeridian/curved projections).
    CrsBoundingBox forwardBounds( const CrsBoundingBox &box, int edgeSamples = 16 ) const;

    const TransformDiagnostics &diagnostics() const { return mDiagnostics; }

    /// Expert access to the forward OCT handle for in-place geometry
    /// transforms (OGR_G_Transform). Owned by this object; null when moved.
    void *forwardHandle() const { return mForward; }

  private:
    CrsTransform( OGRCoordinateTransformationH transformation, TransformDiagnostics diagnostics );
    CrsPoint transform( OGRCoordinateTransformationH transformation, const CrsPoint &point,
                        const char *direction ) const;
    OGRCoordinateTransformationH mForward = nullptr;
    OGRCoordinateTransformationH mInverse = nullptr;
    TransformDiagnostics mDiagnostics;
    bool mTargetGeographic = false;
};

/// Caller-declared policy knobs for dataset CRS resolution.
struct CrsPolicy
{
    /// Working order for transforms created through this policy.
    AxisOrder workingOrder = AxisOrder::TraditionalGis;
    /// When true AND a fallbackCrs is provided, a missing dataset CRS resolves
    /// to fallbackCrs (still reported as "declared_fallback" in diagnostics).
    /// When false (default), missing dataset CRS is a hard error.
    bool allowDeclaredFallback = false;
    CrsInfo fallbackCrs;
};

/// Resolve the effective CRS for a dataset with the explicit policy.
/// Returns the CRS plus a flag telling whether the declared fallback was used.
struct ResolvedCrs
{
    Crs crs;
    bool usedDeclaredFallback = false;
};

ResolvedCrs resolveDatasetCrs( const CrsInfo &info, const CrsPolicy &policy,
                               const std::string &context = std::string() );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_CRS_POLICY_H
