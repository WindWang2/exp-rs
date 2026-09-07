/***************************************************************************
  geospatial/probe/probe.h
  Remote Sensing I/O Foundation 5.0 — unified probe / sniff / open contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One bounded, read-only pipeline that answers "what is this resource":

    URI classify → signature sniff (≤ 64 KiB, local files only)
      → GDAL driver identify/open → product adapter detect
      → (optional) lazy metadata inspect

  Contract:
  * a wrong extension never wins over content: signature/GDAL identification
    outrank the file name; extension is a hint only.
  * probing never scans pixels/features and never reads whole files; the
    signature read is byte-bounded and the metadata stage is the canonical
    lazy inspection.
  * every stage records a diagnostic; the result carries which stage decided.
  * failures are typed: NotFound / OpenFailed / UnsupportedFormat /
    CorruptData / UnsupportedProduct — never a bare string.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_PROBE_H
#define SICNU_GEOSPATIAL_PROBE_H

#include "geospatial/common.h"
#include "geospatial/formats/format_profiles.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/util/resource_uri.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

/// How far the probe goes. Everything is off by default except the cheap
/// classify+identify chain; each opt-in is documented as a cost.
struct ProbeOptions
{
    /// Runs the canonical lazy inspection after identification
    /// (raster/vector/multidim metadata; still no pixel scans).
    bool includeMetadata = false;
    /// Runs the product-adapter detection on the path.
    bool includeProduct = true;
    /// Reads the first bytes of a local file for signature sniffing
    /// (bounded by maxSignatureBytes). Off for remote resources.
    bool includeSignature = true;
    int maxSignatureBytes = 64 * 1024;
};

enum class ProbeStage
{
  None,           ///< nothing decided (probe failed before classification)
  Uri,            ///< resource kind classified
  Signature,      ///< byte signature
  GdalDriver,     ///< GDAL identify/open
  ProductAdapter, ///< product adapter claimed the path
  Metadata        ///< canonical metadata inspection
};

const char *probeStageName( ProbeStage stage );

/// Byte-signature classification of a file head (pure naming — never opens
/// GDAL, never reads more than `head`).
enum class FileSignature
{
  Unknown,
  Tiff,        ///< "II*\0" / "MM\0*"
  BigTiff,     ///< "II+\0" / "MM\0+"
  Zip,         ///< "PK" (Shapefile-in-zip, gpkg, kmz, Zarr v2 zip...)
  Gpkg,        ///< SQLite/GeoPackage magic
  Hdf5,        ///< HDF5 "\x89HDF"
  Hdf4,        ///< HDF4 "\x0e\x03\x13\x01"
  Netcdf,      ///< "CDF\001"/"CDF\002" (classic) — HDF5-based netCDF4 shows Hdf5
  Json,
  Xml,
  PlainText,
};

const char *fileSignatureName( FileSignature signature );
/// Classifies an already-read head buffer (any length up to a few KiB is
/// enough). Total function — empty buffers classify as Unknown.
FileSignature classifySignature( const std::vector<char> &head );

struct FormatDescriptor
{
    std::string profileId;        ///< FormatRegistry id ("" when no profile matched)
    std::string displayName;
    std::string driverName;       ///< GDAL driver short name ("GTiff", "netCDF", ...)
    Certification certification = Certification::Accessible;
    bool driverAvailable = false; ///< driver present in this GDAL build
    Json::Value toJson() const;
};

struct ProductDescriptor
{
    ProductKind kind = ProductKind::Unknown;
    std::string family;           ///< human family name ("Sentinel-2 SAFE", ...)
    Json::Value toJson() const;
};

struct ProbeResult
{
    std::string path;
    std::string displayPath;      ///< redacted display form (ADR 0135)
    ResourceKind resourceKind = ResourceKind::Invalid;
    ProbeStage decidedBy = ProbeStage::None;
    FileSignature signature = FileSignature::Unknown;

    FormatDescriptor format;      ///< identified format (may be empty for
                                  ///< non-GDAL resources like stac://)
    ProductDescriptor product;    ///< claimed product (Unknown when none)

    bool isCog = false;           ///< structural COG verdict (driver == COG)

    Json::Value diagnostics;      ///< array of {stage, note}

    Json::Value toJson() const;
};

/// Runs the probe pipeline. Throws GeoError(NotFound) when the local target
/// does not exist, GeoError(OpenFailed) when GDAL identifies nothing and the
/// signature is unknown, GeoError(CorruptData) when the signature matches a
/// known family but GDAL cannot open it, GeoError(UnsupportedFormat) when the
/// signature family exists but this build lacks the driver.
ProbeResult probeResource( const std::string &path, const ProbeOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_PROBE_H
