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
/// Per-band radiometric calibration exactly as the sidecar declares it.
/// A band may declare gain only, bias only, both, or neither — each field is
/// independently flagged, and absent fields are absent (never defaulted).
struct BandCalibration
{
    std::string band;        ///< native band id ("B1", "MS5", ...)
    bool hasGain = false;
    double gain = 0.0;
    bool hasBias = false;
    double bias = 0.0;
};

struct ProductMetadata
{
    std::string productId;
    std::string sensor;           ///< "OLI_TIRS", "MSI", "C-SAR", "PMS1", "CCD1", ...
    std::string platform;         ///< "LANDSAT_8", "SENTINEL-2A", "GF1", "ZY3", "HJ1A"
    std::string processingLevel;  ///< "L1TP", "Level-1C", "GRD", "L1A", ...
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
    // CN products (ADR 0157): sensor mode ("PMS1", "WFV2", "NAD", "CCD1"),
    // declared orbit id, sun geometry and per-band calibration — all optional,
    // all explicitly flagged when absent.
    std::string sensorMode;       ///< camera/sensor mode token when declared
    std::string orbitId;          ///< declared orbit identifier when present
    bool hasSunElevation = false;
    double sunElevationDeg = 0.0; ///< sun elevation above horizon, degrees
    /// Provenance for sunElevationDeg when derived (e.g. from solar zenith).
    /// Empty when the sidecar declared elevation directly. First-class so it
    /// cannot be silently dropped when the bounded `extra` map is full.
    std::string sunElevationSource;
    bool hasSunAzimuth = false;
    double sunAzimuthDeg = 0.0;   ///< sun azimuth, degrees
    std::vector<BandCalibration> bandCalibration; ///< declared gain/bias per band
    std::vector<std::string> declaredBandIds;     ///< BandID order from the sidecar
    std::vector<std::pair<std::string, std::string>> extra; ///< bounded passthrough
    // Sidecar parse diagnostics (ADR 0147): detected sidecar generation,
    // root element, and top-level elements outside the parser's whitelist
    // (bounded). Empty object for non-CN adapters.
    Json::Value parseDiagnostics;

    Json::Value toJson() const;
};

enum class ProductKind
{
    Unknown,
    LandsatMtl,
    Sentinel2Safe,
    Sentinel1Safe,
    ModisContainer,
    GaofenProduct,     ///< GF-1/2/6 PMS/WFV + GF-7 FWD/BWD L1A (CRESDA sidecar XML + TIFF)
    Gaofen3SarProduct, ///< GF-3 SAR L1A, declared-metadata level (ADR 0159)
    Gaofen4Product,    ///< GF-4 PMI geostationary L1A (ADR 0159)
    Gaofen5Product,    ///< GF-5 AHSI hyperspectral L1A (ADR 0159)
    Zy3Product,    ///< ZY-3 TLC/NAD/FWD/BWD L1A
    Zy1Product,    ///< ZY-1 02C PMS/HRC + 02B CCD/HR + 02D/02E PMS/AHSI L1A
    HjCcdProduct,  ///< HJ-1A/1B CCD + HJ-2A/B CCD L1A
    CbersProduct,  ///< CBERS-4 MUX/WFI/PAN10, INPE sidecar generation (ADR 0159)
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
