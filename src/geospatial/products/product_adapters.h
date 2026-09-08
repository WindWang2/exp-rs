/***************************************************************************
  geospatial/products/product_adapters.h
  Geospatial I/O Foundation 4.0 — RS product metadata adapters.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One canonical place that knows how Landsat MTL, Sentinel-2 SAFE XML,
  Sentinel-1 manifest.safe, MODIS-style containers and generic GeoTIFF
  sidecars describe their products. Algorithms/operators/GUI consume this —
  they must not re-implement per-product parsing.

  Policy: everything declared by the product is copied verbatim; absence is
  absence. The adapters never invent CRS or acquisition times.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_PRODUCT_ADAPTERS_H
#define SICNU_GEOSPATIAL_PRODUCT_ADAPTERS_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Parses a Landsat MTL (key = value) file into an uppercase-key map.
/// Empty when the file cannot be read (MTL is the Collection-1/2 metadata
/// sidecar; used by the adapter and the asset registry).
std::map<std::string, std::string> parseLandsatMtlKeys( const std::string &mtlPath );

/// Product-level semantics extracted from sidecar/metadata files.
struct ProductMetadata
{
    std::string productId;
    std::string sensor;           ///< "OLI_TIRS", "MSI", "C-SAR", ...
    std::string platform;         ///< "LANDSAT_8", "SENTINEL-2A", "SENTINEL-1B"
    std::string processingLevel;  ///< "L1TP", "Level-1C", "GRD", ...
    std::string acquisitionTime;  ///< ISO-8601 when declared
    std::string radiometricState; ///< ADR 0114 vocabulary when determinable
    double numericScale = 0.0;    ///< quantification (e.g. S2 BOA 10000)
    bool hasCloudCover = false;
    double cloudCover = 0.0;      ///< percent
    bool hasResolution = false;
    double resolutionMeters = 0.0;
    std::string modality;         ///< optical / sar / dem
    std::vector<std::string> polarizations; ///< SAR
    std::string orbitDirection;   ///< ASCENDING / DESCENDING (SAR)
    std::string instrumentMode;   ///< SAR acquisition mode (IW/EW/SM)
    std::string crsHint;          ///< declared projection (UTM zone / EPSG text) when declared
    std::vector<std::pair<std::string, std::string>> extra; ///< bounded passthrough

    Json::Value toJson() const;
};

enum class ProductKind
{
    Unknown,
    LandsatMtl,
    Sentinel2Safe,
    Sentinel1Safe,
    ModisContainer,
    GenericRaster
};

/// Stable lowercase identifier ("landsat_mtl", "sentinel2_safe", ...).
const char *productKindName( ProductKind kind );

/// Human family name ("Landsat MTL scene", "Sentinel-2 SAFE product", ...).
std::string productKindDisplayName( ProductKind kind );

/// Detects what a path (dataset, sidecar or product directory) most likely
/// is. Never throws.
ProductKind detectProductKind( const std::string &path );

/// Reads product metadata from a sidecar/dataset path of the given kind.
/// Throws GeoError(OpenFailed) when the sidecar cannot be read,
/// GeoError(InvalidArgument) for Unknown kind.
ProductMetadata readProductMetadata( const std::string &path, ProductKind kind );

/// Convenience: detect + read (detection failure → structured error listing
/// what was tried).
ProductMetadata readProductMetadataAuto( const std::string &path );

/// Enriches canonical metadata in place with product metadata (only non-empty
/// fields are copied; canonical values already present win — the adapter is
/// the fallback layer, not an overwriter).
void enrichWithProductMetadata( RasterMetadata &canonical, const ProductMetadata &product );

/// Canonical band role for a product band name ("B4", "B8A", "band 10", ...).
/// Returns "" for unknown names; the vocabulary mirrors src/data/band_role.h.
std::string productBandRole( ProductKind kind, const std::string &bandName );

/// Landsat band role honoring the MTL SENSOR_ID: OLI/OLI_TIRS (Landsat 8/9)
/// and TM/ETM+ (Landsat 4-7) have different band assignments (B1 coastal vs
/// blue, B6 SWIR vs thermal). An unknown SENSOR_ID defaults to the OLI
/// layout (the current constellation) — a documented default, not a fact
/// about the scene.
std::string productLandsatBandRole( const std::string &sensorId, const std::string &bandName );

/// Center wavelength (nm) for a product band name; false when unknown.
bool productBandWavelengthNm( ProductKind kind, const std::string &bandName, double &wavelengthNm );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_PRODUCT_ADAPTERS_H
