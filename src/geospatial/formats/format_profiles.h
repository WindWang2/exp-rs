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

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FORMAT_PROFILES_H
