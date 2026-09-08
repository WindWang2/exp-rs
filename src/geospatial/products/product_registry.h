/***************************************************************************
  geospatial/products/product_registry.h
  Remote Sensing I/O Foundation 5.0 — product adapter registry, constituent
  asset enumeration and completeness verdicts (ADR 0137).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Adapters *understand products*; pixel I/O stays delegated to GDAL. The
  registry answers three questions for a product path:
    1. which family claims it (Landsat MTL / Sentinel-2 SAFE / Sentinel-1
       SAFE / MODIS container / generic raster fallback)
    2. what constituents does it carry (measurements, masks, annotations,
       metadata sidecars, browse imagery) with native band names, canonical
       band roles and declared resolutions
    3. is the product *complete* — and when it is not, exactly which core
       constituents are missing (never a disguised partial import).

  Policy: everything declared by the product is copied verbatim; absence is
  absence. A missing core file degrades the verdict; nothing is fabricated.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_PRODUCT_REGISTRY_H
#define SICNU_GEOSPATIAL_PRODUCT_REGISTRY_H

#include "geospatial/common.h"
#include "geospatial/products/product_adapters.h"

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Logical constituent of a directory product.
struct ProductAsset
{
    std::string path;            ///< resolved path (product-relative joined)
    std::string role;            ///< "measurement" | "mask" | "annotation" |
                                 ///< "metadata" | "browse" | "thumbnail"
    std::string nativeBandName;  ///< "B4", "vv", "sur_refl_b01", "" for non-band assets
    std::string bandRole;        ///< canonical band-role vocabulary ("" unknown)
    bool hasWavelength = false;
    double wavelengthNm = 0.0;
    bool hasResolution = false;  ///< declared/derived resolution (S2 band suffix, ...)
    double resolutionMeters = 0.0;

    Json::Value toJson() const;
};

/// How a directory product presents itself to the layer.
enum class ProductCompleteness
{
  Complete,          ///< all core constituents present and parseable
  PartialReadable,   ///< core metadata readable; some constituents missing
  Invalid,           ///< core metadata missing/unparseable — not usable as this family
  UnsupportedVersion ///< family recognized, product version not understood
};

const char *productCompletenessName( ProductCompleteness completeness );

/// Enumeration result for one product path.
struct ProductAssets
{
    ProductKind kind = ProductKind::Unknown;
    std::string adapterId;
    std::string productId;
    ProductCompleteness completeness = ProductCompleteness::Invalid;
    std::vector<std::string> missingConstituents; ///< what a Complete product would carry
    std::vector<ProductAsset> assets;
    ProductMetadata metadata;                     ///< normalized product metadata
    Json::Value notes;                            ///< bounded diagnostics

    Json::Value toJson() const;
};

/// One adapter = one product family.
class ProductAdapter
{
  public:
    virtual ~ProductAdapter() = default;

    virtual std::string id() const = 0;
    virtual ProductKind kind() const = 0;
    /// Cheap, read-only claim check (path shape + targeted file existence;
    /// no GDAL opens, no directory scans beyond targeted marker checks).
    virtual bool accepts( const std::string &path ) const = 0;
    /// Full enumeration + completeness verdict. Throws GeoError for
    /// hard failures (unreadable metadata of the claimed family).
    virtual ProductAssets enumerate( const std::string &path ) const = 0;
};

/// Registry of built-in adapters (Landsat MTL, Sentinel-2 SAFE, Sentinel-1
/// SAFE, MODIS container) plus the GenericRaster fallback that always claims
/// openable rasters. Process-wide, thread-safe after first use.
class ProductAdapterRegistry
{
  public:
    static ProductAdapterRegistry &instance();

    /// The first adapter whose accepts() fires; never null (GenericRaster
    /// fallback claims everything else). Specific adapters win over the
    /// fallback in registration order.
    ProductAdapter *adapterFor( const std::string &path ) const;

    /// adapterFor(path)->enumerate(path).
    ProductAssets describe( const std::string &path ) const;

  private:
    ProductAdapterRegistry();
    std::vector<std::unique_ptr<ProductAdapter>> mAdapters;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_PRODUCT_REGISTRY_H
