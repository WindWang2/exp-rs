/***************************************************************************
  geospatial/metadata/canonical_metadata.h
  Geospatial I/O Foundation 4.0 — canonical metadata model.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Canonical Metadata is the single in-memory vocabulary every I/O adapter maps
  into and out of. One dataset inspection = one GDAL open (read-only) + driver
  metadata queries. Inspection never triggers a full pixel/feature scan unless
  the caller explicitly opts in (e.g. RequestOptions::includeStatistics).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_CANONICAL_METADATA_H
#define SICNU_GEOSPATIAL_CANONICAL_METADATA_H

#include "geospatial/common.h"

#include <json/json.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Coordinate reference system description as carried by the dataset.
///
/// Axis-order policy (foundation-wide): geotransform and extent coordinates are
/// ALWAYS in traditional GIS order (x = east/lon first, y = north/lat second),
/// matching the GDAL geotransform convention. Authority axis order only plays
/// a role inside explicit coordinate transforms (see crs/crs_policy.h).
struct CrsInfo
{
    bool valid = false;
    std::string wkt;            ///< WKT (as reported by the driver; WKT2-capable)
    std::string authid;         ///< "EPSG:4326" when identifiable, empty otherwise
    bool isGeographic = false;
    bool isProjected = false;
    bool hasCoordinateEpoch = false;
    double coordinateEpoch = 0.0; ///< decimal year (GDAL coordinate epoch)

    Json::Value toJson() const;
    static CrsInfo fromJson( const Json::Value &json );
};

/// Per-band metadata. All "has*" flags exist because absence is meaningful:
/// fidelity policies must distinguish "unset" from "0".
struct BandInfo
{
    int index = 0;                ///< 1-based GDAL band index
    std::string dtype;            ///< "Byte", "UInt16", "Int16", "UInt32", "Int32",
                                  ///< "Float32", "Float64", "CFloat32", ...
    std::string description;

    bool hasNoData = false;
    double noDataValue = 0.0;
    bool noDataIsNaN = false;

    bool hasScale = false;
    double scale = 1.0;
    bool hasOffset = false;
    double offset = 0.0;
    std::string unit;             ///< physical unit ("" when undeclared)

    std::string role;             ///< canonical band role; "" unknown.
                                  ///< Vocabulary mirrors src/data/band_role.h
                                  ///< (Coastal, Blue, Green, Red, RedEdge, NIR,
                                  ///< NarrowNIR, SWIR1, SWIR2, Cirrus,
                                  ///< Panchromatic, Thermal, QA, SceneClassification)
    bool hasWavelength = false;
    double wavelengthNm = 0.0;
    bool hasFwhm = false;
    double fwhmNm = 0.0;

    std::string colorInterpretation; ///< "Undefined", "Gray", "Palette", "Red", ...
    bool hasColorTable = false;
    int colorTableEntryCount = 0;

    bool isMaskBand = false;      ///< GDAL mask band (ALL_VALID/PER_DATASET alpha/mask)

    std::map<std::string, std::string> metadata; ///< default-domain band metadata (bounded)

    Json::Value toJson() const;
    static BandInfo fromJson( const Json::Value &json );
};

/// Canonical raster metadata. Size/CRS/geotransform/band structure only — no
/// pixel values. `inspectRaster` fills it without scanning pixels.
struct RasterMetadata
{
    // identity
    std::string path;             ///< UTF-8 path / VSI handle as given
    std::string driver;           ///< short name, e.g. "GTiff", "COG" (read path)
    std::string driverLongName;

    // shape
    int width = 0;
    int height = 0;
    int bandCount = 0;

    // placement
    CrsInfo crs;
    bool hasGeotransform = false;
    std::array<double, 6> geotransform = { 0, 1, 0, 0, 0, 1 }; ///< GDAL order; traditional GIS coords
    bool hasExtent = false;       ///< true iff geotransform present and invertible
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    double resolutionX = 0, resolutionY = 0; ///< 0 when undeclared (no geotransform)

    // structure
    std::vector<BandInfo> bands;
    int overviewCount = 0;        ///< -1 unknown, >=0 counts
    std::string compression;      ///< IMAGE_STRUCTURE COMPRESSION ("" undeclared)
    std::string interleave;       ///< IMAGE_STRUCTURE INTERLEAVE ("" undeclared)
    std::vector<std::string> subdatasets; ///< "SUBDATASET_n_NAME=desc" entries

    // georeferencing extras
    bool hasGcps = false;
    int gcpCount = 0;
    bool hasRpc = false;

    // product / semantics (from SICNU_* metadata, standard product keys, or adapters)
    std::string sensor;
    std::string platform;
    std::string productId;
    std::string processingLevel;
    std::string acquisitionTime;  ///< ISO-8601 when declared
    std::string radiometricState; ///< ADR 0114 vocabulary ("toa_reflectance", ...)
    double numericScale = 0.0;    ///< quantification value (e.g. 10000), 0 undeclared
    bool hasCloudCover = false;   ///< eo:cloud_cover when declared (product/STAC)
    double cloudCover = 0.0;      ///< percent 0..100
    bool hasGsd = false;          ///< ground sample distance (m) when declared
    double gsd = 0.0;

    std::map<std::string, std::string> metadata; ///< dataset default-domain metadata (bounded)

    /// Serializes with "kind": "raster" and a version stamp.
    Json::Value toJson() const;
    static RasterMetadata fromJson( const Json::Value &json );

    bool isNull() const { return driver.empty() && width == 0 && height == 0; }
};

struct FieldInfo
{
    std::string name;
    std::string typeName;   ///< OGR type name ("String", "Integer64", "Real", ...)
    int width = 0;
    int precision = 0;
    bool unique = false;    ///< declared unique constraint when reported
    Json::Value toJson() const;
    static FieldInfo fromJson( const Json::Value &json );
};

struct VectorLayerInfo
{
    std::string name;
    std::string geometryTypeName;   ///< "Point", "Polygon", "MultiPolygon", "None", ...
    std::int64_t featureCount = -1; ///< -1 = not cheaply available
    bool featureCountExact = false; ///< true when driver reported an exact count
    CrsInfo crs;
    std::vector<FieldInfo> fields;
    bool hasExtent = false;
    bool extentExact = false;       ///< false when extent comes from a declared envelope
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::string encoding;           ///< declared text encoding ("" unknown)
    bool supportsFastSpatialFilter = false;
    bool supportsSequentialWrite = false;
    bool supportsRandomWrite = false;

    Json::Value toJson() const;
    static VectorLayerInfo fromJson( const Json::Value &json );
};

struct VectorMetadata
{
    std::string path;
    std::string driver;
    std::string driverLongName;
    std::vector<VectorLayerInfo> layers;

    Json::Value toJson() const;
    static VectorMetadata fromJson( const Json::Value &json );

    bool isNull() const { return driver.empty() && layers.empty(); }
};

struct DimensionInfo
{
    std::string name;
    std::int64_t size = 0;
    std::string type;        ///< GDAL dimension type ("TEMPORAL", "HORIZONTAL_X", ...) or ""
    std::string direction;   ///< "" when undeclared
    std::string unit;

    // 7.0 — coordinate axis values from the indexing variable (time steps,
    // depths, ...). Numeric axes only; capture is hard-bounded at
    // kMaxAxisValues entries with valuesBounded flagging a truncation.
    static constexpr std::size_t kMaxAxisValues = 4096;
    bool hasValues = false;      ///< true when numeric axis values were read
    bool valuesBounded = false;  ///< true when the axis was truncated at the cap
    std::vector<double> values;  ///< ascending count == min(size, kMaxAxisValues)

    Json::Value toJson() const;
    static DimensionInfo fromJson( const Json::Value &json );
};

struct VariableInfo
{
    std::string name;
    std::string dtype;
    std::vector<std::string> dimensionNames;  ///< ordered slowest→fastest per GDAL
    std::string unit;
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool noDataIsNaN = false;
    double scale = 1.0;
    double offset = 0.0;
    bool hasScale = false;
    bool hasOffset = false;
    std::map<std::string, std::string> attributes;

    // 7.0 — chunk shape as the storage reports it ("" when unknown), for
    // chunk-aware bounded read planning. Slowest -> fastest, matching
    // dimensionNames.
    std::vector<std::int64_t> blockShape;

    Json::Value toJson() const;
    static VariableInfo fromJson( const Json::Value &json );
};

struct MultidimMetadata
{
    std::string path;
    std::string driver;         ///< "netCDF", "HDF5", ...
    std::vector<DimensionInfo> dimensions;
    std::vector<VariableInfo> variables;
    CrsInfo crs;                ///< horizontal CRS when declared on x/y dims

    Json::Value toJson() const;
    static MultidimMetadata fromJson( const Json::Value &json );

    bool isNull() const { return driver.empty() && variables.empty(); }
};

/// What inspectRaster()/inspectVector() may additionally do. Nothing here is
/// implicit: every opt-in costs I/O and is documented as such.
struct InspectOptions
{
    bool includeStatistics = false; ///< decimated per-band stats (bounded read ≤512²)
    bool includeRemoteProbe = false; ///< doctor: attempt remote HEAD/range probe
    int maxSubdatasets = 64;        ///< bound subdataset listing
    int maxMetadataItems = 128;     ///< bound default-domain metadata capture
};

/// Inspect a raster source. One read-only open; never scans pixels unless
/// options request bounded statistics. Throws GeoError(OpenFailed) when the
/// source cannot be opened; GeoError(InvalidArgument) for an empty path.
RasterMetadata inspectRaster( const std::string &path, const InspectOptions &options = {} );

/// Inspect a vector source. Layer schema + cheap counts only; a full extent
/// scan happens only when the driver cannot declare an extent (and is then
/// flagged extentExact=false... true means scanned-or-declared exact; the flag
/// records whether a scan was required — see implementation notes).
VectorMetadata inspectVector( const std::string &path, const InspectOptions &options = {} );

/// Inspect a multidimensional source (GDAL MDArray groups). Read-only, lazy:
/// enumerates groups/dimensions/variables/attributes, never reads array data.
/// Throws GeoError(Unsupported) when the driver exposes no multidim API.
MultidimMetadata inspectMultidim( const std::string &path, const InspectOptions &options = {} );

/// Convenience: inspect whatever the path holds (raster first, then vector,
/// then multidim). Returns JSON with "kind": raster|vector|multidimensional.
Json::Value inspectAny( const std::string &path, const InspectOptions &options = {} );

/// Validates that a JSON document produced by toJson() parses back into the
/// model. Used by round-trip tests; not intended for external input.
RasterMetadata rasterMetadataFromJsonText( const std::string &jsonText, Json::Value *errors = nullptr );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_CANONICAL_METADATA_H
