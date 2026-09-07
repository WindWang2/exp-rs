/***************************************************************************
  geospatial/formats/format_profiles.h
  Geospatial I/O Foundation 4.0 — certified format profile registry.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  "GDAL has a driver" ≠ "ExpRS supports this format". A format becomes
  Certified only when this repository carries round-trip tests that prove the
  declared fidelity capabilities (see tests/test_io_roundtrip_matrix.cpp and
  docs/io/certified-formats.md). Everything else is Accessible: the driver may
  open it, but ExpRS makes no fidelity claim.

  Runtime driver presence is resolved against the loaded GDAL build: a
  Certified profile on a build without its driver degrades to Unsupported
  for this process — capability queries never lie.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FORMAT_PROFILES_H
#define SICNU_GEOSPATIAL_FORMAT_PROFILES_H

#include "geospatial/common.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

enum class Certification
{
    Certified,   ///< round-trip tested in this repository; fidelity claims below hold
    Accessible,  ///< GDAL can typically open it; ExpRS makes no fidelity claim
    Unsupported  ///< not offered by this layer (still reachable through raw GDAL)
};

enum class FormatFamily
{
    Raster,
    Vector,
    Multidim,
    Container
};

struct FormatProfile
{
    std::string id;                        ///< canonical id ("GeoTIFF", "COG", "GeoPackage", ...)
    std::string displayName;
    FormatFamily family = FormatFamily::Raster;
    std::vector<std::string> driverNames;  ///< GDAL driver short names backing this profile
    std::vector<std::string> extensions;   ///< lowercase, without dot

    Certification certification = Certification::Accessible;

    bool supportsRead = false;
    bool supportsWrite = false;
    bool supportsStreaming = false;        ///< windowed/block access, no full-load contract
    bool remoteCapable = false;            ///< works over /vsicurl/-style handles

    // Fidelity claims — only meaningful at Certified level.
    bool preservesCrs = false;
    bool preservesNoData = false;
    bool preservesScaleOffset = false;
    bool preservesBandMetadata = false;    ///< descriptions/roles/wavelengths/color tables
    bool preservesAttributes = false;      ///< vector: fields + values round-trip

    std::string notes;

    Json::Value toJson( bool driverAvailable ) const;
};

class FormatRegistry
{
  public:
    /// Process-wide registry (static table + runtime driver resolution).
    static const FormatRegistry &instance();

    /// All declared profiles, each annotated with runtime driver availability.
    std::vector<FormatProfile> profiles() const;

    /// Profile lookup by canonical id; nullptr when unknown.
    const FormatProfile *find( const std::string &id ) const;

    /// Profile guess by file extension (first match); nullptr when unknown.
    const FormatProfile *profileForPath( const std::string &path ) const;

    /// True when at least one backing driver of the profile is loaded.
    bool driverAvailable( const FormatProfile &profile ) const;

    /// The certified-format support matrix as JSON (documentation + doctor).
    Json::Value supportMatrixJson() const;

  private:
    FormatRegistry();
    std::vector<FormatProfile> mProfiles;
};

// ---------------------------------------------------------------------------
// Dataset-level capability resolution (5.0, ADR 0136)
//
// A profile declares what a format family *generally* supports; a concrete
// dataset answers what *this* source supports (a GeoTIFF without overviews
// does not offer overview selection). The resolver inspects the dataset once,
// read-only and metadata-only, and ANDs the profile declaration with what the
// dataset actually shows. "Driver exists" and "this dataset supports it" are
// distinct answers.
// ---------------------------------------------------------------------------

enum class DataCapability : std::uint64_t
{
  None = 0,
  Read = 1ULL << 0,          ///< dataset opens read-only
  Write = 1ULL << 1,         ///< driver offers creation/update for this family
  Update = 1ULL << 2,        ///< in-place update (not create-copy)
  WindowRead = 1ULL << 3,    ///< bounded window reads (raster)
  BlockRead = 1ULL << 4,     ///< native block access (raster)
  RandomAccess = 1ULL << 5,  ///< non-sequential access patterns OK
  Multiband = 1ULL << 6,     ///< more than one band
  Multidim = 1ULL << 7,      ///< GDAL multidim API (groups/arrays)
  Subdataset = 1ULL << 8,    ///< subdataset list non-empty
  Georeferencing = 1ULL << 9,///< geotransform and/or GCPs
  Crs = 1ULL << 10,          ///< valid CRS declared
  NoData = 1ULL << 11,       ///< at least one band declares NoData
  Mask = 1ULL << 12,         ///< mask/alpha band present
  Overviews = 1ULL << 13,    ///< overviews present
  Metadata = 1ULL << 14,     ///< metadata domains present beyond the default
  RemoteRange = 1ULL << 15,  ///< remote origin reached through a range-capable VSI handle
  Streaming = 1ULL << 16,    ///< windowed/block access without full-load contract
  Vector = 1ULL << 17,       ///< vector layers present
  Attributes = 1ULL << 18,   ///< vector attribute schema present
  Transactions = 1ULL << 19, ///< vector transaction support declared
};

DataCapability operator|( DataCapability a, DataCapability b );
bool hasCapability( DataCapability value, DataCapability flag );
DataCapability &operator|=( DataCapability &value, DataCapability flag );

/// Stable lowercase names for the JSON surface ("window_read", "block_read", ...).
std::vector<std::string> capabilityNames( DataCapability caps );

struct ResolvedCapabilities
{
    DataCapability caps = DataCapability::None;
    std::string profileId;      ///< matched profile ("" when none)
    std::string driver;         ///</ GDAL driver short name ("" when none)
    bool remote = false;        ///< source reached over a network VSI/URL
    bool isCog = false;         ///< structural COG verdict
    Json::Value notes;          ///< bounded introspection notes (why a flag is off)

    Json::Value toJson() const;
};

/// One read-only open (raster first, then vector, then multidim); never scans
/// pixels/features. Throws GeoError(OpenFailed) when nothing opens; the
/// capability answer for datasets that open is always complete (absence of a
/// flag means the dataset does not offer it).
ResolvedCapabilities resolveDatasetCapabilities( const std::string &path );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FORMAT_PROFILES_H
